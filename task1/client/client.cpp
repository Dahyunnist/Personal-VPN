#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <iostream>
#include <string>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib,"ws2_32.lib")//链接此动态链接库 windows特有 
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")

/*Compile command for version before TLS*/
// 编译时要在后面加上-lws2_32
// e.g. g++ server.cpp -o server -lws2_32


/*Compile order for TLS version:
g++ client.cpp -o client.exe ^
    -I "C:\OpenSSL-Win64\include" ^
    -L "C:\OpenSSL-Win64\lib" ^
    -lws2_32 -llibssl -llibcrypto
*/

using namespace std; 

const char* SERVER_IP = "127.0.0.1";
const int SERVER_PORT = 8888;
const char* CA_CERT_PATH = "certs/server.crt";
const char* CLIENT_CERT_PATH = "certs/client.crt";
const char* CLIENT_KEY_PATH = "certs/client.key";

static FILE* ssl_keylog_file = NULL;
void ssl_keylog_callback(const SSL* ssl, const char* line){
	if(ssl_keylog_file){
		fprintf(ssl_keylog_file, "%s\n", line);
		fflush(ssl_keylog_file);
	}
}


SSL_CTX* InitSSLContext(){
	SSL_CTX *ctx = nullptr;

	SSL_load_error_strings();
	OpenSSL_add_ssl_algorithms();

	ctx = SSL_CTX_new(TLS_client_method());
	if(!ctx){
		cerr << "[SSL] create context failed" << endl;
		ERR_print_errors_fp(stderr);
		return nullptr;
	}

	SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

	ssl_keylog_file = fopen("sslkeylog.txt", "a");
	if(!ssl_keylog_file){
		cerr << "[WARNING] keylog file open failed" << strerror(errno) << endl;
	}
	SSL_CTX_set_keylog_callback(ctx, ssl_keylog_callback);

	// load CA certificate
	if(SSL_CTX_load_verify_locations(ctx, CA_CERT_PATH, nullptr) != 1){
		cerr << "[SSL] load CA certificate failed" << endl;
		ERR_print_errors_fp(stderr);
		SSL_CTX_free(ctx);
		return nullptr;
	}

	// load client certificate and private key
	if(SSL_CTX_use_certificate_file(ctx, CLIENT_CERT_PATH, SSL_FILETYPE_PEM) != 1){
		cerr << "[SSL] load client certificate failed" << endl;
		ERR_print_errors_fp(stderr);
		SSL_CTX_free(ctx);
		return nullptr;
	}
	if(SSL_CTX_use_PrivateKey_file(ctx, CLIENT_KEY_PATH, SSL_FILETYPE_PEM) != 1){
		cerr << "[SSL] load client private key failed" << endl;
		ERR_print_errors_fp(stderr);
		SSL_CTX_free(ctx);
		return nullptr;
	}

	// check if private key matches
	if(SSL_CTX_check_private_key(ctx) != 1){
		cerr << "[SSL] certificate does not match private key" << endl;
		SSL_CTX_free(ctx);
		return nullptr;
	}

	// set check type, check server certificate
	SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);

	cout << "[SSL] successfully initialized context" << endl;
	return ctx;
}


// create TCP connection
int ConnectToServer(){
	// initialize Winsock
	WSADATA wsaData;
	if(WSAStartup(MAKEWORD(2, 2), &wsaData) != 0){
		cerr << "[TCP] Winsock initialization failed: " << WSAGetLastError() << endl;
		return -1;
	}

	// create socket
	int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(sockfd == INVALID_SOCKET){
		cerr << "[TCP] socket creation failed: " << WSAGetLastError() << endl;
		WSACleanup();
		return -1;
	}

	// set server address
	sockaddr_in server_addr = {};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT);
	inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

	// connect to server
	if(connect(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR){
		cerr << "[TCP] connect to server failed: " << WSAGetLastError() << endl;
		closesocket(sockfd);
		WSACleanup();
		return -1;
	}

	cout << "[TCP] connected to server " << SERVER_IP << ":" << SERVER_PORT << endl;
	return sockfd;
}

SSL* DoTLSHandshake(SSL_CTX *ctx, int sockfd){
	// create SSL object
	SSL* ssl = SSL_new(ctx);
	SSL_set_fd(ssl, sockfd);

	// TLS handshake
	cout << "Start TLS handshake..." << endl;
	int ret = SSL_connect(ssl);
	if(ret <= 0){
		int sslerr = SSL_get_error(ssl, ret);
		cerr << "[SSL] handshake failed(code: " << sslerr << "): ";
		ERR_print_errors_fp(stderr);

		SSL_shutdown(ssl);
		SSL_free(ssl);
		return nullptr;
	}

	// check server certificate
	X509* cert = SSL_get_peer_certificate(ssl);
	if(!cert){
		cerr << "[SSL] server certificate not received" << endl;
		SSL_shutdown(ssl);
		SSL_free(ssl);
		return nullptr;
	}
	if(SSL_get_verify_result(ssl) != X509_V_OK){
		cerr << "[SSL] certificate verification failed" << endl;
		X509_free(cert);
		SSL_shutdown(ssl);
		SSL_free(ssl);
		return nullptr;
	}

	cout << "[SSL] safe connection has been built" << endl;
	cout << "	protocol version: " << SSL_get_version(ssl) << endl;
	cout << "	cipher suite:     " << SSL_get_cipher(ssl) << endl;
	
	char subject[256];
	X509_NAME_oneline(X509_get_subject_name(cert), subject, sizeof(subject));
	cout << "	server certificate: " << subject << endl;

	X509_free(cert);
	return ssl;
}

// communication loop
void SecureCommunication(SSL* ssl){
	char buf[1024];
	string message;

	while(true){
		cout << "send message(type 'quit' to exit): ";
		getline(cin, message);
		if(message == "quit"){
			break;
		}

		// encrypt data to send
		int bytes_sent = SSL_write(ssl, message.c_str(), (int)message.length());
		if(bytes_sent <= 0){
			cout << "[SSL] message send failed(code: " << SSL_get_error(ssl, bytes_sent) << ")" << endl;
			break;
		}

		// receive encrypted data
		int bytes_read = SSL_read(ssl, buf, sizeof(buf)-1);
		if(bytes_read <= 0){
			int sslerr = SSL_get_error(ssl, bytes_read);
			if(sslerr == SSL_ERROR_ZERO_RETURN){
				cout << "[SSL] server closed connection" << endl;
			}
			else{
				cerr << "[SSL] receive failed(code: " << sslerr << ")" << endl;
			}
			break;
		}
		buf[bytes_read] = '\0';
		cout << "[DATA] received from server: " << buf << endl; 
	}
}

void Cleanup(SSL* ssl, int sockfd, SSL_CTX* ctx){
	if(ssl){
		SSL_shutdown(ssl);
		SSL_free(ssl);
	}
	if(sockfd != -1){
		closesocket(sockfd);
		WSACleanup();
	}
	if(ctx){
		SSL_CTX_free(ctx);
	}
	EVP_cleanup();
}


// // 1. create a socket
// // 2. connect to a server
// // 3. send data to client
// // 4. receive data from client
// // 5. close the socket



int main()
{
	SSL_CTX* ctx = nullptr;
	SSL* ssl = nullptr;
	int sockfd = -1;

	// initialize SSL
	ctx = InitSSLContext();
	if(!ctx){
		return 1;
	}

	// TCP connect
	sockfd = ConnectToServer();
	if(sockfd == -1){
		Cleanup(nullptr, -1, ctx);
		return 1;
	}

	// TLS handshake
	ssl = DoTLSHandshake(ctx, sockfd);
	if(!ssl){
		Cleanup(nullptr, sockfd, ctx);
		return 1;
	}

	SecureCommunication(ssl);

	Cleanup(ssl, sockfd, ctx);
	return 0;
} 
