#include <stdio.h>
// #include <windows.h>
#include <graphics.h> // easyX

#pragma comment(lib, "ws2_32.lib") // ��һ��

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

	// 1. ����Э��汾
	WSADATA wasData;
	WSAStartup(MAKEWORD(2, 2), &wasData); // ?�����һ������
	if (HIBYTE(wasData.wVersion) != 2 || LOBYTE(wasData.wVersion) != 2) {
		printf("����Э��汾ʧ�ܣ�\n");
		return -1;
	}
	else {
		printf("����Э��ɹ���\n");
	}

	// 2. ����socket
	clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // �����һ�� // PF?
	if (SOCKET_ERROR == clientSocket) {
		printf("����socketʧ�ܣ�\n");
		WSACleanup();
		return -2;
	}
	else {
		printf("����socket�ɹ���\n");
	}

	// 3. ������������ַ��
	SOCKADDR_IN addr = { 0 };
	addr.sin_family = AF_INET; // Э��汾 // PF?
	addr.sin_addr.S_un.S_addr = inet_addr("10.130.167.31"); // ip��ַ��inet_addr�������ʽת��Ϊ����
	addr.sin_port = htons(5555); // �˿ں�0 - 65535

	// 4. ���ӷ�����
	int r = connect(clientSocket, (sockaddr*)&addr, sizeof(addr));
	if (r == -1) {
		printf("���ӷ�����ʧ�ܣ�\n");
	}
	else {
		printf("���ӷ������ɹ���\n");
	}

	// 5. ͨ��
	char buf[1024];
	CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)receiveMessage, NULL, NULL, NULL); // ��һ��

	while (1) {
		memset(buf, 0, 1024);
		printf("��������Ϣ��");
		scanf_s("%s", buf, 1024);
		r = send(clientSocket, buf, strlen(buf), NULL);
	}

	return 0;
}