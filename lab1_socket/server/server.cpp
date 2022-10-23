#include <stdio.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib") // 查一下

SOCKADDR_IN cAddr = { 0 };
int length = sizeof(cAddr);
SOCKET clientSocket[100];
int clientCount = 0; // 连接到服务器的客户端数

void communicate(int index) {
	char buf[1024];
	int r;
	while (1) {
		r = recv(clientSocket[index], buf, 1023, NULL);
		if (r > 0) {
			buf[r] = 0;
			printf("%d:%s\n", index, buf);
			// 广播数据
			for (int i = 0; i < clientCount; i++) {
				send(clientSocket[i], buf, strlen(buf), NULL);
			}
		}
	}
}

int main() {
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
	SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // 过会查一查
	if (SOCKET_ERROR == serverSocket) {
		printf("创建socket失败！\n");
		WSACleanup();
		return -2;
	}
	else {
		printf("创建socket成功！\n");
	}

	// 3. 创建协议地址簇
	SOCKADDR_IN addr = { 0 };
	addr.sin_family = AF_INET; // 协议版本
	addr.sin_addr.S_un.S_addr = inet_addr("10.130.167.31"); // ip地址，inet_addr把数点格式转换为整数
	addr.sin_port = htons(5555); // 端口号0 - 65535
	
	// 4. server绑定socket和协议地址簇
	int r = bind(serverSocket, (sockaddr*)&addr, sizeof(addr));
	if (r == -1) {
		printf("bind失败！\n");
		closesocket(serverSocket);
		WSACleanup();
		return -2;
	}
	else {
		printf("bind成功！\n");
	}

	// 5. 监听
	r = listen(serverSocket, 10);
	if (r == -1) {
		printf("监听失败！\n");
		closesocket(serverSocket);
		WSACleanup();
		return -2;
	}
	else {
		printf("监听成功！\n");
	}

	// 6. 等待客户端连接
	// 客户端协议地址簇
	while (1) {
		clientSocket[clientCount] = accept(serverSocket, (sockaddr*)&cAddr, &length); // accept是一个阻塞函数
		if (SOCKET_ERROR == clientSocket[clientCount]) {
			printf("服务器宕机！\n");
			closesocket(serverSocket);
			WSACleanup();
			return -2;
		}
		else {
			printf("客户端%s连接到服务器！\n", inet_ntoa(cAddr.sin_addr)); // inet_ntoa将ip转换为字符串
			CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)communicate, (char*)clientCount, NULL, NULL);
			clientCount++;
		}
	}
	
	// 7. 通信
	char buf[1024];
	while (1) {
		r = recv(clientSocket[clientCount], buf, 1023, NULL);
		if (r > 0) {
			buf[r] = 0; // 添加'\0'
			printf(">> %s\n", buf);
		}
	}

	// 8. 关闭socket
	closesocket(serverSocket);

	// 9. 清理协议
	WSACleanup();

	return 0;
}