// #define WIN32_LEAN_AND_MEAN

// #include <windows.h>
#include <winsock2.h>
// #include <ws2tcpip.h>
// #include <openssl/ssl.h>
// #include <openssl/err.h>
#include <iostream>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <memory>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>

#pragma comment(lib,"ws2_32.lib")//链接此动态链接库 windows特有 
// #pragma comment(lib, "libssl.lib")
// #pragma comment(lib, "libcrypto.lib")

/*Compile command for version before TLS*/
// 编译时要在后面加上-lws2_32
// e.g. g++ server.cpp -o server -lws2_32


/*Compile order for TLS version(supported boost) in cmd:
g++ client.cpp -o client.exe ^
    -I "C:\OpenSSL-Win64\include" ^
    -L "C:\OpenSSL-Win64\lib" ^
    -lws2_32 -llibssl -llibcrypto
*/

namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace beast = boost::beast;
using tcp = asio::ip::tcp; 

const std::string SERVER_IP = "127.0.0.1";
const unsigned short SERVER_PORT = 8888;
const std::string CA_CERT_PATH = "certs/server.crt";
const std::string CLIENT_CERT_PATH = "certs/client.crt";
const std::string CLIENT_KEY_PATH = "certs/client.key";
const std::string SSL_KEYLOG_FILE = "sslkeylog.txt";

class KeyLogger{
public:
	KeyLogger(){
		file_.open(SSL_KEYLOG_FILE, std::ios::app);
		if(!file_){
			std::cerr << "[WARNING] Failed to open keylog file: " << SSL_KEYLOG_FILE << std::endl;
		}
	}

	void log(const std::string& line){
		if(file_){
			file_ << line << std::endl;
			file_.flush();
		}
	}

	~KeyLogger(){
		if(file_) file_.close();
	}

private:
	std::ofstream file_;
};


class TLSClient{
public:
	using ssl_stream = beast::ssl_stream<tcp::socket>;

	TLSClient(asio::io_context& io ,ssl::context& ctx, KeyLogger& logger) : resolver_(io), socket_(io, ctx), logger_(logger){
		SSL_CTX_set_keylog_callback(ctx.native_handle(), [](const SSL* ssl, const char* line){
			static KeyLogger* logger = nullptr;
			if(!logger){
				logger = static_cast<KeyLogger*>(SSL_CTX_get_app_data(SSL_get_SSL_CTX(ssl)));
			}
			if(logger) logger->log(line);
		});

		SSL_CTX_set_app_data(ctx.native_handle(), &logger_);
	}

	void connect(){
		auto endpoints = resolver_.resolve(SERVER_IP, std::to_string(SERVER_PORT));
		asio::connect(socket_.next_layer(), endpoints);
		socket_.handshake(ssl::stream_base::client);

		std::cout << "[SSL] Successfully connected to server" << socket_.next_layer().remote_endpoint() << std::endl;
		std::cout << "	Version: " << SSL_get_version(socket_.native_handle()) << std::endl;
		std::cout << "	Cipher: " << SSL_get_cipher(socket_.native_handle()) << std::endl;
	}

	void communicate(){
		while(true){
			std::cout << "Send message(type 'quit' to exit): ";
			std::string message;
			getline(std::cin, message);
			if(message == "quit"){
				break;
			}
			asio::write(socket_, asio::buffer(message));
			
			// receive from server
			std::vector<char> buf(4096);
			std::size_t len = asio::read(socket_, asio::buffer(buf), asio::transfer_at_least(1));
			std::cout << "[DATA] Received from server" << socket_.next_layer().remote_endpoint() << std::endl;
			std::cout << "	Length: " << len << std::endl;
			std::cout << "	Content: " << std::string(buf.data(), len) << std::endl;
		}
	}

	void disconnect(){
		beast::error_code ec;
		socket_.shutdown(ec);
		socket_.next_layer().shutdown(tcp::socket::shutdown_both, ec);
		socket_.next_layer().close(ec);
	}

private:
	tcp::resolver resolver_;
	ssl_stream socket_;
	KeyLogger& logger_;
};

bool InitSSL(ssl::context& ctx){
	try{
		ctx.set_options(
			ssl::context::default_workarounds |
			ssl::context::no_sslv2 |
			ssl::context::no_sslv3 |
			ssl::context::no_tlsv1 |
			ssl::context::no_tlsv1_1
		);
		ctx.load_verify_file(CA_CERT_PATH);
		ctx.use_certificate_file(CLIENT_CERT_PATH, ssl::context::pem);
		ctx.use_private_key_file(CLIENT_KEY_PATH, ssl::context::pem);
		ctx.set_verify_mode(ssl::verify_peer);
		return true;
	}
	catch(const std::exception& e){
		std::cerr << "[SSL] Initialization failed: " << e.what() << std::endl;
		return false;
	}
}
// // 1. create a socket
// // 2. connect to a server
// // 3. send data to client
// // 4. receive data from client
// // 5. close the socket

int main()
{
	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);
	try{
		asio::io_context io;
		ssl::context ctx(ssl::context::tls_client);
		KeyLogger key_logger;

		if(!InitSSL(ctx)){
			return 1;
		}

		TLSClient client(io, ctx, key_logger);
		client.connect();
		client.communicate();
		client.disconnect();
		WSACleanup();
		return 0;
	}
	catch(const std::exception& e){
		std::cerr << "[SYSTEM] Error: " << e.what() << std::endl;
		WSACleanup();
		return 1;
	}
} 