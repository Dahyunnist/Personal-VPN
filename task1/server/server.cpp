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