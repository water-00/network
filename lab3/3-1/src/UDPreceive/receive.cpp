#include <stdio.h>
#include <iostream>
#include <winsock2.h>
#include <Windows.h>
#include <WS2tcpip.h>
#include <fstream>
#include <thread>

using namespace std;

// #pragma comment(lib, "ws2_32.lib")

#define PORT 15000
#define IP "127.0.0.1"
#define PACKETSIZE 1500
#define HEADERSIZE 10
#define DATASIZE (PACKETSIZE-HEADERSIZE)
#define FILE_NAME_MAX_LENGTH 64

WSAData wsd;
SOCKET recvSocket = INVALID_SOCKET;
SOCKADDR_IN recvAddr = {0}; // 接收端地址
SOCKADDR_IN sendAddr = {0}; // 发送端地址
int len = sizeof(sendAddr);
char header[HEADERSIZE] = {0};

bool handshake() {
	bool handSuccess = false;

	// 接受第一次握手请求报文

	// 发送第二次握手应答报文

	// 接受第三次握手请求报文
}

void recvfile() {
	char recvBuf[PACKETSIZE] = {0}; // header + data
	char dataSegment[DATASIZE] = {0};
	char filename[FILE_NAME_MAX_LENGTH] = {0};
	int filesize = 0;
	int recvResult = 0; // 接受的packet总长度
	int dataLength = 0; // 其中的数据段长度 = recvResult - HEADERSIZE

	while(true) {
		// 接收文件名
		recvResult = recvfrom(recvSocket, recvBuf, PACKETSIZE, 0, (SOCKADDR*)&sendAddr, &len);

		// 提取header
		memcpy(header, recvBuf, HEADERSIZE);
		
		// 提取文件名
		if (header[4] == 4) {
			memcpy(filename, recvBuf + HEADERSIZE, FILE_NAME_MAX_LENGTH);
		}

		// 接收文件大小
		recvResult = recvfrom(recvSocket, recvBuf, PACKETSIZE, 0, (SOCKADDR*)&sendAddr, &len);

		// 提取header
		memcpy(header, recvBuf, HEADERSIZE);

		// 提取文件大小
		if (header[4] == 8) {
			filesize = atoi(recvBuf + HEADERSIZE);
		}
		cout << "begin to receive a file, filename: " << filename << ", filesize: " << filesize << " bytes." << endl;

		// 接收文件内容
		int hasReceived = 0; // 已接收字节数

		ofstream out;
		out.open(filename, ios::out | ios::binary | ios::app);	
		while (true) {
			memset(recvBuf, 0, PACKETSIZE);
			recvResult = recvfrom(recvSocket, recvBuf, PACKETSIZE, 0, (SOCKADDR*)&sendAddr, &len);
			dataLength = recvResult - HEADERSIZE;
			hasReceived += recvResult - HEADERSIZE;

			// 提取header
			memcpy(header, recvBuf, HEADERSIZE);

			// 提取数据部分
			memcpy(dataSegment, recvBuf + HEADERSIZE, dataLength);

			out.write(dataSegment, dataLength);
			cout << "has received " << hasReceived << " bytes." << endl;
			if (hasReceived == filesize) {
				cout << "receive file " << filename << " successfully! total " << hasReceived << " bytes." << endl;
				break;
			}
		}
	}
}

int main() {
	if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
		cout << "WSAStartup Error = " << WSAGetLastError() << endl;
		exit(1);
	}
	else {
		cout << "start Success" << endl;
	}

	recvSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (recvSocket == SOCKET_ERROR) {
		cout << "socket Error = " << WSAGetLastError() << endl;
		closesocket(recvSocket);
		WSACleanup();
		exit(1);
	}
	else {
		cout << "socket Success" << endl;
	}

	recvAddr.sin_family = AF_INET; // 协议版本
	recvAddr.sin_addr.S_un.S_addr = inet_addr(IP); // ip地址，inet_addr把数点格式转换为整数
	recvAddr.sin_port = htons(PORT); // 端口号，0-65535

	if (bind(recvSocket, (SOCKADDR*)&recvAddr, sizeof(recvAddr)) == SOCKET_ERROR) {
		cout << "bind Error = " << WSAGetLastError() << endl;
		closesocket(recvSocket);
		WSACleanup();
		exit(1);
	}
	else {
		cout << "bind Success" << endl;
	}

    if (handshake()) {
		thread recvfile_thread (recvfile);
        recvfile_thread.join();
    }

    closesocket(recvSocket);
    WSACleanup();
    return 0;
}