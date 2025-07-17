#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <thread>
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
#include <atomic>
#include <csignal>
/*compile command:
g++ server.cpp -o server -lboost_system -lboost_thread -lpthread -lssl -lcrypto
*/

namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

// === 配置参数 ===
const std::string TUN_DEV = "tun0";
const std::string SERVER_TUN_IP = "10.8.0.1";
const std::string TUN_MASK = "24";
const std::string TUN_NETWORK = "10.8.0.0/24";
const std::string PHYSICAL_NIC = "ens33";
const size_t BUFFER_SIZE = 4096;
const std::string CERTIFICATE_PATH = "certs/server.crt";
const std::string PRIVATE_KEY_PATH = "certs/server.key";

// === TUN设备管理类 ===
class TunDevice {
public:
    TunDevice(const std::string& dev_name) : fd_(-1) {
        if ((fd_ = open("/dev/net/tun", O_RDWR)) < 0) {
            throw std::runtime_error("Failed to open /dev/net/tun: " + std::string(strerror(errno)));
        }

        struct ifreq ifr;
        std::memset(&ifr, 0, sizeof(ifr));
        ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
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
        ssize_t written = ::write(fd_, raw_data, raw_size);
        if (written < 0) {
            std::cerr << "TUN写入失败: " << strerror(errno) << std::endl;
        }
        return written;
    }

    int fd() const { return fd_; }

    TunDevice(const TunDevice&) = delete;
    TunDevice& operator=(const TunDevice&) = delete;

private:
    int fd_;
};

// === 会话管理类 ===
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(ssl::stream<tcp::socket> socket, TunDevice& tun) 
        : socket_(std::move(socket)), tun_(tun), active_(true) {}

    void start() {
        do_handshake();
    }

    void stop() {
        if (active_) {
            active_ = false;
            boost::system::error_code ec;
            socket_.lowest_layer().close(ec);
            std::cout << "会话关闭" << std::endl;
        }
    }

    bool is_active() const { return active_; }

    // 写入数据到VPN隧道（发送给客户端）
    void async_write(const uint8_t* data, size_t size) {
        if (!active_) return;
        
        auto self(shared_from_this());
        asio::async_write(socket_, asio::buffer(data, size),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (ec) {
                    std::cerr << "发送失败: " << ec.message() << std::endl;
                    stop();
                }
            });
    }

    // 从VPN隧道读取数据（客户端发来的）
    void start_reading() {
        if (!active_) return;
        
        auto self(shared_from_this());
        socket_.async_read_some(asio::buffer(buffer_),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec && active_) {
                    // 将客户端发来的数据写入TUN设备（发送到互联网）
                    ssize_t n = tun_.write(buffer_.data(), length);
                    if (n != static_cast<ssize_t>(length)) {
                        std::cerr << "TUN写入失败: 预期" << length << "字节，实际" << n << "字节" << std::endl;
                    } else {
                        std::cout << "客户端 -> TUN: " << length << "字节" << std::endl;
                    }
                    // 继续读取
                    start_reading();
                } else {
                    if (ec) {
                        std::cerr << "客户端断开连接: " << ec.message() << std::endl;
                    }
                    stop();
                }
            });
    }

private:
    // TLS握手
    void do_handshake() {
        auto self(shared_from_this());
        socket_.async_handshake(ssl::stream_base::server,
            [this, self](const boost::system::error_code& ec) {
                if (!ec) {
                    std::cout << "客户端已连接: " 
                              << socket_.lowest_layer().remote_endpoint() << std::endl;
                    // 握手成功后开始读取客户端数据
                    start_reading();
                } else {
                    std::cerr << "TLS握手失败: " << ec.message() << std::endl;
                    active_ = false;
                }
            });
    }

    ssl::stream<tcp::socket> socket_;  // SSL加密流
    TunDevice& tun_;
    std::array<uint8_t, BUFFER_SIZE> buffer_;
    std::atomic<bool> active_;
};

// === 服务器类 ===
class Server {
public:
    Server(asio::io_context& io_context, short port, TunDevice& tun, ssl::context& ssl_ctx)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)), 
          tun_(tun), 
          ssl_ctx_(ssl_ctx),
          io_context_(io_context) {
        // 启动TUN读取线程（处理来自互联网的响应）
        tun_to_client_thread_ = std::thread(&Server::tun_read_loop, this);
        // 开始接受客户端连接
        start_accept();
    }

    ~Server() {
        // 停止所有会话
        for (auto& session : sessions_) {
            session->stop();
        }
        sessions_.clear();
        
        // 停止TUN读取线程
        running_ = false;
        if (tun_to_client_thread_.joinable()) {
            tun_to_client_thread_.join();
        }
    }

private:
    // 从TUN设备读取数据（来自互联网的响应）
    void tun_read_loop() {
        std::vector<uint8_t> buffer(BUFFER_SIZE);
        running_ = true;
        
        std::cout << "TUN读取线程启动" << std::endl;
        
        while (running_) {
            // 从TUN设备读取数据
            ssize_t n = tun_.read(buffer.data(), buffer.size());
            if (n <= 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                std::cerr << "TUN读取错误: " << strerror(errno) << std::endl;
                break;
            }
            
            // 将数据发送给所有活跃的客户端
            asio::post(io_context_, [this, buffer, n]() {
                broadcast_to_clients(buffer.data(), n);
            });
        }
        
        std::cout << "TUN读取线程停止" << std::endl;
    }
    
    // 广播数据给所有客户端（发送响应）
    void broadcast_to_clients(const uint8_t* data, size_t size) {
        auto it = sessions_.begin();
        while (it != sessions_.end()) {
            auto session = *it;
            if (session->is_active()) {
                // 发送数据给客户端
                session->async_write(data, size);
                ++it;
            } else {
                // 移除不活跃的会话
                it = sessions_.erase(it);
            }
        }
    }

    // 接受新客户端连接
    void start_accept() {
        auto socket = std::make_shared<tcp::socket>(io_context_);
        acceptor_.async_accept(*socket,
            [this, socket](boost::system::error_code ec) {
                if (!ec) {
                    // 创建SSL流并启动会话
                    auto ssl_stream = std::make_shared<ssl::stream<tcp::socket>>(
                        std::move(*socket), ssl_ctx_);
                    auto session = std::make_shared<Session>(std::move(*ssl_stream), tun_);
                    sessions_.push_back(session);
                    session->start();
                    std::cout << "新客户端连接" << std::endl;
                } else {
                    std::cerr << "接受连接失败: " << ec.message() << std::endl;
                }
                start_accept();  // 继续接受连接
            });
    }

    tcp::acceptor acceptor_;
    TunDevice& tun_;
    ssl::context& ssl_ctx_;
    asio::io_context& io_context_;
    std::vector<std::shared_ptr<Session>> sessions_;  // 客户端会话列表
    std::thread tun_to_client_thread_;
    std::atomic<bool> running_{false};
};

// === 系统配置工具类 ===
class SystemConfig {
public:
    static void run_command(const std::string& cmd) {
        std::cout << "执行命令: " << cmd << std::endl;
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            std::cerr << "命令执行失败: " << cmd << " (返回值: " << ret << ")" << std::endl;
        } else {
            std::cout << "命令执行成功" << std::endl;
        }
    }

    static void configure_tun(const std::string& dev, const std::string& ip, const std::string& mask) {
        run_command("ip link set dev " + dev + " down");
        run_command("ip addr flush dev " + dev);
        run_command("ip addr add " + ip + "/" + mask + " dev " + dev);
        run_command("ip link set dev " + dev + " up");
        run_command("ip route add 10.8.0.0/24 dev " + dev + " proto kernel scope link src " + ip);
        std::cout << "TUN设备配置完成: " << dev << " " << ip << "/" << mask << std::endl;
    }

    static void enable_ip_forward() {
        run_command("sysctl -w net.ipv4.ip_forward=1");
        std::cout << "IP转发已启用" << std::endl;
    }

    static void setup_iptables_nat(const std::string& network, const std::string& nic) {
        run_command("iptables -t nat -F");
        run_command("iptables -t nat -A POSTROUTING -s " + network + " -o " + nic + " -j MASQUERADE");
        run_command("iptables -A FORWARD -i " + TUN_DEV + " -o " + nic + " -j ACCEPT");
        run_command("iptables -A FORWARD -i " + nic + " -o " + TUN_DEV + " -m state --state RELATED,ESTABLISHED -j ACCEPT");
        std::cout << "iptables NAT规则配置完成" << std::endl;
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
            ssl::context::no_sslv3
        );
        std::cout << "SSL上下文配置成功" << std::endl;
    } catch (std::exception& e) {
        std::cerr << "SSL证书加载失败: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    return ctx;
}

// === 全局变量和信号处理 ===
std::unique_ptr<Server> server;
std::unique_ptr<TunDevice> tun_device;

void handle_signal(int signum) {
    std::cout << "\n收到信号 " << signum << "，正在关闭服务器..." << std::endl;
    server.reset();  // 销毁服务器
    tun_device.reset();
    exit(0);
}

// === 主函数 ===
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "用法: " << argv[0] << " <监听端口>" << std::endl;
        return 1;
    }

    // 注册信号处理
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    try {
        // 1. 配置系统环境
        SystemConfig::enable_ip_forward();
        
        // 2. 创建并配置TUN设备
        tun_device = std::make_unique<TunDevice>(TUN_DEV);
        SystemConfig::configure_tun(TUN_DEV, SERVER_TUN_IP, TUN_MASK);
        SystemConfig::setup_iptables_nat(TUN_NETWORK, PHYSICAL_NIC);

        // 3. 创建SSL上下文
        ssl::context ssl_ctx = create_ssl_context();

        // 4. 启动服务器
        asio::io_context io_context;
        server = std::make_unique<Server>(io_context, std::atoi(argv[1]), *tun_device, ssl_ctx);
        std::cout << "VPN服务器已启动，监听端口: " << argv[1] << std::endl;

        // 5. 运行IO服务
        const size_t thread_count = std::thread::hardware_concurrency() > 0 
                                  ? std::thread::hardware_concurrency() : 2;
        std::vector<std::thread> threads;
        
        for (size_t i = 0; i < thread_count; ++i) {
            threads.emplace_back([&io_context]() { 
                try {
                    io_context.run(); 
                } catch (const std::exception& e) {
                    std::cerr << "IO上下文异常: " << e.what() << std::endl;
                }
            });
        }
        
        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }
    } catch (std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}