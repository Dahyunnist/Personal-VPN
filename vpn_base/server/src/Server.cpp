#include "../include/Server.h"
// #include <sys/stat.h>
#include <thread>
#include <iostream>
/*
g++ server.cpp -static -o server -lboost_system -lboost_thread -lpthread -lssl -lcrypto
*/

namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

Server::Server(asio::io_context& io_context, short port, TunDevice& tun, ssl::context& ssl_ctx)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)), tun_(tun), ssl_ctx_(ssl_ctx), io_context_(io_context)
{
    // init_ip_pool();
    // 启动TUN读取线程（处理来自互联网的响应）
    tun_to_client_thread_ = std::thread(&Server::tun_read_loop, this);
    // 开始接受客户端连接
    start_accept();
}

Server::~Server()
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

void Server::assign_client_ip(std::shared_ptr<Session> session)
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

                       vpn_global::ip_pool.mark_ip_used(client_ip);
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

void Server::release_client_ip(std::shared_ptr<Session> session)
{
    asio::post(io_context_,
               [this, session]()
               {
                   std::lock_guard<std::mutex> lock(session_mutex_);
                   std::string client_ip = session->client_ip();
                   if (!client_ip.empty())
                   {
                       ip_to_session_.erase(client_ip);
                       vpn_global::ip_pool.release_ip(client_ip);
                       std::cout << "回收IP: " << client_ip << " （剩余可用：" << vpn_global::ip_pool.get_available_ip_size() << "）" << std::endl;
                   }
                   auto it = std::remove(sessions_.begin(), sessions_.end(), session);
                   if (it != sessions_.end())
                   {
                       sessions_.erase(it);
                   }
               });
}

// 从TUN设备读取数据（来自互联网的响应）
void Server::tun_read_loop()
{
    std::vector<uint8_t> buf(vpn_config::BUFFER_SIZE);
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
        if (n < 0)
        {
            std::cerr << "TUN设备读取失败：" << strerror(errno) << std::endl;
            break;
        }
        if (static_cast<size_t>(n) < sizeof(ipv4_header))
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
void Server::start_accept()
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