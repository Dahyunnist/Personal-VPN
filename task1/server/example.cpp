#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <string.h>
#include <sys/select.h>

using namespace std;

const int BUFFER_SIZE = 256;  // 接收缓冲区大小
const int PORT = 8888;        // 监听端口
const int MAX_CLIENTS = 10;    // 最大客户端数量

int main() {
    // 1. 创建服务器socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        cerr << "[ERROR] Socket creation failed" << endl;
        return 1;
    }

    // 2. 设置SO_REUSEADDR选项（避免重启时端口占用）
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        cerr << "[ERROR] setsockopt failed" << endl;
        close(server_socket);
        return 1;
    }

    // 3. 绑定IP和端口
    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;  // 监听所有网卡

    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        cerr << "[ERROR] Bind failed" << endl;
        close(server_socket);
        return 1;
    }

    // 4. 开始监听
    if (listen(server_socket, 5) == -1) {
        cerr << "[ERROR] Listen failed" << endl;
        close(server_socket);
        return 1;
    }
    cout << "[INFO] Server started on port " << PORT << ", waiting for clients..." << endl;

    // 5. 初始化select相关变量
    fd_set read_fds;                      // 读事件描述符集合
    int client_sockets[MAX_CLIENTS] = {0}; // 客户端socket数组
    int max_sd = server_socket;           // 当前最大描述符

    // 6. 主事件循环
    while (true) {
        FD_ZERO(&read_fds);                // 清空描述符集合
        FD_SET(server_socket, &read_fds);  // 添加服务器socket到监控集合

        // 添加所有客户端socket到监控集合
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] > 0) {
                FD_SET(client_sockets[i], &read_fds);
                if (client_sockets[i] > max_sd) {
                    max_sd = client_sockets[i];  // 更新最大描述符
                }
            }
        }

        // 7. 调用select等待事件（阻塞）
        int activity = select(max_sd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            cerr << "[ERROR] Select error" << endl;
            continue;
        }

        // 8. 处理新连接
        if (FD_ISSET(server_socket, &read_fds)) {
            sockaddr_in client_addr = {};
            socklen_t addr_len = sizeof(client_addr);
            int new_socket = accept(server_socket, (sockaddr*)&client_addr, &addr_len);

            if (new_socket == -1) {
                cerr << "[ERROR] Accept failed" << endl;
                continue;
            }

            cout << "[INFO] New client connected: " << inet_ntoa(client_addr.sin_addr) << endl;

            // 将新socket加入数组
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i] == 0) {
                    client_sockets[i] = new_socket;
                    break;
                }
            }
        }

        // 9. 处理客户端数据
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int client_socket = client_sockets[i];
            if (client_socket > 0 && FD_ISSET(client_socket, &read_fds)) {
                char buffer[BUFFER_SIZE];
                memset(buffer, 0, BUFFER_SIZE);

                // 接收数据
                int recv_len = recv(client_socket, buffer, BUFFER_SIZE, 0);
                if (recv_len <= 0) {
                    // 客户端断开连接
                    cout << "[INFO] Client disconnected" << endl;
                    close(client_socket);
                    client_sockets[i] = 0;
                } else {
                    // 打印接收到的数据
                    cout << "[DATA] From client " << i << ": " << buffer << endl;

                    // 检查退出指令
                    if (strcmp(buffer, "closesocket") == 0) {
                        cout << "[INFO] Closing connection as requested" << endl;
                        close(client_socket);
                        client_sockets[i] = 0;
                    } else {
                        // 回显数据
                        send(client_socket, buffer, recv_len, 0);
                    }
                }
            }
        }
    }

    // 10. 清理资源（实际不会执行到这里）
    close(server_socket);
    return 0;
}