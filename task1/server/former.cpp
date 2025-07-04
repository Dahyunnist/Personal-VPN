// #include <iostream>
// #include <string.h>
// #include <map>
// #include <vector>
// #include <sys/socket.h>
// #include <arpa/inet.h>
// #include <unistd.h>
// #include <sys/select.h>


// using namespace std;
// // 编译：g++ former.cpp -o former -Wall

// // 1. create a socket
// // 2. bind IP and port
// // 3. listen to the port
// // 4. accept a client connection
// // 5. receive data from the client
// // 6. send data to the client
// // 7. close the socket

// const int BUFFER_SIZE = 4096;
// const int PORT = 8888;
// const int MAX_CLIENTS = 10;


// struct ClientInfo{
//     int socket;
//     string name;
//     string ip;
//     uint16_t port;
// };

// map<int, ClientInfo> client_info;

// void show_client_info(const ClientInfo &info){
//     cout << "   new client info:" << endl;
//     cout << "   socket: " << info.socket << endl;
//     cout << "   name: " << info.name << endl;
//     cout << "   ip: " << info.ip << endl;
//     cout << "   port: " << info.port << endl;
// }



// int main(){
//     // create server socket
//     int server_socket = socket(AF_INET, SOCK_STREAM, 0);
//         // AF_INET: use IPv4 address
//         // SOCK_STREAM: communicate with TCP protocol
//         // explicitly specify TCP protocol
//     if(server_socket == -1){ 
//         cout << "socket creation failed" << endl;
//         return 1; //return 0 if socket creation fails
//     }

//     // set SO_REUSEADDR option
//     int opt = 1;
//     if(setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1){
//         cerr << "[ERROR] setsockopt failed" << endl;
//         close(server_socket);
//         return 1;
//     }

//     // bind IP and port
//     sockaddr_in _myaddr = {}; //initialize a struct that stores addr type, port number and IP addr
//     _myaddr.sin_family = AF_INET; //use IPv4 address
//     _myaddr.sin_port = htons(PORT); //use port 8888
//     _myaddr.sin_addr.s_addr = INADDR_ANY; //listen to any IP address
//         // inet_addr(): convert string IP to binary format
//         // S_un.S_addr: use the S_un union to access the address field(specific to Windows)
//     if(bind(server_socket, (sockaddr*)&_myaddr, sizeof(sockaddr_in)) == -1){
//         cout << "bind failed" << endl;
//         close(server_socket);
//         return 1;
//     }

//     // listen to the port
//     if(listen(server_socket, 5) == -1){
//         cout << "listen failed" << endl;
//         close(server_socket);
//         return 1;
//     }

//     // // allow client to connect
//     // sockaddr_in _clientAddr = {}; //initialize a struct to store client addr
//     // socklen_t _addr_len = sizeof(sockaddr_in); //get the length of addr struct
//     // int client_socket = -1; //store client socket temporarily
//     // char _buf[256] = {}; //create buffer to store data from client

//     // initialize select variables
//     fd_set read_fds; //a bitmap
//     int client_sockets[MAX_CLIENTS] = {0};
//     int max_sd = server_socket;


//     // main event
//     while(true){
//         FD_ZERO(&read_fds); //clear the set
//         FD_SET(server_socket, &read_fds);

//         // add all client sockets to the set
//         for(int i = 0; i < MAX_CLIENTS; i++){
//             if(client_sockets[i] > 0){
//                 FD_SET(client_sockets[i], &read_fds);
//                 if(client_sockets[i] > max_sd){
//                     max_sd = client_sockets[i];
//                 }
//             }
//         }

//         // use select function to wait for events
//         if(select(max_sd+1, &read_fds, nullptr, nullptr, nullptr) < 0){
//             cerr << "[ERROR] select error" << endl;
//             continue;
//         }

//         // deal with new connections
//         if(FD_ISSET(server_socket, &read_fds)){
//             sockaddr_in client_addr = {};
//             socklen_t client_addr_len = sizeof(client_addr);
//             int new_socket = accept(server_socket, (sockaddr*)&client_addr, &client_addr_len);
//             if(new_socket == -1){
//                 cerr << "[ERROR] Accept failed" << endl;
//                 continue;
//             }
//             ClientInfo info;
//             info.socket = new_socket;
//             info.name = "Client_" + to_string(new_socket);
//             info.port = ntohs(client_addr.sin_port);
//             info.ip = inet_ntoa(client_addr.sin_addr);
//             client_info[new_socket] = info;

//             cout << "[INFO] New Client connected: " << inet_ntoa(client_addr.sin_addr) << endl;
//             show_client_info(info);

//             for(int i = 0; i < MAX_CLIENTS; i++){
//                 if(client_sockets[i] == 0){
//                     client_sockets[i] = new_socket;
//                     break;
//                 }
//             }
//         }

//         // deal with client data
//         for(int i = 0; i < MAX_CLIENTS; i++){
//             int client_socket = client_sockets[i];
//             if(client_socket > 0 && FD_ISSET(client_socket, &read_fds)){
//                 char buf[BUFFER_SIZE] = {};
//                 memset(buf, 0, sizeof(buf));

//                 int recv_len = recv(client_socket, buf, BUFFER_SIZE, 0);
//                 if(recv_len <= 0){
//                     if(recv_len < 0){
//                         cout << "[INFO] Data receive failed" << endl;
//                     }
//                     else{
//                         cout << "[INFO] Client " << client_info[client_socket].name << " connection closed" << endl;
//                     }
                    
//                     close(client_socket);
//                     client_sockets[i] = 0;
//                 }
//                 else{
//                     cout << "[DATA] Received from client " << client_info[client_socket].name << " : " << buf << endl;
//                     if(strcmp(buf, "closesocket") == 0){
//                         cout << "[INFO] Closing client socket as " << client_info[client_socket].name << " requested..." << endl;
//                         close(client_socket);
//                         client_sockets[i] = 0;
//                     }
//                     else{
//                         send(client_socket, buf, recv_len, 0);
//                         cout << "[DATA] Sent to client " << client_info[client_socket].name << ": " << buf << endl;
//                     }
//                 }
//             }
//         }
        
//     }


//     // close the server socket
//     close(server_socket);
//     return 0;
// }

// 核心头文件
#include <iostream>
#include <vector>
#include <memory>  // 智能指针
#include <ctime>   // 时间处理
#include <boost/asio.hpp>         // Boost 异步I/O核心
#include <boost/asio/ssl.hpp>     // SSL支持
#include <boost/beast/core.hpp>   // 提供高性能缓冲区等工具
#include <boost/beast/ssl.hpp>    // Beast对SSL的增强
#include <openssl/ssl.h>          // OpenSSL原生API支持

// 命名空间简化
namespace asio = boost::asio;     // 网络操作核心
namespace ssl = asio::ssl;       // SSL子模块
namespace beast = boost::beast;  // 网络工具库
using tcp = asio::ip::tcp;       // TCP协议相关类型

// 常量定义
const int PORT = 8888;           // 监听端口
const char* CERT_FILE = "certs/server.crt";  // 证书路径
const char* KEY_FILE = "certs/server.key";   // 私钥路径
const int BUFFER_SIZE = 4096;    // 读写缓冲区大小
const int IDLE_TIMEOUT = 300;    // 客户端空闲超时(秒)

/*
 * TLS客户端连接类
 * 封装SSL连接和客户端信息
 */
class TLSClient {
public:
    // 定义SSL流类型（基于TCP socket）
    using ssl_socket = beast::ssl_stream<tcp::socket>;

    // 构造函数：接管已建立的TCP连接
    TLSClient(tcp::socket socket, ssl::context& ctx)
        : socket_(std::move(socket), ctx),      // 初始化SSL流
          last_active_(std::time(nullptr)) {   // 记录连接时间
        // 获取客户端端点信息
        endpoint_ = socket_.next_layer().remote_endpoint();
    }

    // 访问器方法
    ssl_socket& socket() { return socket_; }
    std::string ip() const { return endpoint_.address().to_string(); }
    uint16_t port() const { return endpoint_.port(); }
    time_t last_active() const { return last_active_; }
    void update_activity() { last_active_ = std::time(nullptr); }

private:
    ssl_socket socket_;        // SSL/TLS加密流
    tcp::endpoint endpoint_;   // 客户端地址+端口
    time_t last_active_;       // 最后活动时间戳
};

/*
 * TLS服务器主类
 * 管理连接接收和数据处理
 */
class TLSServer {
public:
    // 构造函数：初始化监听器
    TLSServer(asio::io_context& io, ssl::context& ctx)
        : acceptor_(io, tcp::endpoint(tcp::v4(), PORT)),  // 创建IPv4监听器
          ctx_(ctx) {                                    // 保存SSL上下文引用
        std::cout << "[TCP] Server started, listening on port: " 
                  << PORT << std::endl;
    }

    // 开始接受新连接（异步）
    void start_accept() {
        // 异步接受连接（非阻塞）
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    // 创建客户端对象（移动语义转移socket所有权）
                    auto client = std::make_shared<TLSClient>(
                        std::move(socket), ctx_);
                    clients_.push_back(client);  // 加入连接列表
                    start_handshake(client);      // 开始SSL握手
                } else {
                    std::cerr << "[ERROR] Accept failed: " 
                              << ec.message() << std::endl;
                }
                // 继续接受下一个连接（尾递归）
                start_accept();
            });
    }

private:
    // SSL握手阶段
    void start_handshake(std::shared_ptr<TLSClient> client) {
        client->socket().async_handshake(
            ssl::stream_base::server,  // 服务器模式握手
            [this, client](boost::system::error_code ec) {
                if (!ec) {
                    std::cout << "[SSL] New client connected: " 
                              << client->ip() << ":" << client->port() 
                              << std::endl;
                    start_read(client);  // 握手成功，开始读数据
                } else {
                    std::cerr << "[SSL] Handshake failed with " 
                              << client->ip() << ": " << ec.message() 
                              << std::endl;
                    remove_client(client);  // 握手失败则移除
                }
            });
    }

    // 异步读取客户端数据
    void start_read(std::shared_ptr<TLSClient> client) {
        // 使用shared_ptr保证buffer生命周期
        auto buffer = std::make_shared<beast::flat_buffer>();
        
        // 异步读取（非阻塞）
        client->socket().async_read_some(
            buffer->prepare(BUFFER_SIZE),  // 准备缓冲区
            [this, client, buffer](boost::system::error_code ec, 
                                  size_t bytes_transferred) {
                if (!ec) {
                    buffer->commit(bytes_transferred);  // 确认读取数据
                    process_data(client, buffer);       // 处理数据
                    start_read(client);                 // 继续读取
                } 
                // 连接关闭或错误处理
                else if (ec == ssl::error::stream_truncated || 
                       ec == beast::error::timeout) {
                    std::cout << "[DISCONNECT] Client disconnected: " 
                              << client->ip() << ":" << client->port() 
                              << std::endl;
                    remove_client(client);
                } else {
                    std::cerr << "[ERROR] Read error from " 
                              << client->ip() << ": " << ec.message() 
                              << std::endl;
                    remove_client(client);
                }
            });
    }

    // 处理接收到的数据
    void process_data(std::shared_ptr<TLSClient> client, 
                     std::shared_ptr<beast::flat_buffer> buffer) {
        client->update_activity();  // 更新活动时间
        
        // 转换缓冲区数据为字符串
        std::string data(beast::buffers_to_string(buffer->data()));
        std::cout << "[DATA] Received from client " 
                  << client->ip() << ":" << client->port() << "\n"
                  << "   Length: " << data.size() << "\n"
                  << "   Content: " << data << std::endl;

        // 异步回显数据
        async_write(client->socket(), buffer->data(),
            [this, client](boost::system::error_code ec, size_t) {
                if (ec) {
                    std::cerr << "[ERROR] Write error to " 
                              << client->ip() << ": " << ec.message() 
                              << std::endl;
                    remove_client(client);
                }
            });
        
        buffer->consume(buffer->size());  // 清空缓冲区
    }

    // 移除客户端连接
    void remove_client(std::shared_ptr<TLSClient> client) {
        // 安全关闭连接（忽略错误）
        boost::system::error_code ec;
        client->socket().shutdown(ec);  // 优雅关闭SSL
        client->socket().next_layer().close(ec);  // 关闭底层socket
        
        // 从活动列表中移除
        clients_.erase(
            std::remove_if(clients_.begin(), clients_.end(),
                [&client](const std::shared_ptr<TLSClient>& c) {
                    return c == client;
                }),
            clients_.end());
    }

    // 检查空闲超时连接
    void check_timeouts() {
        auto now = std::time(nullptr);
        for (auto it = clients_.begin(); it != clients_.end(); ) {
            if (now - (*it)->last_active() > IDLE_TIMEOUT) {
                std::cout << "[TIMEOUT] Clean idle client: " 
                          << (*it)->ip() << ":" << (*it)->port() 
                          << std::endl;
                remove_client(*it);  // 超时移除
            } else {
                ++it;
            }
        }
    }

    // 成员变量
    tcp::acceptor acceptor_;      // 接收器（监听新连接）
    ssl::context& ctx_;           // SSL配置上下文（引用）
    std::vector<std::shared_ptr<TLSClient>> clients_;  // 活动连接列表
};

/*
 * 初始化SSL上下文
 * 配置证书、私钥和加密参数
 */
bool init_ssl_context(ssl::context& ctx) {
    try {
        // 设置SSL选项
        ctx.set_options(
            ssl::context::default_workarounds |  // 兼容性选项
            ssl::context::no_sslv2 |            // 禁用不安全的SSLv2
            ssl::context::no_sslv3 |            // 禁用SSLv3
            ssl::context::no_tlsv1 |            // 禁用TLS1.0
            ssl::context::no_tlsv1_1 |          // 禁用TLS1.1
            ssl::context::single_dh_use);        // 增强安全性

        // 加载证书链和私钥
        ctx.use_certificate_chain_file(CERT_FILE);
        ctx.use_private_key_file(KEY_FILE, ssl::context::pem);
        
        // 密码回调（示例中为固定值，生产环境应自定义）
        ctx.set_password_callback([](size_t, ssl::context::password_purpose) {
            return "test";
        });

        // 设置加密套件（通过原生OpenSSL API）
        SSL_CTX_set_cipher_list(ctx.native_handle(), 
            "HIGH:!aNULL:!MD5:!RC4:!3DES:!DES:!DSS:!PSK:!SRP:!CAMELLIA:!SEED");

        std::cout << "[SSL] Successfully initialized SSL context\n"
                  << "[SSL] Using certificate: " << CERT_FILE << "\n"
                  << "[SSL] Using private key: " << KEY_FILE << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[SSL] Initialization failed: " << e.what() << std::endl;
        return false;
    }
}

/*
 * 主函数
 * 初始化并启动服务器
 */
int main() {
    try {
        // 1. 创建I/O上下文（事件循环核心）
        asio::io_context io;
        
        // 2. 初始化SSL上下文
        ssl::context ctx(ssl::context::tls_server);
        if (!init_ssl_context(ctx)) {
            return EXIT_FAILURE;
        }

        // 3. 创建服务器实例
        TLSServer server(io, ctx);
        server.start_accept();  // 开始接受连接

        // 4. 可选：添加定时器检查空闲连接
        asio::steady_timer timer(io);
        std::function<void()> check_timeouts = [&]() {
            // 实际项目中应将check_timeouts()设为public方法
            timer.expires_after(std::chrono::seconds(10));
            timer.async_wait([&](boost::system::error_code) { 
                check_timeouts(); 
            });
        };
        check_timeouts();

        // 5. 启动事件循环（阻塞直到所有操作完成）
        io.run();
    } catch (const std::exception& e) {
        std::cerr << "[SYSTEM] Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}