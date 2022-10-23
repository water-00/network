#include <stdio.h>
// #include <windows.h>
#include <graphics.h> // easyX

#pragma comment(lib, "ws2_32.lib") // 查一下

SOCKET clientSocket;
HWND hWnd;
int count = 0;

void receiveMessage() {
	char recvBuf[1024];
	int r;
	while (1) {
		r = recv(clientSocket, recvBuf, 1023, NULL);
		if (r > 0) {
			recvBuf[r] = 0;
			outtextxy(0, count * 20, recvBuf);
			count++;
		}
	}
}

int main() {

	hWnd = initgraph(400, 400, SHOWCONSOLE);

	// 1. 请求协议版本
	WSADATA wasData;
	WSAStartup(MAKEWORD(2, 2), &wasData); // ?过会查一查这里
	if (HIBYTE(wasData.wVersion) != 2 || LOBYTE(wasData.wVersion) != 2) {
		printf("请求协议版本失败！\n");
		return -1;
	}
	else {
		printf("请求协议成功！\n");
	}

	// 2. 创建socket
	clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // 过会查一查 // PF?
	if (SOCKET_ERROR == clientSocket) {
		printf("创建socket失败！\n");
		WSACleanup();
		return -2;
	}
	else {
		printf("创建socket成功！\n");
	}

	// 3. 创建服务器地址簇
	SOCKADDR_IN addr = { 0 };
	addr.sin_family = AF_INET; // 协议版本 // PF?
	addr.sin_addr.S_un.S_addr = inet_addr("10.130.167.31"); // ip地址，inet_addr把数点格式转换为整数
	addr.sin_port = htons(5555); // 端口号0 - 65535

	// 4. 连接服务器
	int r = connect(clientSocket, (sockaddr*)&addr, sizeof(addr));
	if (r == -1) {
		printf("连接服务器失败！\n");
	}
	else {
		printf("连接服务器成功！\n");
	}

	// 5. 通信
	char buf[1024];
	CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)receiveMessage, NULL, NULL, NULL); // 查一下

	while (1) {
		memset(buf, 0, 1024);
		printf("请输入消息：");
		scanf_s("%s", buf, 1024);
		r = send(clientSocket, buf, strlen(buf), NULL);
	}

	return 0;
}