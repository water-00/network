#include <stdio.h>
#include <iostream>
#include <winsock2.h>
#include <Windows.h>
#include <WS2tcpip.h>
#include <fstream>
#include <thread>
#include <chrono>
#include <random>

using namespace std;

// #pragma comment(lib, "ws2_32.lib")

#define PORT 15000
#define IP "127.0.0.1"
#define PACKETSIZE 1500
#define HEADERSIZE 14
#define DATASIZE (PACKETSIZE-HEADERSIZE)
#define FILE_NAME_MAX_LENGTH 64
#define DISCARD_RATE 0.02 // 丢包率
#define DELAY_TIME 110 // 延时时间（单位：ms）
#define DELAY_RATE 0 // 发生延时的概率

// 一些header中的标志位
#define SEQ_BITS_START 0
#define ACK_BITS_START 4
#define FLAG_BIT_POSITION 8
#define DATA_LENGTH_BITS_START 10
#define CHECKSUM_BITS_START 12

WSAData wsd;
SOCKET recvSocket = INVALID_SOCKET;
SOCKADDR_IN recvAddr = {0}; // 接收端地址
SOCKADDR_IN sendAddr = {0}; // 发送端地址
int len = sizeof(sendAddr);
char header[HEADERSIZE] = {0};

u_short checkSum(const char* input, int length) {
	int count = (length + 1) / 2; // 有多少组16 bits
	u_short* buf = new u_short[count]{0};
	for (int i = 0; i < count; i++) {
		buf[i] = (u_char)input[2 * i] + ((2 * i + 1 < length) ? (u_char)input[2 * i + 1] << 8 : 0); 
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
    return ~(sum & 0xFFFF);
}

bool handshake() {
	u_short checksum = 0;
	char recvBuf[HEADERSIZE] = {0};
    int recvResult = 0;
	int seq, ack;

	// 接受第一次握手请求报文
    while(true) {
        recvResult = recvfrom(recvSocket, recvBuf, HEADERSIZE, 0, (SOCKADDR*)&sendAddr, &len);
		// 检查checksum
		checksum = checkSum(recvBuf, HEADERSIZE);
		// 提取seq of message 1
		seq = (u_char)recvBuf[SEQ_BITS_START] + ((u_char)recvBuf[SEQ_BITS_START + 1] << 8) 
			+ ((u_char)recvBuf[SEQ_BITS_START + 2] << 16) + ((u_char)recvBuf[SEQ_BITS_START + 3] << 24);

		if (checksum == 0 && recvBuf[FLAG_BIT_POSITION] == 0b010) {
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
	header[ACK_BITS_START] = (u_char)(ack & 0xFF);
	header[ACK_BITS_START + 1] = (u_char)(ack >> 8);
	header[ACK_BITS_START + 2] = (u_char)(ack >> 16);
	header[ACK_BITS_START + 3] = (u_char)(ack >> 24);
	// 设置seq位
	seq = rand() % 65535;
	header[SEQ_BITS_START] = (u_char)(seq & 0xFF);
	header[SEQ_BITS_START + 1] = (u_char)(seq >> 8);
	header[SEQ_BITS_START + 2] = (u_char)(seq >> 16);
	header[SEQ_BITS_START + 3] = (u_char)(seq >> 24);
	// 设置ACK SYN位
	header[FLAG_BIT_POSITION] = 0b110;
    sendto(recvSocket, header, HEADERSIZE, 0, (SOCKADDR*)&sendAddr, sizeof(SOCKADDR));
    cout << "send the Second Handshake message!" << endl;

	// 接受第三次握手请求报文
	while(true) {
		recvResult = recvfrom(recvSocket, recvBuf, HEADERSIZE, 0, (SOCKADDR*)&sendAddr, &len);
		// 检查checksum
		checksum = checkSum(recvBuf, HEADERSIZE);
		// cout << "checksum = " << checksum << endl;
		// 提取ack of message 3
		ack = (u_char)recvBuf[ACK_BITS_START] + ((u_char)recvBuf[ACK_BITS_START + 1] << 8) 
			+ ((u_char)recvBuf[ACK_BITS_START + 2] << 16) + ((u_char)recvBuf[ACK_BITS_START + 3] << 24);

		if (checksum == 0 && ack == seq + 1 && recvBuf[FLAG_BIT_POSITION] == 0b100) {
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

void wavehand() {
	u_short checksum = 0;
	char recvBuf[HEADERSIZE] = {0};
    int recvResult = 0;
	int seq, ack;
	// 接收第一次挥手请求报文，在recvfile()中已经接收了

	// 对面的recvRespondThread()线程的THREAD_END已经被置为true了，但是线程内部仍然阻塞在recvfrom()函数上
	// 所以先随便发个有标志位的包让recvRespondThread()退出
	header[FLAG_BIT_POSITION] = 0b001;
	sendto(recvSocket, header, HEADERSIZE, 0, (SOCKADDR*)&sendAddr, sizeof(SOCKADDR));


	// 发送第二次挥手应答报文
	// 设置ack位
	ack = (u_char)header[SEQ_BITS_START] + ((u_char)header[SEQ_BITS_START + 1] << 8) 
		+ ((u_char)header[SEQ_BITS_START + 2] << 16) + ((u_char)header[SEQ_BITS_START + 3] << 24) + 1;
	header[ACK_BITS_START] = (u_char)(ack & 0xFF);
	header[ACK_BITS_START + 1] = (u_char)(ack >> 8);
	header[ACK_BITS_START + 2] = (u_char)(ack >> 16);
	header[ACK_BITS_START + 3] = (u_char)(ack >> 24);
	// 设置seq位
	seq = rand();
	header[SEQ_BITS_START] = (u_char)(seq & 0xFF);
	header[SEQ_BITS_START + 1] = (u_char)(seq >> 8);
	header[SEQ_BITS_START + 2] = (u_char)(seq >> 16);
	header[SEQ_BITS_START + 3] = (u_char)(seq >> 24);
	// 设置ACK位
	header[FLAG_BIT_POSITION] = 0b100;
    sendto(recvSocket, header, HEADERSIZE, 0, (SOCKADDR*)&sendAddr, sizeof(SOCKADDR));
    cout << "send the Second Wavehand message!" << endl;

	// 发送第三次挥手请求报文
	// 设置seq位
	seq = rand();
	header[SEQ_BITS_START] = (u_char)(seq & 0xFF);
	header[SEQ_BITS_START + 1] = (u_char)(seq >> 8);
	header[SEQ_BITS_START + 2] = (u_char)(seq >> 16);
	header[SEQ_BITS_START + 3] = (u_char)(seq >> 24);
	// ack和上一个报文一样
	// 设置ACK FIN位
	header[FLAG_BIT_POSITION] = 0b101;
    sendto(recvSocket, header, HEADERSIZE, 0, (SOCKADDR*)&sendAddr, sizeof(SOCKADDR));
    cout << "send the Third Wavehand message!" << endl;

	// 接收第四次挥手应答报文
	while(true) {
        recvResult = recvfrom(recvSocket, recvBuf, HEADERSIZE, 0, (SOCKADDR*)&sendAddr, &len);
		// 检查checksum
		checksum = checkSum(recvBuf, HEADERSIZE);
		// 提取ack of message 4
		ack = (u_char)recvBuf[ACK_BITS_START] + ((u_char)recvBuf[ACK_BITS_START + 1] << 8) 
			+ ((u_char)recvBuf[ACK_BITS_START + 2] << 16) + ((u_char)recvBuf[ACK_BITS_START + 3] << 24);
		if (checksum == 0 && recvBuf[FLAG_BIT_POSITION] == 0b100) {
			cout << "successfully received the Forth Wavehand message!" << endl;
			break;
		} else {
			cout << "failed to received the correct Forth Wavehand message, Handshake failed!" << endl;
			return;
		}
    }

	cout << "Wavehand successfully!" << endl;
    return;
}

void recvfile() {
	char recvBuf[PACKETSIZE] = {0}; // header + data
	char dataSegment[DATASIZE] = {0};
	char filename[FILE_NAME_MAX_LENGTH] = {0};
	int filesize = 0;
	int recvResult = 0; // 接受的packet总长度
	
    // 用生成随机数模仿丢包率
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0, 1);

	while(true) {
		// 接收文件名
		recvResult = recvfrom(recvSocket, recvBuf, PACKETSIZE, 0, (SOCKADDR*)&sendAddr, &len);
		
		// 检查是否是挥手报文
		if (recvBuf[FLAG_BIT_POSITION] == 0b101) {
			// 记录一下seq
			for (int i = 0; i < 4; i++) {
				header[SEQ_BITS_START + i] = recvBuf[SEQ_BITS_START + i];
			}
			cout << "successfully received the Fisrt Wavehand message!" << endl;
			wavehand();
			return;
		}

		// 检查是否是文件名
		if (recvBuf[FLAG_BIT_POSITION] != 0b1000) {
			continue; // 因为可能会收到在send端结束发送后的还在路上的报文，过滤掉它们
		}

		// 提取header
		memcpy(header, recvBuf, HEADERSIZE);
		
		// 提取文件名
		if (header[FLAG_BIT_POSITION] == 0b1000) {
			memcpy(filename, recvBuf + HEADERSIZE, FILE_NAME_MAX_LENGTH);
		}

		// 接收文件大小
		recvResult = recvfrom(recvSocket, recvBuf, PACKETSIZE, 0, (SOCKADDR*)&sendAddr, &len);

		// 提取header
		memcpy(header, recvBuf, HEADERSIZE);

		// 提取文件大小
		if (header[FLAG_BIT_POSITION] == 0b10000) {
			filesize = atoi(recvBuf + HEADERSIZE);
		}
		cout << "begin to receive a file, filename: " << filename << ", filesize: " << filesize << " bytes." << endl;

		// 接收文件内容
		int hasReceived = 0; // 已接收字节数
		int seq_opp = 0, ack_opp = 0; // 对方发送报文中的seq, ack
		int seq = 0, ack = 0; // 要发送的响应报文中的seq, ack
		int expectedSeq = ack + 1; // 期待收到的packet seq
		int dataLength = 0; // 接收到的数据段长度(= recvResult - HEADERSIZE)
		u_short checksum = 0; // 校验和（为0时正确）

		ofstream out;
		out.open(filename, ios::out | ios::binary | ios::app);	
		while (true) {
			expectedSeq = ack + 1; // receive端唯一一个接收窗口

			memset(recvBuf, 0, PACKETSIZE);
			// memset(header, 0, HEADERSIZE);
			recvResult = recvfrom(recvSocket, recvBuf, PACKETSIZE, 0, (SOCKADDR*)&sendAddr, &len);
		    if (recvResult == SOCKET_ERROR) {
                cout << "receive error! sleep!" << endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
				continue;
            }

			// 检查校验和 and ACK位
			checksum = checkSum(recvBuf, recvResult);
			if (checksum == 0 && recvBuf[FLAG_BIT_POSITION] == 0b100) {
				seq_opp = (u_char)recvBuf[SEQ_BITS_START] + ((u_char)recvBuf[SEQ_BITS_START + 1] << 8) 
						+ ((u_char)recvBuf[SEQ_BITS_START + 2] << 16) + ((u_char)recvBuf[SEQ_BITS_START + 3] << 24);
				ack_opp = (u_char)recvBuf[ACK_BITS_START] + ((u_char)recvBuf[ACK_BITS_START + 1] << 8) 
						+ ((u_char)recvBuf[ACK_BITS_START + 2] << 16) + ((u_char)recvBuf[ACK_BITS_START + 3] << 24);
				if (seq_opp == expectedSeq) { // 检查收到的包的seq(即seq_opp)是否是期待的seq
					// 如果收到了正确的包，那就提取内容 + 回复
					dataLength = recvResult - HEADERSIZE;
					// 提取数据
					memcpy(dataSegment, recvBuf + HEADERSIZE, dataLength);
					out.write(dataSegment, dataLength);

					// 设置seq位，本协议中为了确认方便，就把响应报文的seq置为收到报文的seq
					seq = seq_opp;
					header[SEQ_BITS_START] = (u_char)(seq & 0xFF);
					header[SEQ_BITS_START + 1] = (u_char)(seq >> 8);
					header[SEQ_BITS_START + 2] = (u_char)(seq >> 16);
					header[SEQ_BITS_START + 3] = (u_char)(seq >> 24);
					// 设置ack位, = seq_opp，表示确认接收到了这之前的全部内容，并期待收到这之后的内容
					ack = seq_opp;
					header[ACK_BITS_START] = (u_char)(ack & 0xFF);
					header[ACK_BITS_START + 1] = (u_char)(ack >> 8);
					header[ACK_BITS_START + 2] = (u_char)(ack >> 16);
					header[ACK_BITS_START + 3] = (u_char)(ack >> 24);
					// 设置ACK位
					header[FLAG_BIT_POSITION] = 0b100;
					// 响应报文中的data length为0，就不用设置了

					hasReceived += recvResult - HEADERSIZE;		
					cout << "has received " << hasReceived << " bytes, ack = " << ack << endl;

					// 模拟延时
					if (dis(gen) < DELAY_RATE)
						std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_TIME));

					// 模拟丢包
					if (dis(gen) > DISCARD_RATE)
				    	sendto(recvSocket, header, HEADERSIZE, 0, (SOCKADDR*)&sendAddr, sizeof(SOCKADDR));
				} else {
					// 说明网络异常，丢了包或者有延迟，所以不用更改，直接重发收到的最新包的ack即可

					// 模拟延时
					if (dis(gen) < DELAY_RATE)
						std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_TIME));

					// 模拟丢包
					if (dis(gen) > DISCARD_RATE)
				    	sendto(recvSocket, header, HEADERSIZE, 0, (SOCKADDR*)&sendAddr, sizeof(SOCKADDR));

					cout << "Don't received the expected packet! Expected seq = " << expectedSeq << ". Received seq = " << seq_opp << endl;
				}
			} else {
				// 校验和或ACK位异常，重发上一个包的ack
				// TODO: 再用函数封装一下
				cout << "checksum ERROR or ACK ERROR!" << endl;
				continue;
			}

			if (hasReceived == filesize) {
				cout << "receive file " << filename << " successfully! total " << hasReceived << " bytes." << endl;
				out.close();
				cout << "Ready to receive the next file!" << endl;
				break;
			}
		}
	}
}

int main() {
	if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
		cout << "WSAStartup error = " << WSAGetLastError() << endl;
		exit(1);
	}
	else {
		cout << "start success" << endl;
	}

	recvSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (recvSocket == SOCKET_ERROR) {
		cout << "socket error = " << WSAGetLastError() << endl;
		closesocket(recvSocket);
		WSACleanup();
		exit(1);
	}
	else {
		cout << "socket success" << endl;
	}

	recvAddr.sin_family = AF_INET; // 协议版本
	recvAddr.sin_addr.S_un.S_addr = inet_addr(IP); // ip地址，inet_addr把数点格式转换为整数
	recvAddr.sin_port = htons(PORT); // 端口号，0-65535
	sendAddr.sin_family = AF_INET;
    sendAddr.sin_port = htons(PORT);
    sendAddr.sin_addr.s_addr = inet_addr(IP);

	if (bind(recvSocket, (SOCKADDR*)&recvAddr, sizeof(recvAddr)) == SOCKET_ERROR) {
		cout << "bind error = " << WSAGetLastError() << endl;
		closesocket(recvSocket);
		WSACleanup();
		exit(1);
	}
	else {
		cout << "bind success" << endl;
	}

    if (handshake()) {
		thread recvfile_thread (recvfile);
        recvfile_thread.join();
    }

    closesocket(recvSocket);
    WSACleanup();
    return 0;
}