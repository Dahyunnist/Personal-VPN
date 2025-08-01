#ifndef SERVER_H
#define SERVER_H


// #include <iostream>
// #include <string>
// #include <vector>
// #include <memory>

// #include <boost/asio.hpp>
// #include <boost/asio/ssl.hpp>
// #include <boost/bind/bind.hpp>
// #include <sys/types.h>
// #include <sys/stat.h>
// #include <fcntl.h>
// #include <sys/ioctl.h>
// #include <net/if.h>
// #include <linux/if_tun.h>
// #include <unistd.h>
// #include <cstring>
// #include <array>
// #include <atomic>
// #include <csignal>
// #include <cstdlib>
// #include <arpa/inet.h>
// #include <boost/asio.hpp>
// #include <boost/asio/ssl.hpp>

// #include <fcntl.h>
// #include <sys/ioctl.h>
// #include <net/if.h>
// #include <linux/if_tun.h>
// #include <unistd.h>
// #include <cstring>
// #include <fstream>
// #include <unordered_set>
// #include <mutex>
// #include <nlohmann/json.hpp>

#include "config.h"
#include "IpPoolManager.h"
#include "BasicFunc.h"
#include "TunDevice.h"
#include "SystemConfig.h"
#include "Session.h"
/*compile command:
g++ server.cpp -static -o server -lboost_system -lboost_thread -lpthread -lssl -lcrypto
*/

namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;


struct ipv4_header{
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

// === 服务器类 ===
class Server {
public:
    Server(asio::io_context& io_context, short port, TunDevice& tun, ssl::context& ssl_ctx);

    ~Server();

    void assign_client_ip(std::shared_ptr<Session> session);

    void release_client_ip(std::shared_ptr<Session> session);

private:
    // 从TUN设备读取数据（来自互联网的响应）
    void tun_read_loop();
    
    // 接受新客户端连接
    void start_accept();

    tcp::acceptor acceptor_;
    TunDevice& tun_;
    ssl::context& ssl_ctx_;
    asio::io_context& io_context_;
    std::vector<std::shared_ptr<Session>> sessions_;  // 客户端会话列表
    std::thread tun_to_client_thread_;
    std::atomic<bool> running_{false};
    // ip pool and session map
    std::unordered_map<std::string, std::shared_ptr<Session>> ip_to_session_;   
    std::mutex session_mutex_;
};

#endif