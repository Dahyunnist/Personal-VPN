#include "../include/Session.h"
#include <iostream>
#include <../include/Server.h>

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