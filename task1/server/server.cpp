#include <iostream>
#include <string.h>
#include <map>
#include <vector>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <time.h>


using namespace std;
// 编译：g++ server.cpp -o server -lssl -lcrypto -Wall

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
// SSL_CTX_use_certificate_file(ctx, "/home/username/server/certs/server.crt", SSL_FILETYPE_PEM);
// SSL_CTX_load_verify_locations(ctx, "C:/tasks/task1/client/server.crt", NULL);

struct TLSClient{
    int fd;
    SSL* ssl;
    string ip;
    uint16_t port;
    time_t last_active;
};
vector<TLSClient> active_clients;
SSL_CTX* ssl_ctx = nullptr;

bool InitSSL(){
    // load error strings and algorithm
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    // set SSL context
    ssl_ctx = SSL_CTX_new(TLS_server_method());
    if(!ssl_ctx){
        cerr<< "[SSL] failed to create SSL context" << endl;
        ERR_print_errors_fp(stderr);
        return false;
    }
    
    // set protocol version
    SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);

    // loag server certificate
    if(SSL_CTX_use_certificate_file(ssl_ctx, CERT_FILE, SSL_FILETYPE_PEM) <= 0){
        cerr << "[SSL] failed to load server certification" << endl;
        ERR_print_errors_fp(stderr);
        return false;
    }

    // load server private key
    if(SSL_CTX_use_PrivateKey_file(ssl_ctx, KEY_FILE, SSL_FILETYPE_PEM) <= 0){
        cerr << "[SSL] failed to load server private key" << endl;
        ERR_print_errors_fp(stderr);
        return false;
    }

    // check private key
    if(!SSL_CTX_check_private_key(ssl_ctx)){
        cerr << "[SSL] certificate and private key do not match" << endl;
        return false;
    }

    // set cipher suite
    SSL_CTX_set_cipher_list(ssl_ctx, "HIGH:!aNULL:!MD5:!RC4:!3DES:!DES:!DSS:!PSK:!SRP:!CAMELLIA:!SEED");
    cout << "[SSL] successfully initialized SSL context" << endl;
    cout << "[SSL] using certificate: " << CERT_FILE << endl;
    cout << "[SSL] using private key: " << KEY_FILE << endl;
    return true;
}


int CreateTCPServer(){
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd == -1){
        cerr << "[TCP] failed to create socket" << endl;
        return -1;
    }

    // set SO_REUSEADDR option
    int opt = 1;
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1){
        cerr << "[ERROR] setsockopt failed" << endl;
        close(server_fd);
        return -1;
    }

    // bind IP and port
    sockaddr_in server_addr = {}; //initialize a struct that stores addr type, port number and IP addr
    server_addr.sin_family = AF_INET; //use IPv4 address
    server_addr.sin_port = htons(PORT); //use port 8888
    server_addr.sin_addr.s_addr = INADDR_ANY; //listen to any IP address
    if(bind(server_fd, (sockaddr*)&server_addr, sizeof(sockaddr_in)) == -1){
        cout << "bind failed" << endl;
        close(server_fd);
        return -1;
    }

    // listen to the port
    if(listen(server_fd, 5) == -1){
        cout << "listen failed" << endl;
        close(server_fd);
        return -1;
    }
    cout << "[TCP] server started, listening on port: " << PORT << endl;
    
    return server_fd;
}

// accept new client, make TLS handshake, return SSL pointer
SSL* AcceptSSLConnection(int client_fd){
    sockaddr_in client_addr = {};
    socklen_t client_addr_len = sizeof(client_addr);
    getpeername(client_fd, (sockaddr*)&client_addr, &client_addr_len);

    // create SSL object
    SSL* ssl = SSL_new(ssl_ctx);
    SSL_set_fd(ssl, client_fd);

    // TLS handshake
    cout << "[SSL] start TLS handshake with " << inet_ntoa(client_addr.sin_addr) << endl;
    int ssl_ret = SSL_accept(ssl);
    if(ssl_ret <= 0){
        int ssl_err = SSL_get_error(ssl, ssl_ret);
        cerr << "[SSL] handshake failed (error code: " << ssl_err << ")" << endl;
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return nullptr;
    }

    // record client info
    TLSClient new_client;
    new_client.fd = client_fd;
    new_client.ssl = ssl;
    new_client.ip = inet_ntoa(client_addr.sin_addr);
    new_client.port = ntohs(client_addr.sin_port);
    new_client.last_active = time(nullptr);
    active_clients.push_back(new_client);

    cout << "[SSL] new client " << new_client.ip << " connected, port: " << new_client.port << endl;
    
    return ssl;
}


bool ProcessClientData(TLSClient &client){
    char buf[BUFFER_SIZE] = {0};
    int bytes_received = SSL_read(client.ssl, buf, sizeof(buf)-1);
    if(bytes_received <= 0){
        int ssl_err = SSL_get_error(client.ssl, bytes_received);
        if(ssl_err == SSL_ERROR_ZERO_RETURN){
            cout << "[DISCONNECT] Close client connection: " << client.ip << " : " << client.port << endl;
            return false;
        }
        else if(ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE){
            return true; //need retry
        }
        else{
            cerr << "[SSL] read error (code: " << ssl_err << "): ";
            ERR_print_errors_fp(stderr);
            return false;
        }
    }

    // receive data
    buf[bytes_received] = '\0';
    cout << "[DATA] Received from client " << client.ip << ": " << client.port << endl;
    cout << "   Length: " << bytes_received;
    cout << "   Content: " << buf << endl;

    // send data back
    if(SSL_write(client.ssl, buf, bytes_received) <= 0){
        cerr << "[SSL] write error: ";
        ERR_print_errors_fp(stderr);
        return false;
    }

    client.last_active = time(nullptr);
    return true;
}


void CleanupClient(vector<TLSClient>::iterator &it){
    // close SSL connection
    if(it->ssl){
        SSL_shutdown(it->ssl);
        SSL_free(it->ssl);
    }
    // close socket
    close(it->fd);
    cout << "[CLEANUP] close connection: " << it->ip << ":" << it->port << endl;
    // remover from active clients
    it = active_clients.erase(it);

}

// Main Event
void RunServer(int server_fd){
    while(true){
        fd_set read_fds;
        FD_ZERO(&read_fds);

        FD_SET(server_fd, &read_fds);
        int max_fd = server_fd;

        for(const auto &client : active_clients){
            FD_SET(client.fd, &read_fds);
            if(client.fd > max_fd){
                max_fd = client.fd;
            }
        }

        if(select(max_fd+1, &read_fds, nullptr, nullptr, nullptr) < 0 && errno != EINTR){
            cerr << "[SYSTEM] select error: " << strerror(errno) << endl;
            continue;
        }

        if(FD_ISSET(server_fd, &read_fds)){
            int client_fd = accept(server_fd, NULL, NULL);
            if(client_fd >= 0){
                SSL* ssl = AcceptSSLConnection(client_fd);
                if(!ssl){
                    close(client_fd);
                }
            }

        }

        for(auto it = active_clients.begin(); it != active_clients.end();){
            if(FD_ISSET(it->fd, &read_fds)){
                if(!(ProcessClientData(*it))){
                    CleanupClient(it);
                    continue; //iterator has been updated in CleanupClient function, skip ++it
                }
            }
            ++it;
        }

        time_t now = time(nullptr);
        for(auto it = active_clients.begin(); it != active_clients.end();){
            if(now - it->last_active > 300){
                cout << "[TIMEOUT] clean idle client: " << it->ip << ":" << it->port << endl;
                CleanupClient(it);
                continue;
            }
            ++it;
        }
    }
}

int main(){
    if(!InitSSL()){
        return EXIT_FAILURE;
    }

    int server_fd = CreateTCPServer();
    if(server_fd < 0){
        SSL_CTX_free(ssl_ctx);
        return EXIT_FAILURE;
    }

    RunServer(server_fd);

    for(auto &client : active_clients){
        SSL_shutdown(client.ssl);
        SSL_free(client.ssl);
        close(client.fd);
    }
    close(server_fd);
    SSL_CTX_free(ssl_ctx);
    EVP_cleanup();

    return EXIT_SUCCESS;

}









