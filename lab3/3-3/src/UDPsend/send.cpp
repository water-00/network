#include <stdio.h>
#include <iostream>
#include <winsock2.h>
#include <Windows.h>
#include <WS2tcpip.h>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <random>

using namespace std;

#define PORT 15000
#define IP "127.0.0.1"
#define PACKETSIZE 1500
#define HEADERSIZE 14
#define DATASIZE (PACKETSIZE-HEADERSIZE)
#define FILE_NAME_MAX_LENGTH 64
#define DISCARD_RATE 0.02 // 丢包率
#define DELAY_TIME 50 // 延时（单位：ms）
#define TIMEOUT 100 // 超时时间（单位：ms）
#define TEST_STOPTIME 100 // send window 满后发送区等待的时间（单位：ms）

// 一些header中的标志位
#define SEQ_BITS_START 0
#define ACK_BITS_START 4
#define FLAG_BIT_POSITION 8
#define DATA_LENGTH_BITS_START 10
#define CHECKSUM_BITS_START 12
// #define SEND_WINDOW_SIZE 30 // 滑动窗口大小

// 慢启动与拥塞避免状态的标志位
#define SS_STATUS 1 // 慢启动
#define CA_STATUS 2 // 拥塞避免
#define QR_STATUS 3 // 快速恢复

WSAData wsd;
SOCKET sendSocket = INVALID_SOCKET;
SOCKADDR_IN recvAddr = {0}; // 接收端地址
SOCKADDR_IN sendAddr = {0}; // 发送端地址
int len = sizeof(recvAddr);

clock_t s, l; // 用于计算程序总发送速率的计时变量
clock_t start, last; // 用于检测是否超时的变量
int totalLength = 0; // 总发送字节数
double totalTime = 0; // 总发送时间

#define INIT_SW_SIZE 30 // 初始滑动窗口大小，也即初始threshold
double threshold = INIT_SW_SIZE;
double cwnd = 1;
int status = SS_STATUS; // 现在是SS, CA还是QR
int dup_ack_times = 0; // 用于检测三次重复

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

int hasSent = 0; // 已发送的文件大小
int fileSize = 0;
int sendResult = 0; // 每次sendto函数的返回结果
int sendSize = 0; // 每次实际发送的报文总长度
int seq = 1, ack = 0; // 发送包时的seq, ack
int base = 1; // 滑动窗口起始
int seq_opp = 0, ack_opp = 0; // 收到的对面的seq, ack
int dataLength = 0; // 每次实际发送的数据部分长度(= sendSize - HEADERSIZE)
u_short checksum = 0; // 校验和
bool resend = false; // 重传标志
char recvBuf[HEADERSIZE] = {0}; // 接受响应报文的缓冲区
int recvResult = 0; // 接受响应报文的返回值
bool finishSend = false; // 是否结束了一个文件的发送

bool THREAD_END = false; // 通过这个变量告诉recvRespondThread和timerThread退出
int THREAD_CREAT_FLAG = 1;
int index = 0; // 用于拯救receive发过来的最后一个确认包丢失，send端卡在hasSent == fileSize内的出不来的情况的变量

void timerThread() {
    while(!THREAD_END) {
        last = clock();
        if (last - start >= TIMEOUT) {
            // 如果超时，调整threshold和cwnd，进入SS
            threshold = cwnd / 2;
            cwnd = 1;
            status = SS_STATUS;
            dup_ack_times = 0;
            if (!finishSend)
                cout << "Timeout! Now threshold = " << threshold << ", cwnd = " << cwnd << " * MSS." << endl;

            start = clock();
            resend = true;
        }
    }
}


void recvRespondThread() {
    // 接受响应报文的线程
    while (!THREAD_END) {
        recvResult = recvfrom(sendSocket, recvBuf, HEADERSIZE, 0, (SOCKADDR*)&recvAddr, &len);
        if (recvResult == SOCKET_ERROR) {
            cout << "receive error! sleep!" << endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            continue;
        }

        if (recvBuf[FLAG_BIT_POSITION] == 0b001) {
            // 收到了挥手前让该线程退出的报文
            break;
        }

        seq_opp = (u_char)recvBuf[SEQ_BITS_START] + ((u_char)recvBuf[SEQ_BITS_START + 1] << 8)
                + ((u_char)recvBuf[SEQ_BITS_START + 2] << 16) + ((u_char)recvBuf[SEQ_BITS_START + 3] << 24);
        ack_opp = (u_char)recvBuf[ACK_BITS_START] + ((u_char)recvBuf[ACK_BITS_START + 1] << 8)
                + ((u_char)recvBuf[ACK_BITS_START + 2] << 16) + ((u_char)recvBuf[ACK_BITS_START + 3] << 24);

        if (recvBuf[FLAG_BIT_POSITION] == 0b100 && ack_opp >= base) { 
            // 对方正确收到了这个packet，则根据status增加cwnd，并移动滑动窗口的base到下一个需要确认的包
            base = ack_opp + 1;
            resend = false;
            index = 0;

            // 根据status改变cwnd
            if (status == SS_STATUS) {
                cwnd++;
                // 判断cwnd是否超过threshold，要不要进入CA
                if (cwnd > threshold) {
                    status = CA_STATUS;
                }
            } else if (status == CA_STATUS) {
                cwnd += 1 / ((cwnd > 1) ? floor(cwnd) : 1); // 这里这么写是避免除以0
            } else if (status == QR_STATUS) {
                status = CA_STATUS;
                cwnd = threshold;
            }

            dup_ack_times = 0;
            cout << "Having received the correct ack = " << ack_opp << ", now threshold = " << threshold << ", cwnd = " << cwnd << " * MSS." << endl;

            // 启动计时（实际上是重置计时器）
            start = clock();
        } else {
            // 如果接受到的ack < base，则等待收到三次重复的ack
            dup_ack_times++;
            cout << "dup_ack_times = " << dup_ack_times << endl;

            // 如果已经到了三次，则进入QR状态，并调整threshold, cwnd，之后进行重传
            if (status != QR_STATUS && dup_ack_times == 3) {
                status = QR_STATUS;
                threshold = cwnd / 2;
                cwnd = threshold + 3;

                resend = true;
                std:cout << "Enter the QR status!" << endl;
                // dup_ack_times = 0;
            } else if (status == QR_STATUS) {
                // 如果是在快速恢复阶段收到了重复ack，说明网络状况良好
                cwnd++;
            }

            cout << "Received the wrong ack = " << ack_opp << ", now threshold = " << threshold << ", cwnd = " << cwnd << " * MSS." << endl;
        }

        if (base == (fileSize / (PACKETSIZE - HEADERSIZE) + 2)) {
            // 发送完毕时的base
            finishSend = true;
        }

    }
}

void sendfile(const char* filename) {
    // 读入文件
    ifstream is(filename, ifstream::in | ios::binary);
    is.seekg(0, is.end);
    fileSize = is.tellg();
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

    hasSent = 0; // 已发送的文件大小
    seq = 1, ack = 0; // 发送包时的seq, ack
    base = 1; // 滑动窗口起始
    seq_opp = 0, ack_opp = 0;
    resend = false; // 重传标志
    finishSend = false; // 结束发送标志
    status = SS_STATUS; // 开始时是SS状态
    threshold = INIT_SW_SIZE;
    cwnd = 1;
    dup_ack_times = 0;

    // 用生成随机数模仿丢包率
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0, 1);

    // 发送文件
    while(true) {
        if (finishSend) {
            cout << "send successfully, send " << fileSize << " bytes." << endl;
            totalLength += fileSize;
            break;
        }

        // 初始化头部和数据段
        memset(header, 0, HEADERSIZE);
        memset(dataSegment, 0, DATASIZE);
        memset(sendBuf, 0, PACKETSIZE);        
        // 设置本次发送长度
        sendSize = min(PACKETSIZE, fileSize - hasSent + HEADERSIZE);

        // 如果THREAD_CREAT_FLAG = 1，则创建接收线程和计时线程
        if (THREAD_CREAT_FLAG == 1) {
            thread recvRespond(recvRespondThread);
            recvRespond.detach();
            thread timer(timerThread);
            timer.detach();
            THREAD_END = false;
            THREAD_CREAT_FLAG = 0;
        }

        if (resend) {
            // 如果需要重传（唯一需要重传的情况就是超时，收到错误的ACK并不会重传），则将seq回到base

            // 减去已经传输的数据数量，并且考虑在最后一组滑动窗口内的包出错的可能性
            if (dataLength == PACKETSIZE - HEADERSIZE) {
                hasSent -= dataLength * (seq - base);
            } else {
                // 此时dataLength较小，说明是发了最后一个包
                hasSent -= dataLength;
                hasSent -= (PACKETSIZE - HEADERSIZE) * (seq - base - 1);
            }

            seq = base;
            resend = false;
            continue;
        }
        
        // 如果不需要重传，则需要首先检查滑动窗口是否满
        if (seq < base + cwnd) {
            if (hasSent < fileSize) {
                // 如果没满，则设置header
                // seq = 即将发送的packet序号
                // ack 不需要设置

                // 设置seq位
                header[SEQ_BITS_START] = (u_char)(seq & 0xFF);
                header[SEQ_BITS_START + 1] = (u_char)(seq >> 8);
                header[SEQ_BITS_START + 2] = (u_char)(seq >> 16);
                header[SEQ_BITS_START + 3] = (u_char)(seq >> 24);

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


                // 模拟丢包
                if (dis(gen) > DISCARD_RATE) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_TIME)); // 模拟延时
                    sendResult = sendto(sendSocket, sendBuf, sendSize, 0, (SOCKADDR*)&recvAddr, sizeof(SOCKADDR));
                }
                    

                // 发送完毕后，如果base = seq，说明发的是滑动窗口内的第一个packet，则启动计时
                if (base == seq) {
                    start = clock();
                }

                // 更新发送长度和seq
                hasSent += sendSize - HEADERSIZE;
                seq++;
            }
        } else {
            // 如果不需要重传，也不能再发，就说一下send window已满，等待ack中
            // cout << "Send window is full! Waiting for the response ack..." << endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
            
        // 如果hasSent == fileSize，说明不用再发了，但是还不能结束发送。结束发送的标志只能由接收线程告知，需要确认收到了对方的所有ACK才能结束发送
        if (hasSent == fileSize) {
            cout << "hasSent == fileSize, but can't finish sending..." << endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            index++;
            if (index == 10) // 如果产生十次hasSent == fileSize且没有结束发送，说明receive端结束了接收packet和发送ack，且最后一个ack丢失了
                finishSend = true;
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
            cout << "Please input the file name, or q to quit (at least send one file before quiting): ";
            cin >> str;
            if (str == "q") {
                THREAD_END = true;
                break;
            } else {
                s = clock();
                sendfile(str.c_str());
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