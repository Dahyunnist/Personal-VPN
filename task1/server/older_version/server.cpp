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
#include <cstdlib>
#include <arpa/inet.h>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <unordered_set>
#include <mutex>
#include <nlohmann/json.hpp>
/*compile command:
g++ server.cpp -static -o server -lboost_system -lboost_thread -lpthread -lssl -lcrypto
*/

namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

// === 配置参数 ===
const std::string TUN_DEV = "tun0";
const std::string SERVER_TUN_IP = "10.8.0.1";
const std::string TUN_MASK = "24";
const std::string TUN_NETWORK = "10.8.0.0/24";
const std::string PHYSICAL_NIC = "ens33";
const size_t BUFFER_SIZE = 4096;
const std::string CERTIFICATE_PATH = "certs/server.crt";
const std::string PRIVATE_KEY_PATH = "certs/server.key";
const size_t CLIENT_IP_LENGTH = 16;

const std::string CLIENT_CONFIG_DIR = "client_config";
const std::string CLIENT_CRT_PATH = CLIENT_CONFIG_DIR + "/client.crt";
const std::string CLIENT_KEY_PATH = CLIENT_CONFIG_DIR + "/client.key";
const std::string CLIENT_PFX_PATH = CLIENT_CONFIG_DIR + "/client.pfx";
const std::string CONFIG_TXT_PATH = CLIENT_CONFIG_DIR + "/client.txt";
const std::string PFX_PASSWORD = "123456";

const std::string IP_POOL_START = "10.8.0.2";
const std::string IP_POOL_END = "10.8.0.254";
unsigned short port = 0;

uint32_t ip_to_uint(const std::string& ip_str)
{
    in_addr addr;
    inet_pton(AF_INET, ip_str.c_str(), &addr);
    return ntohl(addr.s_addr);
}

std::string uint_to_ip(uint32_t ip)
{
    in_addr addr;
    addr.s_addr = htonl(ip);
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr, buf, INET_ADDRSTRLEN);
    return buf;
}

std::string get_vm_ip()
{
    int fd;
    struct ifreq ifr;
    const char* iface = PHYSICAL_NIC.c_str();    // 使用配置中的物理网卡名称

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);

    if (ioctl(fd, SIOCGIFADDR, &ifr) == -1)
    {
        close(fd);
        throw std::runtime_error("无法获取网卡 " + std::string(iface) + " 的IP地址");
    }

    close(fd);
    return inet_ntoa(((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr);
}

class IpPoolManager
{
   public:
    IpPoolManager(const std::string& start, const std::string& end) : start_ip(start), end_ip(end) {}

    void mark_ip_used(const std::string& ip)
    {
        std::lock_guard<std::mutex> lock(ip_mutex);
        used_ips.insert(ip);
    }

    void release_ip(const std::string& ip)
    {
        std::lock_guard<std::mutex> lock(ip_mutex);
        used_ips.erase(ip);
    }

    std::string get_available_ip()
    {
        std::lock_guard<std::mutex> lock(ip_mutex);
        uint32_t start = ip_to_uint(start_ip);
        uint32_t end = ip_to_uint(end_ip);
        for (uint32_t ip = start; ip <= end; ip++)
        {
            std::string ip_str = uint_to_ip(ip);
            if (used_ips.find(ip_str) == used_ips.end())
            {
                return ip_str;
            }
        }
        throw std::runtime_error("No available IP in pool");
    }

    void generate_config_file()
    {
        try
        {
            std::string available_ip = get_available_ip();
            auto read_file = [](const std::string& path)
            {
                std::ifstream file(path);
                if (!file)
                {
                    throw std::runtime_error("Cannot open file: " + path);
                }
                return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            };
            json config = {{"server", {{"ip", get_vm_ip()}, {"port", port}}},
                           {"tun", {{"ip", available_ip}, {"mask", TUN_MASK}}},
                           {"certs", {{"client_crt", read_file(CLIENT_CRT_PATH)}, {"client_key", read_file(CLIENT_KEY_PATH)}, {"server_crt", read_file(CERTIFICATE_PATH)}}}};

            std::ofstream out("config.json");
            out << config.dump(4);
            std::cout << "Generated config.json with IP: " << available_ip << std::endl;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Failed to generate config: " << e.what() << std::endl;
        }
    }

    size_t get_available_ip_size()
    {
        size_t size = 252 - used_ips.size();
        return size;
    }

   private:
    std::unordered_set<std::string> used_ips;
    std::mutex ip_mutex;
    const std::string start_ip;
    const std::string end_ip;
};

IpPoolManager ip_pool(IP_POOL_START, IP_POOL_END);

std::atomic<bool> running(true);
std::mutex cmd_mutex;

void command_handle()
{
    std::string cmd;
    while (running)
    {
        std::unique_lock<std::mutex> lock(cmd_mutex);
        std::getline(std::cin, cmd);
        lock.unlock();

        if (cmd == "genconfig")
        {
            std::lock_guard<std::mutex> ip_lock(cmd_mutex);
            ip_pool.generate_config_file();
        }
        else if (cmd == "quit")
        {
            running = false;
            break;
        }
    }
}

// === TUN设备管理类 ===
class TunDevice
{
   public:
    TunDevice(const std::string& dev_name) : fd_(-1)
    {
        if ((fd_ = open("/dev/net/tun", O_RDWR)) < 0)
        {
            throw std::runtime_error("Failed to open /dev/net/tun: " + std::string(strerror(errno)));
        }

        struct ifreq ifr;
        std::memset(&ifr, 0, sizeof(ifr));
        ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
        std::strncpy(ifr.ifr_name, dev_name.c_str(), IFNAMSIZ - 1);

        if (ioctl(fd_, TUNSETIFF, reinterpret_cast<void*>(&ifr)) < 0)
        {
            close(fd_);
            throw std::runtime_error("Failed to ioctl TUNSETIFF: " + std::string(strerror(errno)));
        }
        std::cout << "TUN设备创建成功: " << ifr.ifr_name << " (fd: " << fd_ << ")" << std::endl;
    }

    ~TunDevice()
    {
        if (fd_ != -1)
        {
            close(fd_);
            std::cout << "TUN设备关闭" << std::endl;
        }
    }

    ssize_t read(uint8_t* buffer, size_t size) { return ::read(fd_, buffer, size); }

    ssize_t write(const uint8_t* raw_data, size_t raw_size)
    {
        ssize_t written = ::write(fd_, raw_data, raw_size);
        if (written < 0)
        {
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

struct ipv4_header
{
    uint8_t version : 4;
    uint8_t ihl : 4;
    uint8_t tos;
    uint16_t total_length;
    uint16_t id;
    uint16_t frag_off;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dest_ip;
};

class Server;

// === 会话管理类 ===
class Session : public std::enable_shared_from_this<Session>
{
   public:
    Session(ssl::stream<tcp::socket> socket, TunDevice& tun, Server& server);
    void start();
    void stop();
    bool is_active() const;
    // 写入数据到VPN隧道（发送给客户端）
    void async_write(const uint8_t* data, size_t size);
    const std::string& client_ip() const;

   private:
    // TLS握手
    void do_handshake();
    void handle_assigned_ip(const std::string& assigned_ip);
    // 从VPN隧道读取数据（客户端发来的）
    void start_reading();

    ssl::stream<tcp::socket> socket_;    // SSL加密流
    TunDevice& tun_;
    std::array<uint8_t, BUFFER_SIZE> buffer_;
    std::atomic<bool> active_;
    class Server& server_;
    std::string client_ip_;
    friend class Server;
};

// === 服务器类 ===
class Server
{
   public:
    Server(asio::io_context& io_context, short port, TunDevice& tun, ssl::context& ssl_ctx)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)), tun_(tun), ssl_ctx_(ssl_ctx), io_context_(io_context)
    {
        // init_ip_pool();
        // 启动TUN读取线程（处理来自互联网的响应）
        tun_to_client_thread_ = std::thread(&Server::tun_read_loop, this);
        // 开始接受客户端连接
        start_accept();
    }

    ~Server()
    {
        // 停止所有会话
        for (auto& session : sessions_)
        {
            session->stop();
        }
        sessions_.clear();

        // 停止TUN读取线程
        running_ = false;
        if (tun_to_client_thread_.joinable())
        {
            tun_to_client_thread_.join();
        }
        for (auto& session : sessions_)
        {
            session->stop();
        }
    }

    void assign_client_ip(std::shared_ptr<Session> session)
    {
        asio::post(io_context_,
                   [this, session]()
                   {
                       try
                       {
                           std::array<char, 16> ip_buf;
                           boost::system::error_code ec;

                           size_t length = boost::asio::read(session->socket_, boost::asio::buffer(ip_buf), boost::asio::transfer_at_least(1), ec);
                           std::string client_ip(ip_buf.data(), length);
                           client_ip.erase(std::remove(client_ip.begin(), client_ip.end(), '\n'), client_ip.end());
                           std::cout << "Received client's tun ip: " << client_ip << std::endl;
                           struct sockaddr_in sa;
                           if (inet_pton(AF_INET, client_ip.c_str(), &(sa.sin_addr)) != 1)
                           {
                               throw std::runtime_error("Invalid IP address format: " + client_ip);
                           }

                           ip_pool.mark_ip_used(client_ip);
                           {
                               std::lock_guard<std::mutex> lock_map(session_mutex_);
                               ip_to_session_[client_ip] = session;
                               sessions_.push_back(session);
                           }
                           session->handle_assigned_ip(client_ip);
                       }
                       catch (const std::exception& e)
                       {
                           std::cerr << "IP分配失败: " << e.what() << std::endl;
                           session->stop();
                       }
                   });
    }

    void release_client_ip(std::shared_ptr<Session> session)
    {
        asio::post(io_context_,
                   [this, session]()
                   {
                       std::lock_guard<std::mutex> lock(session_mutex_);
                       std::string client_ip = session->client_ip();
                       if (!client_ip.empty())
                       {
                           ip_to_session_.erase(client_ip);
                           ip_pool.release_ip(client_ip);
                           std::cout << "回收IP: " << client_ip << " （剩余可用：" << ip_pool.get_available_ip_size() << "）" << std::endl;
                       }
                       auto it = std::remove(sessions_.begin(), sessions_.end(), session);
                       if (it != sessions_.end())
                       {
                           sessions_.erase(it);
                       }
                   });
    }

   private:
    // 从TUN设备读取数据（来自互联网的响应）
    void tun_read_loop()
    {
        std::vector<uint8_t> buf(BUFFER_SIZE);
        running_ = true;

        std::cout << "=== TUN读取线程启动 ===" << std::endl;

        while (running_)
        {
            // 从TUN设备读取数据
            ssize_t n = tun_.read(buf.data(), buf.size());
            if (n <= 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                std::cerr << "TUN读取错误: " << strerror(errno) << std::endl;
                break;
            }
            if (n < sizeof(ipv4_header))
            {
                std::cerr << "TUN数据包过小，无法解析IPv4头部" << std::endl;
                continue;
            }
            ipv4_header* ip_hdr = reinterpret_cast<ipv4_header*>(buf.data());
            std::string dest_ip = uint_to_ip(ntohl(ip_hdr->dest_ip));

            asio::post(io_context_,
                       [this, buf, n, dest_ip]()
                       {
                           std::lock_guard<std::mutex> lock(session_mutex_);
                           auto it = ip_to_session_.find(dest_ip);
                           if (it != ip_to_session_.end() && it->second->is_active())
                           {
                               it->second->async_write(buf.data(), n);
                               std::cout << "TUN -> 客户端(" << dest_ip << "): " << n << "字节" << std::endl;
                           }
                           else
                           {
                               std::cerr << "目标IP未分配客户端：" << dest_ip << " (无法转发)" << std::endl;
                           }
                       });
        }

        std::cout << "TUN读取线程停止" << std::endl;
    }

    // 接受新客户端连接
    void start_accept()
    {
        auto socket = std::make_shared<tcp::socket>(io_context_);
        acceptor_.async_accept(*socket,
                               [this, socket](boost::system::error_code ec)
                               {
                                   if (!ec)
                                   {
                                       // 创建SSL流并启动会话
                                       auto ssl_stream = std::make_shared<ssl::stream<tcp::socket>>(std::move(*socket), ssl_ctx_);
                                       auto session = std::make_shared<Session>(std::move(*ssl_stream), tun_, *this);
                                       sessions_.push_back(session);
                                       session->start();
                                       std::cout << "新客户端连接" << std::endl;
                                   }
                                   else
                                   {
                                       std::cerr << "接受连接失败: " << ec.message() << std::endl;
                                   }
                                   start_accept();    // 继续接受连接
                               });
    }

    tcp::acceptor acceptor_;
    TunDevice& tun_;
    ssl::context& ssl_ctx_;
    asio::io_context& io_context_;
    std::vector<std::shared_ptr<Session>> sessions_;    // 客户端会话列表
    std::thread tun_to_client_thread_;
    std::atomic<bool> running_{false};
    // ip pool and session map
    std::unordered_map<std::string, std::shared_ptr<Session>> ip_to_session_;
    std::mutex session_mutex_;
};

Session::Session(ssl::stream<tcp::socket> socket, TunDevice& tun, Server& server) : socket_(std::move(socket)), tun_(tun), server_(server), active_(true) {}

void Session::start() { do_handshake(); }

void Session::stop()
{
    if (active_)
    {
        active_ = false;
        boost::system::error_code ec;
        socket_.lowest_layer().close(ec);
        server_.release_client_ip(shared_from_this());
        std::cout << "会话关闭" << std::endl;
    }
}

bool Session::is_active() const { return active_; }

// 写入数据到VPN隧道（发送给客户端）
void Session::async_write(const uint8_t* data, size_t size)
{
    if (!active_)
        return;

    auto self(shared_from_this());
    asio::async_write(socket_,
                      asio::buffer(data, size),
                      [this, self](boost::system::error_code ec, std::size_t /*length*/)
                      {
                          if (ec)
                          {
                              std::cerr << "发送失败: " << ec.message() << std::endl;
                              stop();
                          }
                      });
}

const std::string& Session::client_ip() const { return client_ip_; }

// TLS握手
void Session::do_handshake()
{
    auto self(shared_from_this());
    socket_.async_handshake(ssl::stream_base::server,
                            [this, self](const boost::system::error_code& ec)
                            {
                                if (!ec)
                                {
                                    std::cout << "客户端已连接: " << socket_.lowest_layer().remote_endpoint() << std::endl;
                                    server_.assign_client_ip(shared_from_this());
                                }
                                else
                                {
                                    std::cerr << "TLS握手失败: " << ec.message() << std::endl;
                                    active_ = false;
                                }
                            });
}

void Session::handle_assigned_ip(const std::string& assigned_ip)
{
    client_ip_ = assigned_ip;
    std::cout << "客户端分配到IP: " << client_ip_ << std::endl;
    start_reading();
}

// 从VPN隧道读取数据（客户端发来的）
void Session::start_reading()
{
    if (!active_)
        return;

    auto self(shared_from_this());
    socket_.async_read_some(asio::buffer(buffer_),
                            [this, self](boost::system::error_code ec, std::size_t length)
                            {
                                if (!ec && active_)
                                {
                                    // 将客户端发来的数据写入TUN设备（发送到互联网）
                                    ssize_t n = tun_.write(buffer_.data(), length);
                                    if (n != static_cast<ssize_t>(length))
                                    {
                                        std::cerr << "TUN写入失败: 预期" << length << "字节，实际" << n << "字节" << std::endl;
                                    }
                                    else
                                    {
                                        std::cout << "客户端 -> TUN: " << length << "字节" << std::endl;
                                    }
                                    // 继续读取
                                    start_reading();
                                }
                                else
                                {
                                    if (ec)
                                    {
                                        std::cerr << "客户端断开连接: " << ec.message() << std::endl;
                                    }
                                    stop();
                                }
                            });
}

// === 系统配置工具类 ===
class SystemConfig
{
   public:
    static void run_command(const std::string& cmd)
    {
        std::cout << "执行命令: " << cmd << std::endl;
        int ret = std::system(cmd.c_str());
        if (ret != 0)
        {
            std::cerr << "命令执行失败: " << cmd << " (返回值: " << ret << ")" << std::endl;
        }
        else
        {
            std::cout << "命令执行成功" << std::endl;
        }
    }

    static void configure_tun(const std::string& dev, const std::string& ip, const std::string& mask)
    {
        run_command("ip link set dev " + dev + " down");
        run_command("ip addr flush dev " + dev);
        run_command("ip addr add " + ip + "/" + mask + " dev " + dev);
        run_command("ip link set dev " + dev + " up");
        run_command("ip route add 10.8.0.0/24 dev " + dev + " proto kernel scope link src " + ip);
        std::cout << "TUN设备配置完成: " << dev << " " << ip << "/" << mask << std::endl;
    }

    static void enable_ip_forward()
    {
        run_command("sysctl -w net.ipv4.ip_forward=1");
        std::cout << "IP转发已启用" << std::endl;
    }

    static void setup_iptables_nat(const std::string& network, const std::string& nic)
    {
        run_command("iptables -t nat -F");
        run_command("iptables -t nat -A POSTROUTING -s " + network + " -o " + nic + " -j MASQUERADE");
        run_command("iptables -A FORWARD -i " + TUN_DEV + " -o " + nic + " -j ACCEPT");
        run_command("iptables -A FORWARD -i " + nic + " -o " + TUN_DEV + " -m state --state RELATED,ESTABLISHED -j ACCEPT");
        std::cout << "iptables NAT规则配置完成" << std::endl;
    }
};

// === SSL上下文配置函数 ===
ssl::context create_ssl_context()
{
    ssl::context ctx(ssl::context::tls_server);

    try
    {
        ctx.use_certificate_chain_file(CERTIFICATE_PATH);
        ctx.use_private_key_file(PRIVATE_KEY_PATH, ssl::context::pem);
        ctx.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 | ssl::context::no_sslv3);
        std::cout << "SSL上下文配置成功" << std::endl;
    }
    catch (std::exception& e)
    {
        std::cerr << "SSL证书加载失败: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    return ctx;
}

// === 全局变量和信号处理 ===
std::unique_ptr<Server> server;
std::unique_ptr<TunDevice> tun_device;

void handle_signal(int signum)
{
    std::cout << "\n收到信号 " << signum << "，正在关闭服务器..." << std::endl;
    running = false;
    server.reset();    // 销毁服务器
    tun_device.reset();
    exit(0);
}

// === 主函数 ===
int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "用法: " << argv[0] << " <监听端口>" << std::endl;
        return 1;
    }
    port = static_cast<unsigned short>(std::stoi(argv[1]));

    // 注册信号处理
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    try
    {
        std::thread cmd_thread(command_handle);
        // cmd_thread.detach();

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
        std::cout << "VPN服务器已启动, 监听端口: " << argv[1] << std::endl;

        // 5. 运行IO服务
        const size_t thread_count = std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() : 2;
        std::vector<std::thread> threads;

        for (size_t i = 0; i < thread_count; ++i)
        {
            threads.emplace_back(
                [&io_context]()
                {
                    try
                    {
                        io_context.run();
                    }
                    catch (const std::exception& e)
                    {
                        std::cerr << "IO上下文异常: " << e.what() << std::endl;
                    }
                });
        }

        for (auto& t : threads)
        {
            if (t.joinable())
                t.join();
        }

        running = false;
        if (cmd_thread.joinable())
        {
            pthread_kill(cmd_thread.native_handle(), SIGUSR1);
            cmd_thread.join();
        }
    }
    catch (std::exception& e)
    {
        running = false;
        std::cerr << "异常: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}