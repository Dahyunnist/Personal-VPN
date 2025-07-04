#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>

void handle_session(int client_socket) {
    try {
        char buffer[1024];
        ssize_t total_length = 0;
        while (true) {
            ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer), 0);
            if (bytes_read <= 0) {
                if (bytes_read == 0) {
                    std::cout << "Received: " << std::string(buffer, total_length) << "\n";
                }
                break;
            }
            total_length += bytes_read;
        }
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    close(client_socket);
}

int main() {
    try {
        // 创建socket
        int server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == -1) {
            throw std::runtime_error("Socket creation failed");
        }

        // 绑定端口
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(8888);
        
        if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            throw std::runtime_error("Bind failed");
        }

        // 开始监听
        if (listen(server_socket, 5) < 0) {
            throw std::runtime_error("Listen failed");
        }

        std::cout << "Sync Server started, listening on port 8888..." << std::endl;

        while (true) {
            // 接受连接
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
            if (client_socket < 0) {
                throw std::runtime_error("Accept failed");
            }

            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            std::cout << "New connection from: " << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;

            // 处理连接
            handle_session(client_socket);
        }
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    return 0;
}