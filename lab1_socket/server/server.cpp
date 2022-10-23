#include <stdio.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib") // ��һ��

SOCKADDR_IN cAddr = { 0 };
int length = sizeof(cAddr);
SOCKET clientSocket[100];
int clientCount = 0; // ���ӵ��������Ŀͻ�����

void communicate(int index) {
	char buf[1024];
	int r;
	while (1) {
		r = recv(clientSocket[index], buf, 1023, NULL);
		if (r > 0) {
			buf[r] = 0;
			printf("%d:%s\n", index, buf);
			// �㲥����
			for (int i = 0; i < clientCount; i++) {
				send(clientSocket[i], buf, strlen(buf), NULL);
			}
		}
	}
}

int main() {
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
	SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // �����һ��
	if (SOCKET_ERROR == serverSocket) {
		printf("����socketʧ�ܣ�\n");
		WSACleanup();
		return -2;
	}
	else {
		printf("����socket�ɹ���\n");
	}

	// 3. ����Э���ַ��
	SOCKADDR_IN addr = { 0 };
	addr.sin_family = AF_INET; // Э��汾
	addr.sin_addr.S_un.S_addr = inet_addr("10.130.167.31"); // ip��ַ��inet_addr�������ʽת��Ϊ����
	addr.sin_port = htons(5555); // �˿ں�0 - 65535
	
	// 4. server��socket��Э���ַ��
	int r = bind(serverSocket, (sockaddr*)&addr, sizeof(addr));
	if (r == -1) {
		printf("bindʧ�ܣ�\n");
		closesocket(serverSocket);
		WSACleanup();
		return -2;
	}
	else {
		printf("bind�ɹ���\n");
	}

	// 5. ����
	r = listen(serverSocket, 10);
	if (r == -1) {
		printf("����ʧ�ܣ�\n");
		closesocket(serverSocket);
		WSACleanup();
		return -2;
	}
	else {
		printf("�����ɹ���\n");
	}

	// 6. �ȴ��ͻ�������
	// �ͻ���Э���ַ��
	while (1) {
		clientSocket[clientCount] = accept(serverSocket, (sockaddr*)&cAddr, &length); // accept��һ����������
		if (SOCKET_ERROR == clientSocket[clientCount]) {
			printf("������崻���\n");
			closesocket(serverSocket);
			WSACleanup();
			return -2;
		}
		else {
			printf("�ͻ���%s���ӵ���������\n", inet_ntoa(cAddr.sin_addr)); // inet_ntoa��ipת��Ϊ�ַ���
			CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)communicate, (char*)clientCount, NULL, NULL);
			clientCount++;
		}
	}
	
	// 7. ͨ��
	char buf[1024];
	while (1) {
		r = recv(clientSocket[clientCount], buf, 1023, NULL);
		if (r > 0) {
			buf[r] = 0; // ���'\0'
			printf(">> %s\n", buf);
		}
	}

	// 8. �ر�socket
	closesocket(serverSocket);

	// 9. ����Э��
	WSACleanup();

	return 0;
}