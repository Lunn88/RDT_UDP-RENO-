#include<iostream>
#include<fstream>
#include<WS2tcpip.h>
#include<string.h>
#include<time.h>

#define FILEBUFF_LEN 8192
#define FILENAME_LEN 20

#pragma warning (disable : 4996)
#pragma comment (lib, "ws2_32.lib")

using namespace std;

SOCKET clientSocket;
static char fileBuff[1000][FILEBUFF_LEN];
static char fileName[FILENAME_LEN];
static int base = 0;
static int window_size = 7;
static int nextseq = 0;

#pragma pack(1)
class segment {
public:
	char content[FILEBUFF_LEN];
	int flag;
	// 0x01 for SYN, 0x02 for ACK, 0x04 for FIN
	int contSeq;
	int ackSeq;
	int index;
	int len;
	int checkSum;
	sockaddr_in serverAddr;

	segment() {
		ZeroMemory(this, sizeof(segment));
	}
	segment(sockaddr_in serverAddr) {
		ZeroMemory(this, sizeof(segment));
		this->serverAddr = serverAddr;
	}

	bool isSYN() {
		if (this->flag & 0x01) {
			return true;
		}
		else return false;
	}
	bool isACK() {
		if (this->flag & 0x02) {
			return true;
		}
		else return false;
	}
	bool isFIN() {
		if (this->flag & 0x04) {
			return true;
		}
		else return false;
	}
	void setSYN() {
		if (isSYN() == 0) {
			flag += 0x01;
		}
	}
	void setACK(segment s) {
		if (isACK() == 0) {
			flag += 0x02;
		}
		this->ackSeq = s.contSeq;
	}
	void setFIN() {
		if (isFIN() == 0) {
			flag += 0x04;
		}
	}
	void setCheckSum() {
		unsigned int sum = 0;
		int i = 0;
		u_char* tmp = (u_char*)this;
		for (int i = 0; i < 16; i++)
		{
			sum += (tmp[2 * i] << 8) + (tmp[2 * i + 1]);
			while (sum > 0xffff)
			{
				sum = (sum & 0xffff) + (sum >> 16);
			}
		}
		this->checkSum = ~((u_short)sum | 0xffff0000);
	}

	bool checkCheckSum() {
		unsigned int sum = 0;
		int i = 0;
		u_char* tmp = (u_char*)this;
		for (int i = 0; i < 16; i++)
		{
			sum += (tmp[2 * i] << 8) + (tmp[2 * i + 1]);
			while (sum > 0xffff)
			{
				sum = (sum & 0xffff) + (sum >> 16);
			}
		}
		if (checkSum + (u_short)sum == 0xffff) {
			return true;
		}
		else {
			return false;
		}
	}
};
#pragma pack()

void sendSegment(segment& s) {
	s.setCheckSum();
	sendto(clientSocket, (char*)&s, sizeof(segment), 0, (sockaddr*)&s.serverAddr, sizeof(s.serverAddr));
}

void recvSegment(segment& s) {
	ZeroMemory(s.content, sizeof(s.content));
	int serverLen = sizeof(s.serverAddr);
	recvfrom(clientSocket, (char*)&s, sizeof(segment), 0, (sockaddr*)&s.serverAddr, &serverLen);
}

bool stopAndwait(segment& s_in, segment s_out) {
	sendSegment(s_out);
	double start = clock();
	int retry = 0;

	while (true)
	{
		recvSegment(s_in);
		if (s_in.isACK() && s_in.ackSeq == s_out.contSeq) {
			return true;
		}
		if (retry == 5) {
			cout << "Fail to send file!" << endl;
			exit(0);
		}
		if ((clock() - start) / CLOCKS_PER_SEC >= 3) {
			retry++;
			start = clock();
			cout << "retrying..." << retry << endl;
			sendSegment(s_out);
		}
	}
}

bool establish_connection(sockaddr_in serverAddr) {
	segment syn_segment = segment(serverAddr);
	segment ack_segment = segment(serverAddr);
	syn_segment.setSYN();
	cout << "Sent SYN, waiting for ACK..." << endl;
	if (stopAndwait(ack_segment, syn_segment)) {
		cout << "Connection esatablished!" << endl;
		return true;
	}
	return false;
}

bool end_connection(sockaddr_in serverAddr) {
	segment fin_segment = segment(serverAddr);
	segment ack_segment = segment(serverAddr);
	fin_segment.setFIN();
	cout << "Sent FIN, waiting for ACK..." << endl;
	if (stopAndwait(ack_segment, fin_segment)) {
		cout << "Disconnected!" << endl;
		return true;
	}
	return false;
}

void readFile(char fileName[], segment& s) {
	ifstream ifs;
	int index = 0;
	int len = 0;
	char tmp;
	ZeroMemory(fileBuff, FILEBUFF_LEN);
	ifs.open(fileName, ios::binary);
	while (ifs.peek() != EOF)
	{
		tmp = ifs.get();
		fileBuff[index][len] = tmp;
		len++;
		if (len % FILEBUFF_LEN == 0) {
			index++;
			len = 0;
		}
	}
	ifs.close();
	s.index = index;
	s.len = len;
	cout << "File read!" << endl;
}

bool timeout = false;
double segment_start;
static int retry = 0;
static int status = 0;
static int ssthresh = 20;

void sendT(segment& s) {
	double file_start = clock();
	while (base < s.index) {

		if ((base + window_size == nextseq || base + window_size >= s.index) && timeout == false) {
			continue;
		}

		int tmp = base;
		segment_start = clock();

		segment send(s.serverAddr);
		for (; nextseq < base + window_size && nextseq <= s.index && timeout == false; nextseq++) {

			if (nextseq == s.index) {
				for (int j = 0; j < s.len; j++) {
					send.content[j] = fileBuff[nextseq][j];
				}
			}
			else {
				for (int j = 0; j < FILEBUFF_LEN; j++) {
					send.content[j] = fileBuff[nextseq][j];
				}
			}

			/*send.contSeq = nextseq;
			sendSegment(send);*/

			if (nextseq == 30) {

			}
			else {
				send.contSeq = nextseq;
				sendSegment(send);
			}

			printf("status: %d   base: %d   window size: %d   ssthresh: %d   nextseq: %d\n", status, base, window_size, ssthresh, nextseq);
		}

		if (base == tmp && timeout == true) {

			if (status == 0 || status == 1) {
				ssthresh = window_size / 2;
				window_size = 1;
				status = 0;
			}
			if (status == 2) {
				ssthresh = window_size / 2;
				window_size = ssthresh + 3;
			}
			timeout = false;
			for (int i = base; i < nextseq && i <= s.index; i++)
			{
				if (i == s.index) {
					for (int j = 0; j < s.len; j++) {
						send.content[j] = fileBuff[i][j];
					}
				}
				else {
					for (int j = 0; j < FILEBUFF_LEN; j++) {
						send.content[j] = fileBuff[i][j];
					}
				}
				printf("resending %d...%d\n", i, retry);
				send.contSeq = i;
				sendSegment(send);
				printf("status: %d   base: %d   window size: %d   ssthresh: %d   nextseq: %d\n", status, base, window_size, ssthresh, nextseq);
			}

			retry++;
			if (retry > 5) {
				cout << "Fail to sent file!" << endl;
				exit(0);
			}
		}
	}
	cout << "File content sent!" << endl;
	double sec = (clock() - file_start) / CLOCKS_PER_SEC;
	cout << "Cost " << sec << " sec." << endl;
	cout << "Rate: " << (s.index + 1) * sizeof(segment) * 8 / sec / 1000000 << "Mbps." << endl;
}

void recvACK(segment& s) {
	int dup = 0;
	while (base < s.index)
	{
		segment in = segment();
		recvSegment(in);
		if (in.isACK() && (in.ackSeq >= base || in.ackSeq == s.index)) {
			retry = 0;

			if (status == 0) {
				window_size *= 2;
				if (window_size >= ssthresh) {
					status = 1;
				}
			}
			else if (status == 1) {
				window_size += 1;
			}
			else {
				window_size = ssthresh;
				status = 1;
			}

			if (in.ackSeq == s.index) {
				base = s.index;
			}
			else {
				base = in.ackSeq + 1;
			}
		}

		else if (in.isACK() && in.ackSeq < base && timeout == false) {
			dup++;
			printf("Duplicate count: %d\n", dup);
			if (status == 2) {
				window_size++;
			}
			if (dup == 3) {
				status = 2;
				timeout = true;
				dup = 0;
			}
		}
		if ((clock() - segment_start) / CLOCKS_PER_SEC >= 0.5) {
			timeout = true;
		}
	}
}

void sendFile(char fileName[], segment& s) {
	for (int i = 0; i < strlen(fileName); i++) {
		s.content[i] = fileName[i];
	}
	s.contSeq = nextseq;
	segment in = segment(s.serverAddr);
	double start = clock();
	stopAndwait(in, s);
	cout << "File name sent!" << endl;
	HANDLE hThread1 = ::CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)sendT, (LPVOID)&s, NULL, NULL);
	HANDLE hThread2 = ::CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)recvACK, (LPVOID)&s, NULL, NULL);
	WaitForSingleObject(hThread1, INFINITE);
	WaitForSingleObject(hThread2, INFINITE);
}

int main() {
	// Startup Winsock
	WSADATA data;
	WORD version = MAKEWORD(2, 2);
	int wsOk = WSAStartup(version, &data);
	if (wsOk != 0) {
		cerr << "Can't start Winsock!" << wsOk;
		return 1;
	}

	// Creat a hint structure for the server
	clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(54000);
	inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr); // ip transfer
	int serverLen = sizeof(serverAddr);

	struct timeval timeout;
	timeout.tv_sec = 1;//秒
	timeout.tv_usec = 0;//微秒
	if (setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) == -1) {
		cout << "setsockopt failed:";
	}

	establish_connection(serverAddr);

	while (true)
	{
		base = 0;
		nextseq = 0;
		cout << "Enter a file name to send: ";
		ZeroMemory(fileName, FILENAME_LEN);
		cin >> fileName;
		segment s = segment(serverAddr);
		if (strcmp(fileName, "FIN") == 0) {
			end_connection(serverAddr);
			return 0;
		}
		readFile(fileName, s);
		sendFile(fileName, s);
	}

	closesocket(clientSocket);
	WSACleanup();
}