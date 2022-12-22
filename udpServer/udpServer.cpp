#include<iostream>
#include<fstream>
#include<WS2tcpip.h>
#include<string>
#include<time.h>

#define FILEBUFF_LEN 8192
#define FILENAME_LEN 20

#pragma warning (disable : 4996)
#pragma comment (lib, "ws2_32.lib")

using namespace std;

SOCKET serverSocket;
static int nextseq = 0;
static char fileContent[10000][FILEBUFF_LEN];

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
	sockaddr_in clientAddr;

	segment() {
		ZeroMemory(this, sizeof(segment));
	}
	segment(sockaddr_in clientAddr) {
		ZeroMemory(this, sizeof(segment));
		this->clientAddr = clientAddr;
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
	void setACK(segment s) {
		if (isACK() == 0) {
			flag += 0x02;
		}
		this->ackSeq = s.contSeq;
	}
	void setCheckSum();
	bool checkCheckSum();
};
#pragma pack()

void segment::setCheckSum() {
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
	this->checkSum = ~(u_short)sum;
}

bool segment::checkCheckSum() {
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

void sendSegment(segment s) {
	s.setCheckSum();
	sendto(serverSocket, (char*)&s, sizeof(segment), 0, (sockaddr*)&s.clientAddr, sizeof(s.clientAddr));
}

void recvSegment(segment& s) {
	ZeroMemory(s.content, sizeof(s.content));
	int clientLen = sizeof(s.clientAddr);
	recvfrom(serverSocket, (char*)&s, sizeof(segment), 0, (sockaddr*)&s.clientAddr, &clientLen);
}

void establish_connection(segment syn_segment, sockaddr_in clientAddr) {
	segment ack_segment = segment(clientAddr);
	ack_segment.setACK(syn_segment);
	sendSegment(ack_segment);
}

void end_connection(segment fin_segment, sockaddr_in clientAddr) {
	segment ack_segment = segment(clientAddr);
	ack_segment.setACK(fin_segment);
	sendSegment(ack_segment);
}

void writeFile(char name[], int index, int len) {
	ofstream ofs;
	ofs.open(name, ios::binary);
	if (!ofs.is_open()) {
		cout << "Can't open file!" << endl;
		return;
	}
	for (int i = 0; i < index; i++) {
		for (int j = 0; j < FILEBUFF_LEN; j++) {
			ofs << fileContent[i][j];
		}
	}
	for (int i = 0; i < len; i++) {
		ofs << fileContent[index][i];
	}
	ofs.close();
	cout << "File written!" << endl;
}

int recv_sum = 0;
void recvThread(segment s) {
	while (nextseq <= s.index)
	{
		segment in = segment();
		segment out = segment(s.clientAddr);
		recvSegment(in);
		if (in.checkCheckSum() && in.contSeq == nextseq) {
			recv_sum++;
			if (recv_sum % 5 == 0 || in.contSeq == s.index) {
				cout << "Received segment: " << in.contSeq << ", Check sum: " << in.checkSum << '.' << endl;
				cout << "Check sum Correct, replying ACK!" << endl;
				out.setACK(in);
				sendSegment(out);
			}

			if (nextseq == s.index) {
				for (int j = 0; j < s.len; j++) {
					fileContent[nextseq][j] = in.content[j];
				}
			}
			else {
				for (int j = 0; j < FILEBUFF_LEN; j++) {
					fileContent[nextseq][j] = in.content[j];
				}
			}
			nextseq++;
		}
		else {
			in.contSeq = nextseq - 1;
			out.setACK(in);
			sendSegment(out);
		}
	}
}

void recvAndAck(segment s) {
	segment ack_segment = segment(s.clientAddr);
	ack_segment.setACK(s);
	ack_segment.setCheckSum();
	sendSegment(ack_segment);

	char fileName[FILENAME_LEN];
	ZeroMemory(fileName, FILENAME_LEN);
	strcpy(fileName, s.content);
	cout << "Received file name!" << endl;

	ZeroMemory(fileContent, FILEBUFF_LEN);
	recvThread(s);
	cout << "Received all the content in " << fileName << endl;
	writeFile(fileName, s.index, s.len);
}

int main() {
	// Startup Winsock
	WSADATA wsaData;
	WORD version = MAKEWORD(2, 2);
	int wsOk = WSAStartup(version, &wsaData);
	if (wsOk != 0) {
		cout << "Can't start Winsock!" << wsOk;
		return 1;
	}

	//Bind socket to ip address and port
	serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
	sockaddr_in serverAddr;
	serverAddr.sin_addr.S_un.S_addr = ADDR_ANY;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(54000); // host to net short
	sockaddr_in clientAddr;

	if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		cout << "Can't bind socket! " << WSAGetLastError() << endl;
		return 1;
	}

	int clientLen = sizeof(clientAddr);
	ZeroMemory(&clientAddr, clientLen);

	cout << "waiting for SYN..." << endl;
	while (true)
	{
		nextseq = 0;
		recv_sum = 0;
		segment s = segment(clientAddr);
		recvSegment(s);
		if (s.checkCheckSum()) {
			if (s.isSYN()) {
				establish_connection(s, s.clientAddr);
				cout << "Connection established!" << endl;
				continue;
			}
			else if (s.isFIN()) {
				end_connection(s, s.clientAddr);
				cout << "Client disconnected..." << endl;
				return 0;
			}
			else recvAndAck(s);
		}
	}

	closesocket(serverSocket);
	WSACleanup();
}