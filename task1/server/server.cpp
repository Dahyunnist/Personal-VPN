#define WIN32_LEAN_AND_MEAN

#include <winSock2.h> //for Windows web programming，must be ahead of windows.h之前
#include <windows.h> //basic Windows header file
#include <bits/stdc++.h> //C++标准库头文件
#pragma comment(lib, "ws2_32.lib") //tell compiler to connect Winsock



using namespace std;


// 1. create a socket
// 2. bind IP and port
// 3. listen to the port
// 4. accept a client connection
// 5. receive data from the client
// 6. send data to the client
// 7. close the socket


int main(){
    // initialize Winsock
    WORD ver = MAKEWORD(2, 2); // define Winsock version
        // MAKEWORD: pack two bytes into a WORD, e.g. (2, 2) becomes version 2.2
    WSADATA dat; //structure to store details about Winsock initialization
    if(WSAStartup(ver, &dat) != 0){
        return 0; //return 0 if initialization fails
    }

    // create server socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
        // AF_INET: use IPv4 address
        // SOCK_STREAM: communicate with TCP protocol
        // explicitly specify TCP protocol
    if(server_socket == SOCKET_ERROR){ 
        cout << "socket creation failed" << endl;
        return 0; //return 0 if socket creation fails
    }

    // bind IP and port
    sockaddr_in _myaddr = {}; //initialize a struct that stores addr type, port number and IP addr
    _myaddr.sin_family = AF_INET; //use IPv4 address
    _myaddr.sin_port = htons(8888); //use port 8888
    _myaddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1"); //store IP
        // inet_addr(): convert string IP to binary format
        // S_un.S_addr: use the S_un union to access the address field(unique to Windows)
    if(bind(server_socket, (sockaddr*)&_myaddr, sizeof(sockaddr_in)) == SOCKET_ERROR){
        cout << "bind failed" << endl;
    }

    // listen to the port
    if(listen(server_socket, 5) == SOCKET_ERROR){
        cout << "listen failed" << endl;
    }

    // allow client to connect
    sockaddr_in _clientAddr = {}; //initialize a struct to store client addr
    int _addr_len = sizeof(sockaddr_in); //get the length of addr struct
    SOCKET _temp_socket = INVALID_SOCKET; //store client socket temporarily
    char _buf[256] = {}; //create buffer to store data from client

    // deal with client connections 
    while(true){
        // first, check if connetction is valid
        _temp_socket = accept(server_socket, (sockaddr*)&_clientAddr, &_addr_len);
        if(_temp_socket == INVALID_SOCKET){
            cout << "received invalid client socket" << endl;
        }
        else{
            cout << "new client added" << endl;
            cout << "IP address: " << inet_ntoa(_clientAddr.sin_addr) << endl;
        }
        while(true){
            // receive data from client
            memset(_buf, 0, sizeof(_buf));
            int _buf_len = recv(_temp_socket, _buf, 256, 0);
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
            send(_temp_socket, _msg, strlen(_msg)+1, 0);
        }
        // close the socket
        closesocket(_temp_socket);
    }

    // close the server socket
    closesocket(server_socket);
    WSACleanup(); //clean up Winsock environment
    return 0;
}