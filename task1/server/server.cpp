#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>  // TLS依赖
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
/*compile command:
g++ server.cpp -o server -lboost_system -lboost_thread -lpthread -lssl -lcrypto
*/

/*get ip for server(WSL actually)
ip addr show eth0 | grep -oP '(?<=inet\s)\d+(\.\d+){3}/\d+' | cut -d '/' -f 1
*/

namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;  // SSL命名空间
using tcp = boost::asio::ip::tcp;

// === 配置参数 ===
const std::string TUN_DEV = "tun0";               // TUN设备名称
const std::string SERVER_TUN_IP = "10.8.0.1";     // 服务端TUN IP
const std::string TUN_MASK = "255.255.255.0";     // 子网掩码
const std::string TUN_NETWORK = "10.8.0.0/24";    // TUN子网（用于NAT）
const std::string PHYSICAL_NIC = "eth0";          // WSL物理网卡（通过`ip addr`查看）
const size_t BUFFER_SIZE = 4096;                  // 缓冲区大小
// TLS配置（需替换为你的证书路径）
const std::string CERTIFICATE_PATH = "certs/server.crt";  // SSL证书路径
const std::string PRIVATE_KEY_PATH = "certs/server.key";  // SSL私钥路径

// === TUN设备管理类（不变） ===
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

    ssize_t write(const uint8_t* raw_data, size_t raw_size) {
        // uint32_t tun_header = htonl(0x0800);
        // std::vector<uint8_t> tun_packet;
        // tun_packet.resize(4 + raw_size);
        // memcpy(tun_packet.data(), &tun_header, 4);
        // memcpy(tun_packet.data() + 4, raw_data, raw_size);
        // ssize_t written = ::write(fd_, tun_packet.data(), tun_packet.size());
        ssize_t written = ::write(fd_, raw_data, raw_size);
        if(written < 0){
            std::cerr << "TUN write failed: expected " << raw_size << " bytes, " << written << " bytes actually" << std::endl;
            std::cerr << "Error: " << strerror(errno) << std::endl;
        }
        return written;
    }

    TunDevice(const TunDevice&) = delete;
    TunDevice& operator=(const TunDevice&) = delete;

private:
    int fd_;
};

// === 会话管理类（TLS加密版）===
class Session : public std::enable_shared_from_this<Session> {
public:
    // 使用SSL流替代原始socket
    Session(ssl::stream<tcp::socket> socket, TunDevice& tun) 
        : socket_(std::move(socket)), tun_(tun) {}

    void start() {
        // 执行TLS握手
        do_handshake();
    }

private:
    // TLS握手
    void do_handshake() {
        auto self(shared_from_this());
        socket_.async_handshake(ssl::stream_base::server,  // 服务端模式
            [this, self](const boost::system::error_code& ec) {
                if (!ec) {
                    std::cout << "客户端已连接（TLS加密）: " << socket_.lowest_layer().remote_endpoint() << std::endl;
                    // 握手成功后启动双向转发
                    tun_to_client();
                    client_to_tun();
                } else {
                    std::cerr << "TLS握手失败: " << ec.message() << std::endl;
                }
            });
    }

    // TUN -> 客户端（加密发送）
    void tun_to_client() {
        auto self(shared_from_this());
        std::thread([this, self]() {
            std::array<uint8_t, BUFFER_SIZE> buffer;
            while (true) {
                ssize_t n = tun_.read(buffer.data(), buffer.size());
                if (n <= 0) {
                    std::cerr << "TUN读取失败: " << std::string(strerror(errno)) << std::endl;
                    break;
                }
                // 通过SSL流异步发送
                asio::async_write(socket_, asio::buffer(buffer.data(), n),
                    [this, self, n](boost::system::error_code ec, std::size_t /*length*/) {
                        if (ec) {
                            std::cerr << "加密发送失败: " << ec.message() << std::endl;
                        } else {
                            std::cout << "TUN -> 客户端（加密）: " << n << " bytes" << std::endl;
                        }
                    });
            }
        }).detach();
    }

    // 客户端 -> TUN（解密接收）
    void client_to_tun() {
        auto self(shared_from_this());
        socket_.async_read_some(asio::buffer(buffer_),  // SSL流读取（自动解密）
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec) {
                    ssize_t n = tun_.write(buffer_.data(), length);
                    if (n != length) {
                        std::cerr << "TUN写入失败: 预期" << length << "bytes，实际" << n << "bytes" << std::endl;
                    } else {
                        std::cout << "客户端（加密） -> TUN: " << length << " bytes" << std::endl;
                    }
                    client_to_tun();  // 继续读取
                } else {
                    std::cerr << "客户端断开连接: " << ec.message() << std::endl;
                }
            });
    }

    ssl::stream<tcp::socket> socket_;  // SSL加密流
    TunDevice& tun_;
    std::array<uint8_t, BUFFER_SIZE> buffer_;
};

// === 服务器类（TLS加密版）===
class Server {
public:
    Server(asio::io_context& io_context, short port, TunDevice& tun, ssl::context& ssl_ctx)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)), 
          tun_(tun), 
          ssl_ctx_(ssl_ctx) {
        start_accept();
    }

private:
    // 异步接受TLS连接
    void start_accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    // 将TCP socket包装为SSL流并创建会话
                    std::make_shared<Session>(ssl::stream<tcp::socket>(std::move(socket), ssl_ctx_), tun_)->start();
                }
                start_accept();  // 继续接受下一个连接
            });
    }

    tcp::acceptor acceptor_;
    TunDevice& tun_;
    ssl::context& ssl_ctx_;  // SSL上下文（包含证书配置）
};

// === 系统配置工具类（不变）===
class SystemConfig {
public:
    static void run_command(const std::string& cmd) {
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            std::cerr << "命令执行失败: " << cmd << " (返回值: " << ret << ")" << std::endl;
            if (cmd.find("iptables") != std::string::npos || cmd.find("ip_forward") != std::string::npos) {
                exit(EXIT_FAILURE);
            }
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
        run_command("iptables -A FORWARD -i " + TUN_DEV + " -j ACCEPT");
        run_command("iptables -A FORWARD -o " + TUN_DEV + " -j ACCEPT");
        run_command("iptables -t nat -A POSTROUTING -s " + network + " -o " + nic + " -j MASQUERADE");
        std::cout << "iptables NAT规则配置完成: " << network << " -> " << nic << std::endl;
    }
};

// === SSL上下文配置函数 ===
ssl::context create_ssl_context() {
    ssl::context ctx(ssl::context::tls_server);  // 服务端TLS上下文

    // 加载证书和私钥（替换为你的证书路径）
    try {
        ctx.use_certificate_chain_file(CERTIFICATE_PATH);
        ctx.use_private_key_file(PRIVATE_KEY_PATH, ssl::context::pem);
        // 可选：配置TLS版本（兼容客户端）
        ctx.set_options(
            ssl::context::default_workarounds |
            ssl::context::no_sslv2 |
            ssl::context::no_sslv3 |
            ssl::context::no_tlsv1 |
            ssl::context::no_tlsv1_1  // 仅允许TLSv1.2及以上
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

        // 1. 配置系统环境（IP转发、NAT等）
        SystemConfig::enable_ip_forward();

        // 2. 创建并配置TUN设备
        TunDevice tun(TUN_DEV);
        SystemConfig::configure_tun(TUN_DEV, SERVER_TUN_IP, TUN_MASK);
        SystemConfig::setup_iptables_nat(TUN_NETWORK, PHYSICAL_NIC);

        // 3. 创建SSL上下文（加载证书）
        ssl::context ssl_ctx = create_ssl_context();

        // 4. 启动TLS加密服务器
        asio::io_context io_context;
        Server server(io_context, std::atoi(argv[1]), tun, ssl_ctx);
        std::cout << "TLS服务器已启动，监听端口: " << argv[1] << std::endl;

        // 5. 运行IO服务（多线程支持）
        std::vector<std::thread> threads;
        for (size_t i = 0; i < std::thread::hardware_concurrency(); ++i) {
            threads.emplace_back([&io_context]() { io_context.run(); });
        }
        for (auto& t : threads) {
            t.join();
        }
    } catch (std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}