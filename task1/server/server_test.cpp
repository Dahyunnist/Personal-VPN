#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/bind/bind.hpp>
#include <iomanip>

/*compile command:
g++ server_test.cpp -o server_test -lboost_system -lboost_thread -lpthread -lssl -lcrypto
*/

namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

class TestSession : public std::enable_shared_from_this<TestSession> {
private:
    ssl::stream<tcp::socket> ssl_socket_;
    std::array<char, 4096> buffer_;
    int packet_count_;

public:
    TestSession(asio::io_context& io_context, ssl::context& ssl_context)
        : ssl_socket_(io_context, ssl_context), packet_count_(0) {}

    ssl::stream<tcp::socket>::lowest_layer_type& socket() {
        return ssl_socket_.lowest_layer();
    }

    void start() {
        auto self = shared_from_this();
        ssl_socket_.async_handshake(ssl::stream_base::server,
            [this, self](boost::system::error_code ec) {
                if (!ec) {
                    std::cout << "✅ SSL握手成功，客户端已连接" << std::endl;
                    do_read();
                } else {
                    std::cerr << "❌ SSL握手失败: " << ec.message() << std::endl;
                }
            });
    }

private:
    void do_read() {
        auto self = shared_from_this();
        ssl_socket_.async_read_some(asio::buffer(buffer_),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec) {
                    handle_data(buffer_.data(), length);
                    do_read(); // 继续读取下一个包
                } else if (ec == asio::error::eof) {
                    std::cout << "🔌 客户端正常断开连接" << std::endl;
                } else {
                    std::cerr << "❌ 读取错误: " << ec.message() << std::endl;
                }
            });
    }

    void handle_data(const char* data, std::size_t length) {
        packet_count_++;
        std::cout << "\n📦 接收到数据包 #" << packet_count_ << std::endl;
        std::cout << "   大小: " << length << " 字节" << std::endl;
        
        // 打印数据包的十六进制内容（前64字节）
        std::cout << "   内容: ";
        size_t print_length = std::min(length, size_t(64));
        for (size_t i = 0; i < print_length; ++i) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') 
                      << (unsigned char)data[i] << " ";
            if ((i + 1) % 16 == 0) std::cout << "\n         ";
        }
        if (length > 64) {
            std::cout << "... (剩余 " << (length - 64) << " 字节)";
        }
        std::cout << std::dec << std::endl;

        // 如果是IP包，解析基本信息
        if (length >= 20 && (data[0] & 0xF0) == 0x40) { // IPv4
            analyze_ip_packet(data, length);
        }

        // 发送确认回复
        send_ack();
    }

    void analyze_ip_packet(const char* data, std::size_t length) {
        const unsigned char* ip_header = (const unsigned char*)data;
        
        int version = (ip_header[0] >> 4) & 0x0F;
        int header_length = (ip_header[0] & 0x0F) * 4;
        int total_length = (ip_header[2] << 8) | ip_header[3];
        int protocol = ip_header[9];
        
        std::cout << "   🌐 IP包分析:" << std::endl;
        std::cout << "      版本: IPv" << version << std::endl;
        std::cout << "      头部长度: " << header_length << " 字节" << std::endl;
        std::cout << "      总长度: " << total_length << " 字节" << std::endl;
        std::cout << "      协议: ";
        
        switch (protocol) {
            case 1: std::cout << "ICMP"; break;
            case 6: std::cout << "TCP"; break;
            case 17: std::cout << "UDP"; break;
            default: std::cout << "其他(" << protocol << ")"; break;
        }
        std::cout << std::endl;

        // 解析源和目标IP
        std::cout << "      源IP: " 
                  << (int)ip_header[12] << "." << (int)ip_header[13] << "."
                  << (int)ip_header[14] << "." << (int)ip_header[15] << std::endl;
        std::cout << "      目标IP: " 
                  << (int)ip_header[16] << "." << (int)ip_header[17] << "."
                  << (int)ip_header[18] << "." << (int)ip_header[19] << std::endl;
    }

    void send_ack() {
        auto self = shared_from_this();
        std::string ack_msg = "ACK_" + std::to_string(packet_count_);
        
        asio::async_write(ssl_socket_, asio::buffer(ack_msg),
            [this, self, ack_msg](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    std::cout << "   ✅ 已发送确认: " << ack_msg << std::endl;
                } else {
                    std::cerr << "   ❌ 发送确认失败: " << ec.message() << std::endl;
                }
            });
    }
};

class TestServer {
private:
    asio::io_context& io_context_;
    ssl::context ssl_context_;
    tcp::acceptor acceptor_;

public:
    TestServer(asio::io_context& io_context, int port)
        : io_context_(io_context)
        , ssl_context_(ssl::context::tlsv12_server)
        , acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        
        configure_ssl();
        start_accept();
    }

private:
    void configure_ssl() {
        ssl_context_.set_options(
            ssl::context::default_workarounds
            | ssl::context::no_sslv2
            | ssl::context::single_dh_use);

        try {
            ssl_context_.use_certificate_file("certs/server.crt", ssl::context::pem);
            ssl_context_.use_private_key_file("certs/server.key", ssl::context::pem);
            std::cout << "✅ SSL证书加载成功" << std::endl;
        } catch (std::exception& e) {
            std::cerr << "❌ SSL证书加载失败: " << e.what() << std::endl;
            std::cerr << "请确保证书文件存在: certs/server.crt 和 certs/server.key" << std::endl;
            throw;
        }
    }

    void start_accept() {
        auto new_session = std::make_shared<TestSession>(io_context_, ssl_context_);
        
        acceptor_.async_accept(new_session->socket(),
            [this, new_session](boost::system::error_code ec) {
                if (!ec) {
                    std::cout << "🔗 新客户端连接，开始SSL握手..." << std::endl;
                    new_session->start();
                } else {
                    std::cerr << "❌ 接受连接失败: " << ec.message() << std::endl;
                }
                
                start_accept(); // 继续接受新连接
            });
    }
};

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "用法: " << argv[0] << " <端口>" << std::endl;
            return 1;
        }

        int port = std::atoi(argv[1]);
        
        std::cout << "=== VPN数据传输测试服务端 ===" << std::endl;
        std::cout << "监听端口: " << port << std::endl;
        std::cout << "功能: 仅接收并分析客户端数据，不进行转发" << std::endl;
        std::cout << "===============================\n" << std::endl;

        asio::io_context io_context;
        TestServer server(io_context, port);

        std::cout << "🚀 测试服务器已启动，等待客户端连接..." << std::endl;
        io_context.run();

    } catch (std::exception& e) {
        std::cerr << "❌ 服务器错误: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}