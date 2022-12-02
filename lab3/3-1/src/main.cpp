#include <iostream>
#include <fstream>
#include <WinSock2.h>
#include <string>
#include <time.h>

#define HEADERSIZE 10

using namespace std;

u_short checkSum(char* input, int length) {
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
	// delete[] buf;
    return ~(sum & 0xFFFF);
}

int main(int argc, char *argv[])
{
    char header[HEADERSIZE] = {0};
    u_short checksum = 0;
    // 发送第一次握手请求报文
    memset(header, 0, HEADERSIZE);
    // 设置seq位
    u_short seq = rand() % 65535;
    header[0] = (u_char)(seq & 0xFF);
    header[1] = (u_char)(seq >> 8);
    // 设置SYN位
    header[4] = 0b010; // SYN在header[4]的第二位，所以这一行表示SYN == 1
    checksum = checkSum(header, HEADERSIZE);

    // 设置checksum位
    header[8] = (u_char)(checksum & 0xFF);
    header[9] = (u_char)(checksum >> 8);

    (char*)header;
    u_short s = checkSum(header, HEADERSIZE); // s应该等于0x0000
    if (s == 0) {
        cout << "success!" << endl;
    }
}