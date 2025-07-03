#include <iostream>
#include <string.h>
#include <map>
#include <vector>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>


using namespace std;
// 编译：g++ former.cpp -o former -Wall

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


struct ClientInfo{
    int socket;
    string name;
    string ip;
    uint16_t port;
};

map<int, ClientInfo> client_info;

void show_client_info(const ClientInfo &info){
    cout << "   new client info:" << endl;
    cout << "   socket: " << info.socket << endl;
    cout << "   name: " << info.name << endl;
    cout << "   ip: " << info.ip << endl;
    cout << "   port: " << info.port << endl;
}



int main(){
    // create server socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
        // AF_INET: use IPv4 address
        // SOCK_STREAM: communicate with TCP protocol
        // explicitly specify TCP protocol
    if(server_socket == -1){ 
        cout << "socket creation failed" << endl;
        return 1; //return 0 if socket creation fails
    }

    // set SO_REUSEADDR option
    int opt = 1;
    if(setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1){
        cerr << "[ERROR] setsockopt failed" << endl;
        close(server_socket);
        return 1;
    }

    // bind IP and port
    sockaddr_in _myaddr = {}; //initialize a struct that stores addr type, port number and IP addr
    _myaddr.sin_family = AF_INET; //use IPv4 address
    _myaddr.sin_port = htons(PORT); //use port 8888
    _myaddr.sin_addr.s_addr = INADDR_ANY; //listen to any IP address
        // inet_addr(): convert string IP to binary format
        // S_un.S_addr: use the S_un union to access the address field(specific to Windows)
    if(bind(server_socket, (sockaddr*)&_myaddr, sizeof(sockaddr_in)) == -1){
        cout << "bind failed" << endl;
        close(server_socket);
        return 1;
    }

    // listen to the port
    if(listen(server_socket, 5) == -1){
        cout << "listen failed" << endl;
        close(server_socket);
        return 1;
    }

    // // allow client to connect
    // sockaddr_in _clientAddr = {}; //initialize a struct to store client addr
    // socklen_t _addr_len = sizeof(sockaddr_in); //get the length of addr struct
    // int client_socket = -1; //store client socket temporarily
    // char _buf[256] = {}; //create buffer to store data from client

    // initialize select variables
    fd_set read_fds; //a bitmap
    int client_sockets[MAX_CLIENTS] = {0};
    int max_sd = server_socket;


    // main event
    while(true){
        FD_ZERO(&read_fds); //clear the set
        FD_SET(server_socket, &read_fds);

        // add all client sockets to the set
        for(int i = 0; i < MAX_CLIENTS; i++){
            if(client_sockets[i] > 0){
                FD_SET(client_sockets[i], &read_fds);
                if(client_sockets[i] > max_sd){
                    max_sd = client_sockets[i];
                }
            }
        }

        // use select function to wait for events
        if(select(max_sd+1, &read_fds, nullptr, nullptr, nullptr) < 0){
            cerr << "[ERROR] select error" << endl;
            continue;
        }

        // deal with new connections
        if(FD_ISSET(server_socket, &read_fds)){
            sockaddr_in client_addr = {};
            socklen_t client_addr_len = sizeof(client_addr);
            int new_socket = accept(server_socket, (sockaddr*)&client_addr, &client_addr_len);
            if(new_socket == -1){
                cerr << "[ERROR] Accept failed" << endl;
                continue;
            }
            ClientInfo info;
            info.socket = new_socket;
            info.name = "Client_" + to_string(new_socket);
            info.port = ntohs(client_addr.sin_port);
            info.ip = inet_ntoa(client_addr.sin_addr);
            client_info[new_socket] = info;

            cout << "[INFO] New Client connected: " << inet_ntoa(client_addr.sin_addr) << endl;
            show_client_info(info);

            for(int i = 0; i < MAX_CLIENTS; i++){
                if(client_sockets[i] == 0){
                    client_sockets[i] = new_socket;
                    break;
                }
            }
        }

        // deal with client data
        for(int i = 0; i < MAX_CLIENTS; i++){
            int client_socket = client_sockets[i];
            if(client_socket > 0 && FD_ISSET(client_socket, &read_fds)){
                char buf[BUFFER_SIZE] = {};
                memset(buf, 0, sizeof(buf));

                int recv_len = recv(client_socket, buf, BUFFER_SIZE, 0);
                if(recv_len <= 0){
                    if(recv_len < 0){
                        cout << "[INFO] Data receive failed" << endl;
                    }
                    else{
                        cout << "[INFO] Client " << client_info[client_socket].name << " connection closed" << endl;
                    }
                    
                    close(client_socket);
                    client_sockets[i] = 0;
                }
                else{
                    cout << "[DATA] Received from client " << client_info[client_socket].name << " : " << buf << endl;
                    if(strcmp(buf, "closesocket") == 0){
                        cout << "[INFO] Closing client socket as " << client_info[client_socket].name << " requested..." << endl;
                        close(client_socket);
                        client_sockets[i] = 0;
                    }
                    else{
                        send(client_socket, buf, recv_len, 0);
                        cout << "[DATA] Sent to client " << client_info[client_socket].name << ": " << buf << endl;
                    }
                }
            }
        }
        
    }


    // close the server socket
    close(server_socket);
    return 0;
}