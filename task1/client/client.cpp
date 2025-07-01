#define WIN32_LEAN_AND_MEAN

#include<winSock2.h>
#include<windows.h>
#include<bits/stdc++.h>

#pragma comment(lib,"ws2_32.lib")//链接此动态链接库 windows特有 

using namespace std; 


// 1. create a socket
// 2. connect to a server
// 3. send data to client
// 4. receive data from client
// 5. close the socket



int main()
{
	// Initialize Winsock 
	WORD ver = MAKEWORD(2,2);
	WSADATA dat; 
	if(WSAStartup(ver,&dat) != 0)
	{
		return 0;
	}
	
	//create client socket 
	SOCKET _mysocket = socket(AF_INET,SOCK_STREAM,0);
	if(_mysocket == INVALID_SOCKET)
    {   
        return 0;  
    } 
    
    //connect to server
    sockaddr_in _sin = {};
    _sin.sin_family = AF_INET;
    _sin.sin_port = htons(8888);
	_sin.sin_addr.S_un.S_addr =  inet_addr("127.0.0.1");
	if(connect(_mysocket, (sockaddr*)&_sin, sizeof(sockaddr_in)) == SOCKET_ERROR){
		cout << "connecttion failed" << endl;
		closesocket(_mysocket);
	}
	else{
		cout << "successfully connected" << endl; 
	}

	char _buf[256] = {};
	while(true){
		char _msg[256] = {};
		cout << "Enter message(type 'closesocket' to exit): ";
		cin.getline(_msg, sizeof(_msg));
		//send data to server
		send(_mysocket,_msg, strlen(_msg)+1, 0);//客户端套接字 数据 数据长短 flag 
		
		// decide whether to exit
		if(strcmp(_msg, "closesocket") == 0){
			cout << "Exiting..." << endl;
			break;
		}

		//receive data from server
		memset(_buf, 0, sizeof(_buf));
		int _buf_len = recv(_mysocket, _buf, 256, 0);
		if(_buf_len > 0){
			cout << "Recieved from server: " << _buf << endl;
		}
		else{
			cout << "Failed to receive data from server or connection error" << endl;
			break;
		}

	}

	closesocket(_mysocket);  
	WSACleanup(); 
	return 0;
} 
