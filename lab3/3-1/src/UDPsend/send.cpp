#include <stdio.h>
#include <iostream>
#include <winsock2.h>
#include <Windows.h>
#include <WS2tcpip.h>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>

using namespace std;

#define PORT 15000
#define IP "127.0.0.1"
#define PACKETSIZE 1500
#define HEADERSIZE 10
#define DATASIZE (PACKETSIZE-HEADERSIZE)
#define FILE_NAME_MAX_LENGTH 64

WSAData wsd;
SOCKET sendSocket = INVALID_SOCKET;
SOCKADDR_IN recvAddr = {0}; // 接收端地址
SOCKADDR_IN sendAddr = {0}; // 发送端地址
int len = sizeof(recvAddr);


// 伪首部10 byte，约定：
// 0 1--16位seq（0--低8位，1--高8位，下同）
// 2 3--16位ack
// 4--标志位，低三位分别代表ACK SYN FIN，第四位、第五位暂时起测试功能，代表此次发送的是文件名、文件大小
// 5--空着，全0
// 6 7--数据部分长度
// 8 9--校验和
u_char header[HEADERSIZE] = {0};

char dataSegment[DATASIZE] = {0}; // 报文数据部分

char sendBuf[PACKETSIZE] = {0}; // header + data

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
    // delete buf
    return ~(sum & 0xFFFF);
    return 0xFFFF;
}

bool handshake() {
    u_short checksum = 0;

    // 发送第一次握手请求报文
    memset(header, 0, HEADERSIZE);
    // 设置seq位
    int seq = rand() % 65535;
    header[0] = (u_char)(seq & 0xFF);
    header[1] = (u_char)(seq >> 8);
    // 设置SYN位
    header[4] = 0b010; // SYN在header[4]的第二位，所以这一行表示SYN == 1
    checksum = checkSum(header, HEADERSIZE);

    // 设置checksum位
    header[8] = (u_char)(checksum & 0xFF);
    header[9] = (u_char)(checksum >> 8);
    sendto(sendSocket, (char*)header, HEADERSIZE, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
    cout << "send the First Handshake message!" << endl;

    // 接受第二次握手应答报文
    char recvBuf[HEADERSIZE] = {0};
    int recvResult = 0;
    while(true) {
        recvResult = recvfrom(sendSocket, recvBuf, HEADERSIZE, 0, (SOCKADDR*)&recvAddr, &len);
        // 接受ack
        int ack = recvBuf[2] + (recvBuf[3] << 8);
        if ((ack == seq + 1) && (recvBuf[4] == 0b110)) { // 0b110代表ACK SYN FIN == 110
            cout << "successfully received the Second Handshake message!" << endl;
            break;
        } else {
            cout << "failed to received the correct Second Handshake message, Handshake failed!" << endl;
            return false;
        }
    }

    // 发送第三次握手请求报文
    memset(header, 0, HEADERSIZE);
    // 设置ack位，ack = seq of message2 + 1
    int ack = recvBuf[0] + (recvBuf[1] << 8) + 1;
    header[2] = ack & 0xFF;
    header[3] = ack >> 8;
    // 设置ACK位
    header[4] = 0b100;
    checksum = checkSum(header, HEADERSIZE);
    // 设置checksum位
    header[8] = (u_char)(checksum & 0xFF);
    header[9] = (u_char)(checksum >> 8);
    sendto(sendSocket, (char*)header, HEADERSIZE, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
    cout << "send the Third Handshake message!" << endl;

    cout << "Handshake successfully!" << endl;
    return true;

}
void sendfile(const char* filename) {
    // 读入文件
    ifstream is(filename, ifstream::in | ios::binary);
    is.seekg(0, is.end);
    int fileSize = is.tellg();
    is.seekg(0, is.beg);
    char* filebuf;
    filebuf = (char*)calloc(fileSize, sizeof(char));
    is.read(filebuf, fileSize);
    is.close();

    // 发送文件名
    memset(sendBuf, 0, PACKETSIZE);
    header[4] = 0b1000;
    strcat((char*)memcpy(sendBuf, header, HEADERSIZE) + HEADERSIZE, filename);
	// sendto(sendSocket, sendBuf, strlen(sendBuf), 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
    sendto(sendSocket, sendBuf, PACKETSIZE, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));

    // 发送文件大小
    memset(sendBuf, 0, PACKETSIZE);
    header[4] = 0b10000;
    strcat((char*)memcpy(sendBuf, header, HEADERSIZE) + HEADERSIZE, to_string(fileSize).c_str());
	// sendto(sendSocket, sendBuf, strlen(sendBuf), 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR)); strlen(sendBuf) = 0...因为sendBuf第一个字符就是0
    sendto(sendSocket, sendBuf, PACKETSIZE, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));

    // 发送文件
    int hasSent = 0; // 已发送的文件大小
    int sendResult = 0; // 每次sendto函数的返回结果
    int sendSize = 0; // 每次实际发送的报文长度
    while(true) {
        // 初始化头部和数据段
        memset(header, 0, HEADERSIZE);
        memset(dataSegment, 0, DATASIZE);
        memset(sendBuf, 0, PACKETSIZE);

        // 设置本次发送长度
        sendSize = min(PACKETSIZE, fileSize - hasSent + HEADERSIZE);

        // file->dataSegment
        memcpy(dataSegment, filebuf + hasSent, sendSize - HEADERSIZE);
        
        // header->sendBuf
        memcpy(sendBuf, header, HEADERSIZE);

        // dataSegment->sendBuf（从sendBuf[10]开始）
        memcpy(sendBuf + HEADERSIZE, dataSegment, sendSize - HEADERSIZE);

		sendResult = sendto(sendSocket, sendBuf, sendSize, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
        std::this_thread::sleep_for(std::chrono::microseconds(500));

        hasSent += sendSize - HEADERSIZE;
        if (hasSent == fileSize) {
            cout << "send successfully, send " << fileSize << " bytes." << endl;
            break;
        }
    }
}

void wavehand() {

}

int main() {
	if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
		cout << "WSAStartup Error = " << WSAGetLastError() << endl;
		exit(1);
	}
	else {
		cout << "start Success" << endl;
	}

    sendSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sendSocket == SOCKET_ERROR) {
		cout << "socket Error = " << WSAGetLastError() << endl;
		closesocket(sendSocket);
		WSACleanup();
		exit(1);
	}
	else {
		cout << "socket Success" << endl;
	}

    recvAddr.sin_family = AF_INET;
    recvAddr.sin_port = htons(PORT);
    recvAddr.sin_addr.s_addr = inet_addr(IP);
    sendAddr.sin_family = AF_INET;
    sendAddr.sin_port = htons(PORT);
    sendAddr.sin_addr.s_addr = inet_addr(IP);

    if (handshake()) {
        while(true) {
            string str;
            cout << "please input the file name(or q to quit sending): ";
            cin >> str;
            if (str == "q") {
                break;
            } else {
                thread sendfile_thread (sendfile, str.c_str());
                sendfile_thread.join();
                // sendfile(str.c_str());
            }
        }
        wavehand();
    }

    closesocket(sendSocket);
    WSACleanup();
    return 0;
}