#include <iostream>
#include <vector>
// #include <string.h>
// #include <map>
// #include <sys/socket.h>
// #include <arpa/inet.h>
// #include <unistd.h>
// #include <sys/select.h>
// #include <openssl/ssl.h>
// #include <openssl/err.h>
// #include <time.h>
#include <memory>
#include <ctime>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>

namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;
// 编译：g++ -std=c++17 server.cpp -o server -lssl -lcrypto -lpthread -I/usr/include/boost -L/usr/lib/x86_64-linux-gnu

// 1. create a socket
// 2. bind IP and port
// 3. listen to the port
// 4. accept a client connection
// 5. receive data from the client
// 6. send data to the client
// 7. close the socket

const int BUFFER_SIZE = 4096;
const int PORT = 8888;
const int MAX_CLIENTS = 10;
const char* CERT_FILE = "certs/server.crt";
const char* KEY_FILE = "certs/server.key";
const int IDLE_TIMEOUT = 300;
// SSL_CTX_use_certificate_file(ctx, "/home/username/server/certs/server.crt", SSL_FILETYPE_PEM);
// SSL_CTX_load_verify_locations(ctx, "C:/tasks/task1/client/server.crt", NULL);


class TLSClient{
public:
    using ssl_socket = beast::ssl_stream<tcp::socket>;

    TLSClient(tcp::socket socket, ssl::context& ctx) : socket_(std::move(socket), ctx), last_active_(std::time(nullptr)){
        endpoint_ = socket_.next_layer().remote_endpoint();
    }
    ssl_socket& socket(){
        return socket_;
    }
    std::string ip() const {
        return endpoint_.address().to_string();
    }
    uint16_t port() const {
        return endpoint_.port();
    }
    time_t last_active() const {
        return last_active_;
    }
    void update_activity(){
        last_active_ = std::time(nullptr);
    }

private:
    ssl_socket socket_; //need to combine TCP socket and SSL context: socket_(std::move(socket), ctx)
    tcp::endpoint endpoint_; //client addr + port
    time_t last_active_;
};



class TLSServer{
public:
    TLSServer(asio::io_context& io, ssl::context& ctx) : acceptor_(io, tcp::endpoint(tcp::v4(), PORT)), ctx_(ctx){
        std::cout << "[TCP] Server started, listening on port: " << PORT << std::endl;
    }
    void start_accept(){
        acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket){
            if(!ec){
                auto client = std::make_shared<TLSClient>(std::move(socket), ctx_);
                clients_.push_back(client);
                start_handshake(client);
            }
            else{
                std::cerr << "[ERROR] Accept failed: " << ec.message() << std::endl;
            }
            start_accept();
        });
    }
private:
    void start_handshake(std::shared_ptr<TLSClient> client){
        client->socket().async_handshake(ssl::stream_base::server, [this, client](boost::system::error_code ec){
            if(!ec){
                std::cout << "[SSL] New client connected: " << client->ip() << ":" << client->port() << std::endl;
                start_read(client);
            }
            else{
                std::cerr << "[SSL] Handshake failed with " << client->ip() << ": " << ec.message() << std::endl;
                remove_client(client);
            }
        });
    }

    void start_read(std::shared_ptr<TLSClient> client){
        auto buffer = std::make_shared<beast::flat_buffer>();

        client->socket().async_read_some(buffer->prepare(BUFFER_SIZE), [this, client, buffer](boost::system::error_code ec, size_t bytes_transferred){
            if(!ec){
                buffer->commit(bytes_transferred);
                process_data(client, buffer);
                start_read(client);
            }
            else if(ec == ssl::error::stream_truncated || ec == beast::error::timeout){
                std::cout << "[DISCONNECT] Client disconnected: " << client->ip() << ":" << client->port() << std::endl;
                remove_client(client);
            }
            else{
                std::cerr << "[ERROR] Read error from " << client->ip() << ": " << ec.message() << std::endl;
                remove_client(client);
            }
        });
    }

    void process_data(std::shared_ptr<TLSClient> client, std::shared_ptr<beast::flat_buffer> buffer){
        client->update_activity();

        std::string data(beast::buffers_to_string(buffer->data()));
        std::cout << "[DATA] Received from client " << client->ip() << ":" << client->port() << std::endl;
        std::cout << "  Length: " << data.size() << std::endl;
        std::cout << "  Content: " << data << std::endl;

        async_write(client->socket(), buffer->data(), [this, client](boost::system::error_code ec, size_t){
            if(ec){
                std::cerr << "[ERROR] Write error to " << client->ip() << ": " << ec.message() << std::endl;
                remove_client(client);
            }
        });
        buffer->consume(buffer->size());
    }

    void remove_client(std::shared_ptr<TLSClient> client){
        boost::system::error_code ec;
        client->socket().shutdown(ec);
        client->socket().next_layer().close(ec);

        clients_.erase(std::remove_if(clients_.begin(), clients_.end(), [&client](const std::shared_ptr<TLSClient>& c){
            return c == client;
            }), 
            clients_.end()
        );
    }

    void check_timeouts(){
        auto now = std::time(nullptr);
        for(auto it = clients_.begin(); it != clients_.end();){
            if(now - (*it)->last_active() > IDLE_TIMEOUT){
                std::cout << "[TIMEOUT] Clean idle client: " << (*it)->ip() << ":" << (*it)->port() << std::endl;
                remove_client(*it);
                continue;
            }
            ++it;
        }
    }

    tcp::acceptor acceptor_;
    ssl::context& ctx_;
    std::vector<std::shared_ptr<TLSClient>> clients_;
};


bool InitSSL(ssl::context& ctx){
    try{
        ctx.set_options(
            ssl::context::default_workarounds |
            ssl::context::no_sslv2 |
            ssl::context::no_sslv3 |
            ssl::context::no_tlsv1 |
            ssl::context::no_tlsv1_1 |
            ssl::context::single_dh_use);
        ctx.use_certificate_chain_file(CERT_FILE);
        ctx.use_private_key_file(KEY_FILE, ssl::context::pem);

        ctx.set_password_callback([](size_t, ssl::context::password_purpose){
            return "text";
        });

        SSL_CTX_set_cipher_list(ctx.native_handle(), "HIGH:!aNULL:!MD5:!RC4:!3DES:!DES:!DSS:!PSK:!SRP:!CAMELLIA:!SEED");

        std::cout << "[SSL] successfull initialized SSL context" << std::endl;
        std::cout << "[SSL] using certificate: " << CERT_FILE << std::endl;
        std::cout << "[SSL] using private key: " << KEY_FILE << std::endl;

        return true;
    }
    catch(const std::exception& e){
        std::cerr << "[SSL] Initialization failed" << e.what() << std::endl;
        return false;
    }
}


int main(){
    try{
        asio::io_context io;

        ssl::context ctx(ssl::context::tls_server);
        if(!InitSSL(ctx)){
            return EXIT_FAILURE;
        }
        TLSServer server(io, ctx);
        server.start_accept();

        asio::steady_timer timer(io);
        std::function<void()> check_timeouts = [&](){
            timer.expires_after(std::chrono::seconds(10));
            timer.async_wait([&](boost::system::error_code){
                check_timeouts();
            });
        };
        check_timeouts();
        io.run();
    }
    catch(const std::exception& e){
        std::cerr << "[SYSTEMM] Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}





// bool InitSSL(){
//     // load error strings and algorithm
//     SSL_load_error_strings();
//     OpenSSL_add_all_algorithms();

//     // set SSL context
//     ssl_ctx = SSL_CTX_new(TLS_server_method());
//     if(!ssl_ctx){
//         cerr<< "[SSL] failed to create SSL context" << endl;
//         ERR_print_errors_fp(stderr);
//         return false;
//     }
    
//     // set protocol version
//     SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);

//     // loag server certificate
//     if(SSL_CTX_use_certificate_file(ssl_ctx, CERT_FILE, SSL_FILETYPE_PEM) <= 0){
//         cerr << "[SSL] failed to load server certification" << endl;
//         ERR_print_errors_fp(stderr);
//         return false;
//     }

//     // load server private key
//     if(SSL_CTX_use_PrivateKey_file(ssl_ctx, KEY_FILE, SSL_FILETYPE_PEM) <= 0){
//         cerr << "[SSL] failed to load server private key" << endl;
//         ERR_print_errors_fp(stderr);
//         return false;
//     }

//     // check private key
//     if(!SSL_CTX_check_private_key(ssl_ctx)){
//         cerr << "[SSL] certificate and private key do not match" << endl;
//         return false;
//     }

//     // set cipher suite
//     SSL_CTX_set_cipher_list(ssl_ctx, "HIGH:!aNULL:!MD5:!RC4:!3DES:!DES:!DSS:!PSK:!SRP:!CAMELLIA:!SEED");
//     cout << "[SSL] successfully initialized SSL context" << endl;
//     cout << "[SSL] using certificate: " << CERT_FILE << endl;
//     cout << "[SSL] using private key: " << KEY_FILE << endl;
//     return true;
// }


// int CreateTCPServer(){
//     int server_fd = socket(AF_INET, SOCK_STREAM, 0);
//     if(server_fd == -1){
//         cerr << "[TCP] failed to create socket" << endl;
//         return -1;
//     }

//     // set SO_REUSEADDR option
//     int opt = 1;
//     if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1){
//         cerr << "[ERROR] setsockopt failed" << endl;
//         close(server_fd);
//         return -1;
//     }

//     // bind IP and port
//     sockaddr_in server_addr = {}; //initialize a struct that stores addr type, port number and IP addr
//     server_addr.sin_family = AF_INET; //use IPv4 address
//     server_addr.sin_port = htons(PORT); //use port 8888
//     server_addr.sin_addr.s_addr = INADDR_ANY; //listen to any IP address
//     if(bind(server_fd, (sockaddr*)&server_addr, sizeof(sockaddr_in)) == -1){
//         cout << "bind failed" << endl;
//         close(server_fd);
//         return -1;
//     }

//     // listen to the port
//     if(listen(server_fd, 5) == -1){
//         cout << "listen failed" << endl;
//         close(server_fd);
//         return -1;
//     }
//     cout << "[TCP] server started, listening on port: " << PORT << endl;
    
//     return server_fd;
// }

// // accept new client, make TLS handshake, return SSL pointer
// // ssl_ctx存放证书等静态配置
// // ssl对象处理具体连接的状态（加密密钥、会话数据等）
// SSL* AcceptSSLConnection(int client_fd){
//     sockaddr_in client_addr = {};
//     socklen_t client_addr_len = sizeof(client_addr);
//     getpeername(client_fd, (sockaddr*)&client_addr, &client_addr_len);

//     // create SSL object
//     SSL* ssl = SSL_new(ssl_ctx);
//     SSL_set_fd(ssl, client_fd);

//     // TLS handshake
//     cout << "[SSL] start TLS handshake with " << inet_ntoa(client_addr.sin_addr) << endl;
//     int ssl_ret = SSL_accept(ssl);
//     if(ssl_ret <= 0){
//         int ssl_err = SSL_get_error(ssl, ssl_ret);
//         cerr << "[SSL] handshake failed (error code: " << ssl_err << ")" << endl;
//         ERR_print_errors_fp(stderr);
//         SSL_free(ssl);
//         return nullptr;
//     }

//     // record client info
//     TLSClient new_client;
//     new_client.fd = client_fd;
//     new_client.ssl = ssl;
//     new_client.ip = inet_ntoa(client_addr.sin_addr);
//     new_client.port = ntohs(client_addr.sin_port);
//     new_client.last_active = time(nullptr);
//     active_clients.push_back(new_client);

//     cout << "[SSL] new client " << new_client.ip << " connected, port: " << new_client.port << endl;
    
//     return ssl;
// }


// bool ProcessClientData(TLSClient &client){
//     char buf[BUFFER_SIZE] = {0};
//     int bytes_received = SSL_read(client.ssl, buf, sizeof(buf)-1);
//     if(bytes_received <= 0){
//         int ssl_err = SSL_get_error(client.ssl, bytes_received);
//         if(ssl_err == SSL_ERROR_ZERO_RETURN){
//             cout << "[DISCONNECT] Close client connection: " << client.ip << " : " << client.port << endl;
//             return false;
//         }
//         else if(ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE){
//             return true; //need retry
//         }
//         else{
//             cerr << "[SSL] read error (code: " << ssl_err << "): ";
//             ERR_print_errors_fp(stderr);
//             return false;
//         }
//     }

//     // receive data
//     buf[bytes_received] = '\0';
//     cout << "[DATA] Received from client " << client.ip << ": " << client.port << endl;
//     cout << "   Length: " << bytes_received;
//     cout << "   Content: " << buf << endl;

//     // send data back
//     if(SSL_write(client.ssl, buf, bytes_received) <= 0){
//         cerr << "[SSL] write error: ";
//         ERR_print_errors_fp(stderr);
//         return false;
//     }

//     client.last_active = time(nullptr);
//     return true;
// }


// void CleanupClient(vector<TLSClient>::iterator &it){
//     // close SSL connection
//     if(it->ssl){
//         SSL_shutdown(it->ssl);
//         SSL_free(it->ssl);
//     }
//     // close socket
//     close(it->fd);
//     cout << "[CLEANUP] close connection: " << it->ip << ":" << it->port << endl;
//     // remover from active clients
//     it = active_clients.erase(it);

// }

// // Main Event
// void RunServer(int server_fd){
//     while(true){
//         fd_set read_fds;
//         FD_ZERO(&read_fds);

//         FD_SET(server_fd, &read_fds);
//         int max_fd = server_fd;

//         for(const auto &client : active_clients){
//             FD_SET(client.fd, &read_fds);
//             if(client.fd > max_fd){
//                 max_fd = client.fd;
//             }
//         }

//         if(select(max_fd+1, &read_fds, nullptr, nullptr, nullptr) < 0 && errno != EINTR){
//             cerr << "[SYSTEM] select error: " << strerror(errno) << endl;
//             continue;
//         }

//         if(FD_ISSET(server_fd, &read_fds)){
//             int client_fd = accept(server_fd, NULL, NULL);
//             if(client_fd >= 0){
//                 SSL* ssl = AcceptSSLConnection(client_fd);
//                 if(!ssl){
//                     close(client_fd);
//                 }
//             }
//         }

//         for(auto it = active_clients.begin(); it != active_clients.end();){
//             if(FD_ISSET(it->fd, &read_fds)){
//                 if(!(ProcessClientData(*it))){
//                     CleanupClient(it);
//                     continue; //iterator has been updated in CleanupClient function, skip ++it
//                 }
//             }
//             ++it;
//         }

//         time_t now = time(nullptr);
//         for(auto it = active_clients.begin(); it != active_clients.end();){
//             if(now - it->last_active > 300){
//                 cout << "[TIMEOUT] clean idle client: " << it->ip << ":" << it->port << endl;
//                 CleanupClient(it);
//                 continue;
//             }
//             ++it;
//         }
//     }
// }

// int main(){
//     if(!InitSSL()){
//         return EXIT_FAILURE;
//     }

//     int server_fd = CreateTCPServer();
//     if(server_fd < 0){
//         SSL_CTX_free(ssl_ctx);
//         return EXIT_FAILURE;
//     }

//     RunServer(server_fd);

//     for(auto &client : active_clients){
//         SSL_shutdown(client.ssl);
//         SSL_free(client.ssl);
//         close(client.fd);
//     }
//     close(server_fd);
//     SSL_CTX_free(ssl_ctx);
//     EVP_cleanup();

//     return EXIT_SUCCESS;

// }









