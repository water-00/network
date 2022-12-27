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
#define HEADERSIZE 14
#define DATASIZE (PACKETSIZE-HEADERSIZE)
#define FILE_NAME_MAX_LENGTH 64

// 一些header中的标志位
#define SEQ_BITS_START 0
#define ACK_BITS_START 4
#define FLAG_BIT_POSITION 8
#define DATA_LENGTH_BITS_START 10
#define CHECKSUM_BITS_START 12

WSAData wsd;
SOCKET sendSocket = INVALID_SOCKET;
SOCKADDR_IN recvAddr = {0}; // 接收端地址
SOCKADDR_IN sendAddr = {0}; // 发送端地址
int len = sizeof(recvAddr);

clock_t s, l;
int totalLength = 0;
double totalTime = 0;

// 伪首部14 byte，约定：
// 0 1 2 3--32位seq（0--低8位，3--高8位，下同）
// 4 5 6 7--32位ack
// 8--标志位，低三位分别代表ACK SYN FIN，第四位、第五位暂时起测试功能，代表此次发送的是文件名、文件大小
// 9--空着，全0
// 10 11--数据部分长度
// 12 13--校验和
char header[HEADERSIZE] = {0};

char dataSegment[DATASIZE] = {0}; // 报文数据部分

char sendBuf[PACKETSIZE] = {0}; // header + data

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

    // 发送第一次握手请求报文
    memset(header, 0, HEADERSIZE);
    // 设置seq位
    int seq = rand();
    header[SEQ_BITS_START] = (u_char)(seq & 0xFF);
    header[SEQ_BITS_START + 1] = (u_char)(seq >> 8);
    header[SEQ_BITS_START + 2] = (u_char)(seq >> 16);
    header[SEQ_BITS_START + 3] = (u_char)(seq >> 24);
    // 设置SYN位
    header[FLAG_BIT_POSITION] = 0b010; // SYN在header[8]的第二位，所以这一行表示SYN == 1
    checksum = checkSum(header, HEADERSIZE);
    // 设置checksum位
    header[CHECKSUM_BITS_START] = (u_char)(checksum & 0xFF);
    header[CHECKSUM_BITS_START + 1] = (u_char)(checksum >> 8);
    sendto(sendSocket, header, HEADERSIZE, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
    cout << "send the First Handshake message!" << endl;

    // 接受第二次握手应答报文
    char recvBuf[HEADERSIZE] = {0};
    int recvResult = 0;
    while(true) {
        recvResult = recvfrom(sendSocket, recvBuf, HEADERSIZE, 0, (SOCKADDR*)&recvAddr, &len);
        // 接受ack
        int ack = recvBuf[ACK_BITS_START] + (recvBuf[ACK_BITS_START + 1] << 8) 
                + (recvBuf[ACK_BITS_START + 2] << 16) + (recvBuf[ACK_BITS_START + 3] << 24);
        if ((ack == seq + 1) && (recvBuf[FLAG_BIT_POSITION] == 0b110)) { // 0b110代表ACK SYN FIN == 110
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
    int ack = (u_char)recvBuf[SEQ_BITS_START] + ((u_char)recvBuf[SEQ_BITS_START + 1] << 8)
            + ((u_char)recvBuf[SEQ_BITS_START + 2] << 16) + ((u_char)recvBuf[SEQ_BITS_START + 3] << 24) + 1;
    header[ACK_BITS_START] = (u_char)(ack & 0xFF);
    header[ACK_BITS_START + 1] = (u_char)(ack >> 8);
    header[ACK_BITS_START + 2] = (u_char)(ack >> 16);
    header[ACK_BITS_START + 3] = (u_char)(ack >> 24);
    // 设置ACK位
    header[FLAG_BIT_POSITION] = 0b100;
    checksum = checkSum(header, HEADERSIZE);
    // 设置checksum位
    header[CHECKSUM_BITS_START] = (u_char)(checksum & 0xFF);
    header[CHECKSUM_BITS_START + 1] = (u_char)(checksum >> 8);
    sendto(sendSocket, header, HEADERSIZE, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
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
    header[FLAG_BIT_POSITION] = 0b1000;
    strcat((char*)memcpy(sendBuf, header, HEADERSIZE) + HEADERSIZE, filename);
    sendto(sendSocket, sendBuf, PACKETSIZE, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));

    // 发送文件大小
    memset(sendBuf, 0, PACKETSIZE);
    header[FLAG_BIT_POSITION] = 0b10000;
    strcat((char*)memcpy(sendBuf, header, HEADERSIZE) + HEADERSIZE, to_string(fileSize).c_str());
    sendto(sendSocket, sendBuf, PACKETSIZE, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));

    int hasSent = 0; // 已发送的文件大小
    int sendResult = 0; // 每次sendto函数的返回结果
    int sendSize = 0; // 每次实际发送的报文总长度
    int seq = 0, ack = 0; // 发送包时的seq, ack
    int seq_opp = 0, ack_opp = 0; // 收到的对面的seq, ack
    int dataLength = 0; // 每次实际发送的数据部分长度(= sendSize - HEADERSIZE)
    u_short checksum = 0; // 校验和
    bool resend = false; // 重传标志
    char recvBuf[HEADERSIZE] = {0}; // 接受响应报文的缓冲区
    int recvResult = 0; // 接受响应报文的返回值

    // 发送文件
    while(true) {
        // 初始化头部和数据段
        memset(header, 0, HEADERSIZE);
        memset(dataSegment, 0, DATASIZE);
        memset(sendBuf, 0, PACKETSIZE);        
        // 设置本次发送长度
        sendSize = min(PACKETSIZE, fileSize - hasSent + HEADERSIZE);

        if (!resend) {
            // 如果不是重传，需要设置header
            // seq = 收到的包的ack，表示接下来要发的字节位置
            // ack = 收到的包的seq + 收到的data length（然而receive方的data length永远为0）
            // 设置seq位
            seq = ack_opp;
            header[SEQ_BITS_START] = (u_char)(seq & 0xFF);
            header[SEQ_BITS_START + 1] = (u_char)(seq >> 8);
            header[SEQ_BITS_START + 2] = (u_char)(seq >> 16);
            header[SEQ_BITS_START + 3] = (u_char)(seq >> 24);
            // 设置ack位
            ack = seq_opp;
            header[ACK_BITS_START] = (u_char)(ack & 0xFF);
            header[ACK_BITS_START + 1] = (u_char)(ack >> 8);
            header[ACK_BITS_START + 2] = (u_char)(ack >> 16);
            header[ACK_BITS_START + 3] = (u_char)(ack >> 24);
            // 设置ACK位
            header[FLAG_BIT_POSITION] = 0b100;
            // 设置data length位
            dataLength = sendSize - HEADERSIZE;
            header[DATA_LENGTH_BITS_START] = dataLength & 0xFF;
            header[DATA_LENGTH_BITS_START + 1] = dataLength >> 8;

            // file中此次要被发送的数据->dataSegment
            memcpy(dataSegment, filebuf + hasSent, sendSize - HEADERSIZE);
            // header->sendBuf
            memcpy(sendBuf, header, HEADERSIZE);
            // dataSegment->sendBuf（从sendBuf[10]开始）
            memcpy(sendBuf + HEADERSIZE, dataSegment, sendSize - HEADERSIZE);
            // 设置checksum位
            checksum = checkSum(sendBuf, sendSize);
            header[CHECKSUM_BITS_START] = sendBuf[CHECKSUM_BITS_START] = checksum & 0xFF;
            header[CHECKSUM_BITS_START + 1] = sendBuf[CHECKSUM_BITS_START + 1] = checksum >> 8;

            sendResult = sendto(sendSocket, sendBuf, sendSize, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
        } else {
            // 如果是重传，不需要设置header，再发一次即可
            sendResult = sendto(sendSocket, sendBuf, sendSize, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
        }

        // 发完packet后接受响应报文
        while (true) {
            // TODO: 如果超时还没收到响应报文，则break并重传
            recvResult = recvfrom(sendSocket, recvBuf, HEADERSIZE, 0, (SOCKADDR*)&recvAddr, &len);
            if (recvResult == SOCKET_ERROR) {
                cout << "receive error! sleep!" << endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                continue;
            }
            seq_opp = (u_char)recvBuf[SEQ_BITS_START] + ((u_char)recvBuf[SEQ_BITS_START + 1] << 8)
                    + ((u_char)recvBuf[SEQ_BITS_START + 2] << 16) + ((u_char)recvBuf[SEQ_BITS_START + 3] << 24);
            ack_opp = (u_char)recvBuf[ACK_BITS_START] + ((u_char)recvBuf[ACK_BITS_START + 1] << 8)
                    + ((u_char)recvBuf[ACK_BITS_START + 2] << 16) + ((u_char)recvBuf[ACK_BITS_START + 3] << 24);

            if (recvBuf[FLAG_BIT_POSITION] == 0b100 && seq == seq_opp) { 
                // 因为接收方没有要发送的数据，所以响应报文里的seq按传统TCP/UDP协议来说一直为0，但在我的协议里把响应报文的seq置成发送报文的seq，方便确认
                // 对方正确收到了这个packet
                resend = false;
                hasSent += sendSize - HEADERSIZE;
                // cout << "send seq = " << seq << " packet successfully!" << endl;
                break;
            } else {
                resend = true;
                cout << "failed to send seq = " << seq << " packet! This packet will be resent." << endl;
                break;
            }
        }
        // std::this_thread::sleep_for(std::chrono::microseconds(500));

        if (hasSent == fileSize) {
            cout << "send successfully, send " << fileSize << " bytes." << endl;
            totalLength += fileSize;
            break;
        }
    }
}

void wavehand() {
    int seq = 0, ack = 0;
    u_short checksum = 0;
    // 发送第一次挥手请求报文
    memset(header, 0, HEADERSIZE);
    // 设置seq位
    seq = rand();
    header[SEQ_BITS_START] = (u_char)(seq & 0xFF);
    header[SEQ_BITS_START + 1] = (u_char)(seq >> 8);
    header[SEQ_BITS_START + 2] = (u_char)(seq >> 16);
    header[SEQ_BITS_START + 3] = (u_char)(seq >> 24);
    // 设置ACK FIN位
    header[FLAG_BIT_POSITION] = 0b101;
    checksum = checkSum(header, HEADERSIZE);
    // 设置checksum位
    header[CHECKSUM_BITS_START] = (u_char)(checksum & 0xFF);
    header[CHECKSUM_BITS_START + 1] = (u_char)(checksum >> 8);
    sendto(sendSocket, header, HEADERSIZE, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
    cout << "send the First Wavehand message!" << endl;

    // 接收第二次挥手应答报文
    char recvBuf[HEADERSIZE] = {0};
    int recvResult = 0;
    while(true) {
        recvResult = recvfrom(sendSocket, recvBuf, HEADERSIZE, 0, (SOCKADDR*)&recvAddr, &len);
        // 接受ack
        ack = recvBuf[ACK_BITS_START] + (recvBuf[ACK_BITS_START + 1] << 8) 
            + (recvBuf[ACK_BITS_START + 2] << 16) + (recvBuf[ACK_BITS_START + 3] << 24);
        if ((ack == seq + 1) && (recvBuf[FLAG_BIT_POSITION] == 0b100)) {
            cout << "successfully received the Second Wavehand message!" << endl;
            break;
        } else {
            cout << "failed to received the correct Second Wavehand message, Wavehand failed!" << endl;
            return;
        }
    }

    // 接收第三次挥手请求报文
    while(true) {
        recvResult = recvfrom(sendSocket, recvBuf, HEADERSIZE, 0, (SOCKADDR*)&recvAddr, &len);
        // 接受ack
        ack = recvBuf[ACK_BITS_START] + (recvBuf[ACK_BITS_START + 1] << 8) 
            + (recvBuf[ACK_BITS_START + 2] << 16) + (recvBuf[ACK_BITS_START + 3] << 24);
        if ((ack == seq + 1) && (recvBuf[FLAG_BIT_POSITION] == 0b101)) {
            cout << "successfully received the Third Wavehand message!" << endl;
            break;
        } else {
            cout << "failed to received the correct Third Wavehand message, Wavehand failed!" << endl;
            return;
        }
    }

    // 发送第四次挥手应答报文
    memset(header, 0, HEADERSIZE);
    // 设置seq位
    seq = ack;
    header[SEQ_BITS_START] = (u_char)(seq & 0xFF);
    header[SEQ_BITS_START + 1] = (u_char)(seq >> 8);
    header[SEQ_BITS_START + 2] = (u_char)(seq >> 16);
    header[SEQ_BITS_START + 3] = (u_char)(seq >> 24);
    // 设置ack位
    ack = (u_char)recvBuf[ACK_BITS_START] + ((u_char)recvBuf[ACK_BITS_START + 1] << 8) 
        + ((u_char)recvBuf[ACK_BITS_START + 2] << 16) + ((u_char)recvBuf[ACK_BITS_START + 3] << 24) + 1;
    header[ACK_BITS_START] = (u_char)(ack & 0xFF);
    header[ACK_BITS_START + 1] = (u_char)(ack >> 8);
    header[ACK_BITS_START + 2] = (u_char)(ack >> 16);
    header[ACK_BITS_START + 3] = (u_char)(ack >> 24);
    // 设置ACK位
    header[FLAG_BIT_POSITION] = 0b100;
    checksum = checkSum(header, HEADERSIZE);
    // 设置checksum位
    header[CHECKSUM_BITS_START] = (u_char)(checksum & 0xFF);
    header[CHECKSUM_BITS_START + 1] = (u_char)(checksum >> 8);
    sendto(sendSocket, header, HEADERSIZE, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
    cout << "send the Forth Wavehand message!" << endl;

    cout << "Wavehand successfully!" << endl;
    return;
}

int main() {
	if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
		cout << "WSAStartup error = " << WSAGetLastError() << endl;
		exit(1);
	}
	else {
		cout << "start success" << endl;
	}

    sendSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sendSocket == SOCKET_ERROR) {
		cout << "socket error = " << WSAGetLastError() << endl;
		closesocket(sendSocket);
		WSACleanup();
		exit(1);
	}
	else {
		cout << "socket success" << endl;
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
                s = clock();
                thread sendfile_thread(sendfile, str.c_str());
                sendfile_thread.join();
                l = clock();
                totalTime += (double)(l - s) / CLOCKS_PER_SEC;
            }
        }
        wavehand();
        cout << endl << "send time: " << totalTime << " s." << endl;
        cout << "total size: " << totalLength << " Bytes." << endl;
        cout << "throughput: " << (double)((totalLength * 8  / 1000) / totalTime) << " kbps." << endl;
    }

    closesocket(sendSocket);
    WSACleanup();
    return 0;
}