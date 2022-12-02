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
u_char header[HEADERSIZE] = {0};

u_short checkSum(const u_char* input, int length) {
	int count = (length + 1) / 2; // 有多少组16 bits
	u_short* buf = new u_short[count]{0};
	for (int i = 0; i < count; i++) {
		buf[i] = input[2 * i] + ((2 * i + 1 < length) ? input[2 * i + 1] << 8 : 0); 
		// 最后这个三元表达式是为了避免在计算buf最后一位时，出现input[length]的越界情况
	}

    register u_long sum = 0;
    while (count--) {
        sum += *buf++;
        // 如果sum有进位，则进位回滚
        if (sum & 0xFFFF0000) {
            sum &= 0xFFFF;
            sum++;
        }
    }
	// delete buf;
    return ~(sum & 0xFFFF);
}

bool handshake() {
	u_short checksum = 0;
	u_char recvBuf[HEADERSIZE] = {0};
    int recvResult = 0;
	int seq, ack;

	// 接受第一次握手请求报文
    while(true) {
        recvResult = recvfrom(recvSocket, (char*)recvBuf, HEADERSIZE, 0, (SOCKADDR*)&sendAddr, &len);
		// 检查checksum
		checksum = checkSum(recvBuf, HEADERSIZE);
		// 提取seq of message 1
		seq = recvBuf[0] + (recvBuf[1] << 8);

		if (checksum == 0 && recvBuf[4] == 0b010) {
			cout << "successfully received the First Handshake message!" << endl;
			break;
		} else {
			cout << "failed to received the correct First Handshake message, Handshake failed!" << endl;
			return false;
		}
    }

	// 发送第二次握手应答报文
	memset(header, 0, HEADERSIZE);
	// 设置ack位，ack = seq of message 1 + 1
	ack = seq + 1;
	header[2] = (u_short)(ack & 0xFF);
	header[3] = (u_short)(ack >> 8);
	// 设置seq位
	seq = rand() % 65535;
	header[0] = (u_short)(seq & 0xFF);
	header[1] = (u_short)(seq >> 8);
	// 设置ACK SYN位
	header[4] = 0b110;
    sendto(recvSocket, (char*)header, HEADERSIZE, 0, (SOCKADDR*)&sendAddr, sizeof(SOCKADDR));
    cout << "send the Second Handshake message!" << endl;

	// 接受第三次握手请求报文
	while(true) {
		recvResult = recvfrom(recvSocket, (char*)recvBuf, HEADERSIZE, 0, (SOCKADDR*)&sendAddr, &len);
		// 检查checksum
		checksum = checkSum(recvBuf, HEADERSIZE);
		// cout << "checksum = " << checksum << endl;
		// 提取ack of message 3
		ack = recvBuf[2] + (recvBuf[3] << 8);

		if (checksum == 0 && ack == seq + 1 && recvBuf[4] == 0b100) {
			cout << "successfully received the Third Handshake message!" << endl;
			break;
		} else {
			cout << "failed to received the correct Third Handshake message, Handshake failed!" << endl;
			return false;
		}
    }
	cout << "Handshake successfully!" << endl;
    return true;
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
		if (header[4] == 0b1000) {
			memcpy(filename, recvBuf + HEADERSIZE, FILE_NAME_MAX_LENGTH);
		}

		// 接收文件大小
		recvResult = recvfrom(recvSocket, recvBuf, PACKETSIZE, 0, (SOCKADDR*)&sendAddr, &len);

		// 提取header
		memcpy(header, recvBuf, HEADERSIZE);

		// 提取文件大小
		if (header[4] == 0b10000) {
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
	sendAddr.sin_family = AF_INET;
    sendAddr.sin_port = htons(PORT);
    sendAddr.sin_addr.s_addr = inet_addr(IP);

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