#include <iostream>
#include <boost/asio.hpp>

using namespace boost::asio;
using namespace boost::asio::ip;
using namespace std;

// 处理单个客户端连接
void handle_session(tcp::socket skt) {
    try {
        size_t length = 0;
        while (true) {
            // 接收数据
            char buffer[1024];
            boost::system::error_code error;
            length += skt.read_some(boost::asio::buffer(buffer), error);
            if (error == error::eof) {
                std::cout << "Received: " << std::string(buffer, length) << "\n";
                // 连接关闭
                break;
            }
            else if (error) {
                // 其他错误
                throw boost::system::system_error(error);
            }
        }
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}

int main() {
    try {
        // 创建io_context
        io_context io;

        // 创建tcp acceptor，绑定到指定端口
        tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), 8888));

        std::cout << "Sync Server started, listening on port 8888..." << std::endl;

        while (true) {
            // 接受客户端连接
            tcp::socket skt(io);
            acceptor.accept(skt);
            std::cout << "New connection from: " << skt.remote_endpoint() << std::endl;

            // 处理客户端连接
            handle_session(std::move(skt));
        }
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    return 0;
}