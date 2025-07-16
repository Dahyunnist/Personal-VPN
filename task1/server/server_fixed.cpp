#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/bind/bind.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <unistd.h>
#include <cstring>
#include <array>
#include <queue>
#include <mutex>

/*compile command:
g++ server_fixed.cpp -o server_fixed -lboost_system -lboost_thread -lpthread -lssl -lcrypto
*/

namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

// === 配置参数 ===
const std::string TUN_DEV = "tun0";               // TUN设备名称
const std::string SERVER_TUN_IP = "10.8.0.1";    // 服务端TUN IP
const std::string TUN_MASK = "24";                // 子网掩码
const std::string TUN_NETWORK = "10.8.0.0/24";   // TUN子网（用于NAT）
const std::string PHYSICAL_NIC = "eth0";          // 物理网卡（根据实际情况修改）
const size_t BUFFER_SIZE = 4096;                  // 缓冲区大小

// TLS配置
const std::string CERTIFICATE_PATH = "certs/server.crt";
const std::string PRIVATE_KEY_PATH = "certs/server.key";

// 全局变量
static std::atomic_bool should_exit(false);

// === TUN设备管理类 ===
class TunDevice {
public:
    TunDevice(const std::string& dev_name) : fd_(-1) {
        if ((fd_ = open("/dev/net/tun", O_RDWR)) < 0) {
            throw std::runtime_error("Failed to open /dev/net/tun: " + std::string(strerror(errno)));
        }

        struct ifreq ifr;
        std::memset(&ifr, 0, sizeof(ifr));
        ifr.ifr_flags = IFF_TUN | IFF_NO_PI;  // 三层TUN设备，无包头
        std::strncpy(ifr.ifr_name, dev_name.c_str(), IFNAMSIZ - 1);

        if (ioctl(fd_, TUNSETIFF, reinterpret_cast<void*>(&ifr)) < 0) {
            close(fd_);
            throw std::runtime_error("Failed to ioctl TUNSETIFF: " + std::string(strerror(errno)));
        }
        std::cout << "TUN设备创建成功: " << ifr.ifr_name << " (fd: " << fd_ << ")" << std::endl;
    }

    ~TunDevice() {
        if (fd_ != -1) {
            close(fd_);
            std::cout << "TUN设备关闭" << std::endl;
        }
    }

    ssize_t read(uint8_t* buffer, size_t size) {
        return ::read(fd_, buffer, size);
    }

    ssize_t write(const uint8_t* data, size_t size) {
        ssize_t written = ::write(fd_, data, size);
        if (written < 0) {
            std::cerr << "TUN write failed: " << strerror(errno) << std::endl;
        }
        return written;
    }

    int get_fd() const { return fd_; }

    TunDevice(const TunDevice&) = delete;
    TunDevice& operator=(const TunDevice&) = delete;

private:
    int fd_;
};

// === 会话管理类（修复版）===
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(ssl::stream<tcp::socket> socket, TunDevice& tun)
        : socket_(std::move(socket)), tun_(tun), client_endpoint_(socket_.lowest_layer().remote_endpoint()) {}

    void start() {
        do_handshake();
    }

private:
    void do_handshake() {
        auto self(shared_from_this());
        socket_.async_handshake(ssl::stream_base::server,
            [this, self](const boost::system::error_code& ec) {
                if (!ec) {
                    std::cout << "客户端连接成功（TLS加密）: " << client_endpoint_ << std::endl;
                    
                    // 启动双向数据转发
                    start_ssl_to_tun();
                    start_tun_to_ssl();
                } else {
                    std::cerr << "TLS握手失败: " << ec.message() << std::endl;
                }
            });
    }

    // SSL → TUN 数据流
    void start_ssl_to_tun() {
        auto self(shared_from_this());
        socket_.async_read_some(asio::buffer(ssl_buffer_),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec && length > 0) {
                    std::cout << "SSL → TUN: 接收到 " << length << " 字节，转发到TUN设备" << std::endl;
                    
                    // 打印数据包信息（用于调试）
                    std::cout << "数据包内容（前16字节）: ";
                    for (size_t i = 0; i < std::min(length, static_cast<size_t>(16)); ++i) {
                        printf("%02x ", ssl_buffer_[i]);
                    }
                    std::cout << std::endl;
                    
                    // 写入TUN设备
                    ssize_t written = tun_.write(ssl_buffer_.data(), length);
                    if (written > 0) {
                        std::cout << "SSL → TUN: 成功写入 " << written << " 字节到TUN设备" << std::endl;
                    } else {
                        std::cerr << "SSL → TUN: TUN写入失败" << std::endl;
                    }
                    
                    // 继续读取
                    start_ssl_to_tun();
                } else {
                    if (ec) {
                        std::cout << "客户端断开连接: " << ec.message() << std::endl;
                    }
                    // 连接断开，停止TUN→SSL线程
                    should_exit = true;
                }
            });
    }

    // TUN → SSL 数据流（在单独线程中运行）
    void start_tun_to_ssl() {
        tun_thread_ = std::thread([this, self = shared_from_this()]() {
            std::array<uint8_t, BUFFER_SIZE> tun_buffer;
            
            while (!should_exit) {
                // 从TUN设备读取数据
                ssize_t bytes_read = tun_.read(tun_buffer.data(), tun_buffer.size());
                
                if (bytes_read > 0) {
                    std::cout << "TUN → SSL: 从TUN读取 " << bytes_read << " 字节，发送到客户端" << std::endl;
                    
                    // 打印数据包信息（用于调试）
                    std::cout << "TUN数据包内容（前16字节）: ";
                    for (ssize_t i = 0; i < std::min(bytes_read, static_cast<ssize_t>(16)); ++i) {
                        printf("%02x ", tun_buffer[i]);
                    }
                    std::cout << std::endl;
                    
                    // 通过SSL发送到客户端
                    boost::system::error_code ec;
                    size_t written = asio::write(socket_, asio::buffer(tun_buffer.data(), bytes_read), ec);
                    
                    if (!ec) {
                        std::cout << "TUN → SSL: 成功发送 " << written << " 字节到客户端" << std::endl;
                    } else {
                        std::cerr << "TUN → SSL: SSL发送失败: " << ec.message() << std::endl;
                        break;
                    }
                } else if (bytes_read < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // 无数据可读，短暂等待
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        continue;
                    } else {
                        std::cerr << "TUN → SSL: TUN读取错误: " << strerror(errno) << std::endl;
                        break;
                    }
                }
            }
            std::cout << "TUN → SSL 线程退出" << std::endl;
        });
        
        // 分离线程，让它独立运行
        tun_thread_.detach();
    }

    ssl::stream<tcp::socket> socket_;
    TunDevice& tun_;
    tcp::endpoint client_endpoint_;
    std::array<uint8_t, BUFFER_SIZE> ssl_buffer_;
    std::thread tun_thread_;
};

// === 服务器类（修复版）===
class Server {
public:
    Server(asio::io_context& io_context, short port, ssl::context& ssl_ctx, TunDevice& tun)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)), 
          ssl_ctx_(ssl_ctx), tun_(tun) {
        start_accept();
    }

private:
    void start_accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::cout << "新客户端连接: " << socket.remote_endpoint() << std::endl;
                    // 创建SSL会话并传递TUN设备引用
                    std::make_shared<Session>(
                        ssl::stream<tcp::socket>(std::move(socket), ssl_ctx_), 
                        tun_
                    )->start();
                } else {
                    std::cerr << "Accept错误: " << ec.message() << std::endl;
                }
                start_accept();
            });
    }

    tcp::acceptor acceptor_;
    ssl::context& ssl_ctx_;
    TunDevice& tun_;
};

// === 系统配置工具类 ===
class SystemConfig {
public:
    static void run_command(const std::string& cmd) {
        std::cout << "执行命令: " << cmd << std::endl;
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            std::cerr << "命令执行失败: " << cmd << " (返回值: " << ret << ")" << std::endl;
        }
    }

    static void configure_tun(const std::string& dev, const std::string& ip, const std::string& mask) {
        run_command("ip addr add " + ip + "/" + mask + " dev " + dev);
        run_command("ip link set dev " + dev + " up");
        std::cout << "TUN设备配置完成: " << dev << " " << ip << "/" << mask << std::endl;
    }

    static void enable_ip_forward() {
        run_command("echo 1 > /proc/sys/net/ipv4/ip_forward");
        std::cout << "IP转发已启用" << std::endl;
    }

    static void setup_iptables_nat(const std::string& network, const std::string& nic) {
        // 清理旧规则
        run_command("iptables -t nat -D POSTROUTING -s " + network + " -o " + nic + " -j MASQUERADE 2>/dev/null || true");
        run_command("iptables -D FORWARD -i " + TUN_DEV + " -j ACCEPT 2>/dev/null || true");
        run_command("iptables -D FORWARD -o " + TUN_DEV + " -j ACCEPT 2>/dev/null || true");
        
        // 添加新规则
        run_command("iptables -A FORWARD -i " + TUN_DEV + " -j ACCEPT");
        run_command("iptables -A FORWARD -o " + TUN_DEV + " -j ACCEPT");
        run_command("iptables -t nat -A POSTROUTING -s " + network + " -o " + nic + " -j MASQUERADE");
        std::cout << "iptables NAT规则配置完成: " << network << " -> " << nic << std::endl;
    }
};

// === SSL上下文配置函数 ===
ssl::context create_ssl_context() {
    ssl::context ctx(ssl::context::tls_server);

    try {
        ctx.use_certificate_chain_file(CERTIFICATE_PATH);
        ctx.use_private_key_file(PRIVATE_KEY_PATH, ssl::context::pem);
        ctx.set_options(
            ssl::context::default_workarounds |
            ssl::context::no_sslv2 |
            ssl::context::no_sslv3 |
            ssl::context::no_tlsv1 |
            ssl::context::no_tlsv1_1
        );
        std::cout << "SSL上下文配置成功" << std::endl;
    } catch (std::exception& e) {
        std::cerr << "SSL证书加载失败: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    return ctx;
}

// === 主函数 ===
int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "用法: " << argv[0] << " <监听端口>" << std::endl;
            return 1;
        }

        std::cout << "=== VPN服务端启动 ===" << std::endl;

        // 1. 启用IP转发
        SystemConfig::enable_ip_forward();

        // 2. 创建并配置TUN设备
        TunDevice tun(TUN_DEV);
        SystemConfig::configure_tun(TUN_DEV, SERVER_TUN_IP, TUN_MASK);

        // 3. 配置iptables NAT规则
        SystemConfig::setup_iptables_nat(TUN_NETWORK, PHYSICAL_NIC);

        // 4. 创建SSL上下文
        ssl::context ssl_ctx = create_ssl_context();

        // 5. 启动VPN服务器
        asio::io_context io_context;
        Server server(io_context, std::atoi(argv[1]), ssl_ctx, tun);
        
        std::cout << "VPN服务器已启动，监听端口: " << argv[1] << std::endl;
        std::cout << "TUN设备: " << TUN_DEV << " (" << SERVER_TUN_IP << "/" << TUN_MASK << ")" << std::endl;

        // 6. 运行IO服务（多线程支持）
        std::vector<std::thread> threads;
        for (size_t i = 0; i < std::thread::hardware_concurrency(); ++i) {
            threads.emplace_back([&io_context]() { io_context.run(); });
        }

        // 等待所有线程完成
        for (auto& t : threads) {
            t.join();
        }

    } catch (std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}