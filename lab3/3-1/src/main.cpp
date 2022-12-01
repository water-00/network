#include <iostream>
#include <fstream>
#include <WinSock2.h>
#include <string>
#include <time.h>

using namespace std;

u_short checkSum(char* input, int length) {
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
	delete[] buf;
    return ~(sum & 0xFFFF);
}

int main(int argc, char *argv[])
{
	char str[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};	
	cout << (int)(checkSum(str, sizeof(str)));
}