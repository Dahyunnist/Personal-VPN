#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <string.h>

using namespace std;
// 编译：g++ server.cpp -o server -Wall

// 1. create a socket
// 2. bind IP and port
// 3. listen to the port
// 4. accept a client connection
// 5. receive data from the client
// 6. send data to the client
// 7. close the socket


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

    // bind IP and port
    sockaddr_in _myaddr = {}; //initialize a struct that stores addr type, port number and IP addr
    _myaddr.sin_family = AF_INET; //use IPv4 address
    _myaddr.sin_port = htons(8888); //use port 8888
    _myaddr.sin_addr.s_addr = inet_addr("0.0.0.0"); //store IP
        // inet_addr(): convert string IP to binary format
        // S_un.S_addr: use the S_un union to access the address field(unique to Windows)
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

    // allow client to connect
    sockaddr_in _clientAddr = {}; //initialize a struct to store client addr
    socklen_t _addr_len = sizeof(sockaddr_in); //get the length of addr struct
    int client_socket = -1; //store client socket temporarily
    char _buf[256] = {}; //create buffer to store data from client

    // deal with client connections 
    while(true){
        // first, check if connetction is valid
        client_socket = accept(server_socket, (sockaddr*)&_clientAddr, &_addr_len);
        if(client_socket == -1){
            cout << "received invalid client socket" << endl;
            continue;
        }
        else{
            cout << "New client added" << endl;
            cout << "IP address: " << inet_ntoa(_clientAddr.sin_addr) << endl;
        }
        while(true){
            // receive data from client
            memset(_buf, 0, sizeof(_buf));
            int _buf_len = recv(client_socket, _buf, 256, 0);
            if(_buf_len <= 0){
                cout << "Data receive failed or connection error" << endl;
                break;
            }
            cout << "Received from client: " << _buf << endl;
            if(strcmp(_buf, "closesocket") == 0){
                cout << "Now Close Socket as requested..." << endl;
                break;
            }
            // reply to client
            char _msg[256] = {};
            memcpy(&_msg, _buf, _buf_len); //copy data from buffer to message
            send(client_socket, _msg, strlen(_msg)+1, 0);
        }
        // close the socket
        close(client_socket);
    }

    // close the server socket
    close(server_socket);
    return 0;
}