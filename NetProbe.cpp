// NetProbe.cpp : Defines the entry point for the console application.
//
#ifdef WIN32
#include "stdafx.h"
#endif

//
//  main.cpp
//  NetProbe
//
//  Created by Jonathan on 15/1/25.
//  Copyright (c) 2015 ___jonathan___. All rights reserved.
//

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib,"ws2_32.lib")
#else // Assume Linux
#include <sys/types.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#define SOCKET int
#define SOCKET_ERROR -1
#define INVALID_SOCKET -1
#define SOCKADDR sockaddr
#define Sleep(s) usleep(1000*s)
#define WSAGetLastError() (errno)
#define closesocket(s) close(s)
#define ioctlsocket ioctl
#define WSAEWOULDBLOCK EWOULDBLOCK
// There are other WSAExxxx constants which may also need to be defined
#endif

#include <iostream>
#include <queue>
#include <fstream>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <vector>
#include "es_TIMER.H"
using namespace std;

#define DEFAULT_BUFLEN 65536
#define DEFAULT_UDP_PORT 9878

std::mutex lck;
queue<SOCKET> socket_queue;
vector<std::thread> thread_array;
std::condition_variable cv;

struct preload_pointer
{
	char *index_point;
	char *error_point;
	char *readme_point;
};

preload_pointer pointers;

int thread_id = 0;

double disply_elapse_time = 0;
unsigned int display_pkts = 0, display_lost_pkts = 0;
double display_rate = 0, display_jitter = 0, display_lost_rate = 0;
bool packet_send_not_finished = true;
bool start_counting_time = false;
bool last_is_127_or_m1 = false;
//preloaded pages
enum preloaded {index_page,error_page,readme_page,none};

string warning = "Incorrect input format.\n\nUsage: NetProbe.exe[mode:s/c] <parameters depending on mode>\n\n========================================================================\n'c' [ref_int] [server_host] [server_port] [protocol] [pkt_size] [rate] [num]\n's' [port] [server_mode:o/p] [optional:thread number]\n========================================================================";

void error_handling(string str, int error_num)
{
	printf("\n%s: %d\n", str.c_str(), error_num);
	packet_send_not_finished = false;
}

bool check_arg_num(int argc, char mode)
{
	bool result = false;
	if (mode == 'c' && argc == 9)
		result = true;
	else if (mode == 's'&& (argc == 4 || argc == 5))
		result = true;
	else if (mode == 'o' && argc == 4)
		result = true;
	else if (mode == 'p' && argc == 5)
		result = true;
	else
		printf("%s\n", warning.c_str());
	return result;
}

// Get the size of a file
long getFileSize(FILE *file)
{
	long lCurPos, lEndPos;
	lCurPos = ftell(file);
	fseek(file, 0, 2);
	lEndPos = ftell(file);
	fseek(file, lCurPos, 0);
	return lEndPos;
}

void display_receive(int refresh_interval)
{
	unsigned int pkt_num = 0, lost_pkt = 0;
	double rate = 0, jitter = 0, lost_rate = 0;
	while (!start_counting_time)
	{
		printf("\rElapsed [%.1fs] Pkts [%u] Lost [%u, %.2lf%%] Rate [%.2lfbps] Jitter [%.3lfms]", disply_elapse_time / 1000, pkt_num, lost_pkt, lost_rate, rate, jitter);
		this_thread::sleep_for(chrono::milliseconds(refresh_interval));
	}

	ES_FlashTimer timer = ES_FlashTimer();
	timer.Start();
	while (packet_send_not_finished)
	{
		lck.lock();
		pkt_num = display_pkts; lost_pkt = display_lost_pkts;
		rate = display_rate; jitter = display_jitter; lost_rate = display_lost_rate;
		disply_elapse_time = timer.Elapsed();
		lck.unlock();
		if (rate<1000)
			printf("\rElapsed [%.1fs] Pkts [%u] Lost [%u, %.2lf%%] Rate [%.2lfbps] Jitter [%.3lfms]", disply_elapse_time / 1000, pkt_num, lost_pkt, lost_rate, rate, jitter);
		else if (rate >= 1000 && rate < 1000 * 1000)
			printf("\rElapsed [%.1fs] Pkts [%u] Lost [%u, %.2lf%%] Rate [%.2lfkbps] Jitter [%.3lfms]", disply_elapse_time / 1000, pkt_num, lost_pkt, lost_rate, rate / 1000, jitter);
		else if (rate >= 1000 * 1000 && rate < 1000 * 1000 * 1000)
			printf("\rElapsed [%.1fs] Pkts [%u] Lost [%u, %.2lf%%] Rate [%.2lfMbps] Jitter [%.3lfms]", disply_elapse_time / 1000, pkt_num, lost_pkt, lost_rate, rate / 1000 / 1000, jitter);
		else
			printf("\rElapsed [%.1fs] Pkts [%u] Lost [%u, %.2lf%%] Rate [%.2lfbps] Jitter [%.3lfms]", disply_elapse_time / 1000, pkt_num, lost_pkt, lost_rate, rate, jitter);
		fflush(stdout);
		this_thread::sleep_for(chrono::milliseconds(refresh_interval));
	}
	lck.lock();
	pkt_num = display_pkts; lost_pkt = display_lost_pkts;
	rate = display_rate; jitter = display_jitter; lost_rate = display_lost_rate;
	disply_elapse_time = timer.Elapsed();
	lck.unlock();
	if (rate<1000)
		printf("\rElapsed [%.1fs] Pkts [%u] Lost [%u, %.2lf%%] Rate [%.2lfbps] Jitter [%.3lfms]", disply_elapse_time / 1000, pkt_num, lost_pkt, lost_rate, rate, jitter);
	else if (rate >= 1000 && rate < 1000 * 1000)
		printf("\rElapsed [%.1fs] Pkts [%u] Lost [%u, %.2lf%%] Rate [%.2lfkbps] Jitter [%.3lfms]", disply_elapse_time / 1000, pkt_num, lost_pkt, lost_rate, rate / 1000, jitter);
	else if (rate >= 1000 * 1000 && rate < 1000 * 1000 * 1000)
		printf("\rElapsed [%.1fs] Pkts [%u] Lost [%u, %.2lf%%] Rate [%.2lfMbps] Jitter [%.3lfms]", disply_elapse_time / 1000, pkt_num, lost_pkt, lost_rate, rate / 1000 / 1000, jitter);
	else
		printf("\rElapsed [%.1fs] Pkts [%u] Lost [%u, %.2lf%%] Rate [%.2lfbps] Jitter [%.3lfms]", disply_elapse_time / 1000, pkt_num, lost_pkt, lost_rate, rate, jitter);
	fflush(stdout);

}

bool TCP_Client(int remote_port, char* remote_host, int ref_inter, int pkg_size, double rate, int pkg_num)
{
	int iResult = 0;
	//initialze Winsock
#ifdef WIN32
	WSADATA wsaData;
	iResult = WSAStartup(MAKEWORD(2, 0), &wsaData);
	if (iResult)
	{
		printf("WSAStartup failed: %d\n", iResult);
		WSACleanup();
		return false;
	}
#endif
	//create socket
	SOCKET sock;

	//-----------------Entering the requesting mode--------------------//

	// buffer
	char *sendbuf = (char*)malloc(pkg_size);
	int sendbuflen = pkg_size;
	memset(sendbuf, 0, sendbuflen);
	int i = 1;
	//memcpy(sendbuf, &i, sendbuflen);
	bool infinite_pkg = false;
	long seq_num = 1;

	sockaddr_in TCP_receiver_addr;
	TCP_receiver_addr.sin_family = AF_INET;
	TCP_receiver_addr.sin_port = htons(remote_port);
	TCP_receiver_addr.sin_addr.s_addr = inet_addr(remote_host);

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET)
	{
		error_handling("SOCKET create error: ", WSAGetLastError());
#ifdef WIN32
		WSACleanup();
#endif
		return false;
	}

	//Try connecting
	iResult = connect(sock, (SOCKADDR *)&TCP_receiver_addr, sizeof(TCP_receiver_addr));
#ifdef WIN32
	if (iResult == SOCKET_ERROR)
	{
		error_handling("Unable to connect with error", WSAGetLastError());
		closesocket(sock);
		WSACleanup();
#else
	if (iResult == -1)
	{
		error_handling("Unable to connect with error", WSAGetLastError());
		closesocket(sock);
#endif
		return false;
	}
	printf("Connecting Succesfully!\nSending request...\n");

	//send the request packet with metadata: Protocol[0-2], packet size[3-8], rate[9-16], num[17-24]
	char metadata[30], temp[10], temp2[10], temp3[10];
	memset(metadata, 0, sizeof(metadata));
	memset(temp, 0, sizeof(temp));
	memset(temp2, 0, sizeof(temp2));
	memset(temp3, 0, sizeof(temp3));
	sprintf(temp, "%d", pkg_size);
	sprintf(temp2, "%d", pkg_num);
	sprintf(temp3, "%d", (int)rate);
	for (int i = 0; i < 10; i++)
	{
		metadata[i] = temp[i];
		metadata[10 + i] = temp2[i];
		metadata[20 + i] = temp3[i];
	}

	iResult = send(sock, metadata, sizeof(metadata), 0);

	if (iResult == SOCKET_ERROR) {
		error_handling("send request failed with error: ", WSAGetLastError());
		closesocket(sock);
#ifdef WIN32
		WSACleanup();
#endif
		return false;
	}
	printf("Request sent!\n");
	//------------Wait for response and recv the data packet---------------//
	//
	//
	//------------Enetering the receiver mode--------------------------------//


	//create Timer
	ES_FlashTimer timer = ES_FlashTimer();

	// buffer
	char *recvbuf = (char *)malloc(pkg_size);
	int recvbuflen = pkg_size;

	unsigned int lost_pkg_num = 0;
	double duration = 0, current_rate = 0;
	long total_recv_bytes = 0, total_pkg_num = 0;
	long old_jit_time = 0, starting_time = 0;
	double jitter_old = 0, jitter_new = 0;
	//add a new thread
	std::thread th(display_receive, ref_inter);

	timer.Start();
	starting_time = timer.Elapsed();
	old_jit_time = timer.Elapsed();
	start_counting_time = true;
	do{
		memset(recvbuf, 0, recvbuflen);

		//TCP boundary maintain
		int byte_recv = 0;
		while (byte_recv < recvbuflen)
		{
			//iResult = recv(client_sock, recvbuf, recvbuflen, 0);
			iResult = recv(sock, recvbuf + byte_recv, recvbuflen - byte_recv, 0);
			if (iResult <= 0)
				break;
			byte_recv += iResult;
			total_recv_bytes += iResult;
			total_pkg_num++;

			duration = timer.Elapsed();
			current_rate = total_recv_bytes * 1.0 / (duration / 1000);
		}

		if (iResult == 0)
		{
			break;
		}
		else if (iResult<0)
		{
			error_handling("Recv failed with error: ", WSAGetLastError());
			closesocket(sock);
#ifdef WIN32
			WSACleanup();
#endif
			th.join();
			return true;
		}

		//get the pkg num first
		char temp_num[8];
		for (int j = 0; j < 8; j++)
			temp_num[j] = recvbuf[j];
		long temp_check_num = atol(temp_num);

		//check the pkg num
		if (temp_check_num == seq_num)
			seq_num++;
		else
			lost_pkg_num++;

		//counting jitter
		long temp_t = timer.Elapsed();
		double T = (temp_t - starting_time) *1.0 / seq_num;
		jitter_new = (jitter_old * (seq_num - 1) + temp_t - old_jit_time - T) *1.0 / seq_num;
		jitter_old = jitter_new;
		old_jit_time = temp_t;
		if (jitter_new < 0)
		{
			jitter_new = 0;
			jitter_old = 0;
		}

		//update the display attributes
		lck.lock();
		//duration = timer.Elapsed() / 1000;
		//disply_elapse_time = duration;
		display_pkts = seq_num - 1;
		display_rate = current_rate;
		display_lost_pkts = lost_pkg_num;
		display_lost_rate = lost_pkg_num*1.0 / total_pkg_num * 100;
		display_jitter = jitter_new;
		lck.unlock();

	} while (iResult>0);
	packet_send_not_finished = false;
	th.join();
	// cleanup
	printf("\nConnection closing...\n");
	closesocket(sock);
#ifdef WIN32
	WSACleanup();
#endif
	return true;
}

bool UDP_Client(int remote_port, char* remote_host, int ref_inter, int pkg_size, double req_rate, int req_pkg_num)
{
	//--------------------------Requesting part----------------------//
	//
	//--------------------------------------------------------------//

	int iResult = 0;
	//initialze Winsock
#ifdef WIN32
	WSADATA wsaData;
	iResult = WSAStartup(MAKEWORD(2, 0), &wsaData);
	if (iResult)
	{
		printf("WSAStartup failed: %d\n", iResult);
		WSACleanup();
		return false;
	}
#endif
	//create socket
	SOCKET sock;

	sockaddr_in UDP_Server_addr, UDP_Reiceiver_addr;
	socklen_t addr_len = sizeof(UDP_Server_addr);
	UDP_Server_addr.sin_family = AF_INET;
	UDP_Server_addr.sin_port = htons(remote_port);
	UDP_Server_addr.sin_addr.s_addr = inet_addr(remote_host);

	UDP_Reiceiver_addr.sin_family = AF_INET;
	UDP_Reiceiver_addr.sin_port = htons(DEFAULT_UDP_PORT);
	UDP_Reiceiver_addr.sin_addr.s_addr = inet_addr(remote_host);

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == SOCKET_ERROR)
	{
		error_handling("SOCKET create error: ", WSAGetLastError());
#ifdef WIN32
		WSACleanup();
#endif
		return false;
	}

	//send the request packet with metadata: Protocol[0-2], packet size[3-8], rate[9-16], num[17-24]
	char metadata[30], temp[10], temp2[10], temp3[10];
	memset(metadata, 0, sizeof(metadata));
	memset(temp, 0, sizeof(temp));
	memset(temp2, 0, sizeof(temp2));
	memset(temp3, 0, sizeof(temp3));
	sprintf(temp, "%d", pkg_size);
	sprintf(temp2, "%d", req_pkg_num);
	sprintf(temp3, "%d", (int)req_rate);
	for (int i = 0; i < 10; i++)
	{
		metadata[i] = temp[i];
		metadata[10 + i] = temp2[i];
		metadata[20 + i] = temp3[i];
	}

	//send the request
	iResult = sendto(sock, metadata, 30, 0, (sockaddr*)&UDP_Server_addr, sizeof(UDP_Server_addr));
	//printf("\npacket request send to %s:%d!\n", remote_host, remote_port);

	//------------------------------Receiving Part---------------------------//

	// buffer
	char *recvbuf = (char*)malloc(pkg_size);
	int recvbuflen = pkg_size;
	long int lost_pkg_num = 0;
	long seq_num = 1;
	double current_rate = 0;
	double duration = 0;
	long total_recv_bytes = 0, total_pkt_num = 0;
	long old_jit_time = 0, starting_time = 0;
	double jitter_old = 0, jitter_new = 0;
	ES_FlashTimer timer = ES_FlashTimer();

	//add a new thread
	std::thread th(display_receive, ref_inter);

	start_counting_time = false;
	bool temp_start_time = false;

	while (1)
	{
		memset(recvbuf, 0, recvbuflen);
		iResult = recvfrom(sock, recvbuf, recvbuflen, 0, (sockaddr*)&UDP_Server_addr, &addr_len);
		if (!temp_start_time && iResult > 0)
		{
			timer.Start();
			starting_time = timer.Elapsed();
			old_jit_time = starting_time;
			start_counting_time = true;
			temp_start_time = true;
		}
		total_pkt_num++;

		//check current rate
		duration = timer.Elapsed();
		current_rate = total_recv_bytes * 1.0 / (duration / 1000);

		//get the pkg num first
		char temp_num[8];
		for (int j = 0; j < 8; j++)
			temp_num[j] = recvbuf[j];
		long temp_check_num = atol(temp_num);

		//check packet sequence (rewrite)
		if (temp_check_num >= seq_num)
			seq_num = temp_check_num + 1;

		lost_pkg_num = seq_num - total_pkt_num - 1;

		//---------------------------------------//

		if (iResult == SOCKET_ERROR)
		{
			error_handling("recvfrom() failed with error: ", WSAGetLastError());
			packet_send_not_finished = false;
			th.join();
			closesocket(sock);
#ifdef WIN32
			WSACleanup();
#endif
			return false;
		}

		//counting jitter
		long temp_t = timer.Elapsed();
		double T = (temp_t - starting_time) *1.0 / (seq_num - 1);
		jitter_new = (jitter_old * (seq_num - 2) + temp_t - old_jit_time - T) *1.0 / (seq_num - 1);
		jitter_old = jitter_new;
		old_jit_time = temp_t;
		if (jitter_new < 0)
		{
			jitter_new = 0;
			jitter_old = 0;
		}

		total_recv_bytes += iResult;

		//update the display attributes
		lck.lock();
		display_pkts = seq_num - 1;
		display_rate = current_rate;
		display_lost_pkts = lost_pkg_num;
		display_lost_rate = lost_pkg_num*1.0 / total_pkt_num * 100;
		display_jitter = jitter_new;
		lck.unlock();
	}
	packet_send_not_finished = false;
	th.join();
	closesocket(sock);
#ifdef WIN32
	WSACleanup();
#endif
	return true;
}

bool http_client_handling(SOCKET sender_sock)
{
#ifndef WIN32
	signal(SIGPIPE,SIG_IGN);
#endif
	int iResult = 0;

	//-------------Receiving the http request and parse it---------//
	//printf("\nReceiving the metadata\n");
	char *temp_recv_buf = (char*)malloc(DEFAULT_BUFLEN);
	memset(temp_recv_buf, 0, DEFAULT_BUFLEN);
	iResult = recv(sender_sock, temp_recv_buf, DEFAULT_BUFLEN, 0);
	
	//printf("%s\n",temp_recv_buf);
	
	string token_array[2];
	int tem_num = 0;
	char *token = strtok(temp_recv_buf, " ");
	while (token != NULL && tem_num<2) {
		token_array[tem_num] = token;
        token = strtok(NULL, " ");
		tem_num++;
    }
	/*
	the first token is GET (use for checking)
	the second token is the file name('/' stands for index.htm)	
	*/

	//-------------Creating response data--------------------------------//

	//create file locator
	string file_name = token_array[1];
	string status_code = "200 OK";
	bool file_not_found = false;
	if (strcmp(file_name.c_str(),"/")==0)
		file_name = "index.htm";
	else if (file_name[0]=='/')
		file_name.erase(0, 1);

	FILE *file = NULL;
	char *buffer;

	//cout << "Page Name is " << file_name.c_str() << endl;

	//Open file
	file = fopen(file_name.c_str(), "rb");
	if (!file)
	{
		fputs("File not found\n", stderr);

		//file not found handling
		file_name = "error.htm";
		status_code = "404 Not Found";
		file_not_found = true;
	}

	if (file_not_found)
	{
		file = fopen(file_name.c_str(), "rb");
	}

	//Get file length
	long fileSize = getFileSize(file);

	//Allocate memory
	buffer = (char*)malloc(sizeof(char)*fileSize);
	if (!buffer)
	{
		fprintf(stderr, "Memory error!\n");
		fclose(file);
		return false;
	}
	//Read file contents into buffer
	int temp_result = fread(buffer, 1, fileSize, file);
	if (temp_result != fileSize)
	{
		fputs("Reading error\n", stderr);
		fclose(file);
		return false;
	}
	fclose(file);

	char header[10240];

	sprintf(header, "HTTP/1.1 %s\n"
		"Date: Thu, 19 Feb 2015 12:27:04 GMT\n"
		"Connection: close\n"
		"Server: Apache/2.2.3\n"
		"Accept-Ranges: bytes\n"
		"Content-Type: text/html\n"
		"Content-Length: %i\n"	
        "\n", status_code.c_str(), fileSize);
	long header_len = strlen(header);

	char *reply = (char *)malloc(header_len + fileSize);
	memset(reply, 0, header_len + fileSize);
	memcpy(reply, header,header_len);
	memcpy(reply + header_len, buffer, fileSize);

	//-------------Entering sender mode and send data--------------------//

	// buffer
	char *sendbuf = (char*)malloc(DEFAULT_BUFLEN);
	memset(sendbuf, 0, DEFAULT_BUFLEN);
	memcpy(sendbuf,reply,strlen(reply));
	//TCP boundary maintain
	int byte_send = 0;
	while (byte_send < DEFAULT_BUFLEN)
	{
		//iResult = send(sock, sendbuf, sendbuflen, 0);
		iResult = send(sender_sock, sendbuf + byte_send, DEFAULT_BUFLEN - byte_send, 0);
		if (iResult > 0)
		{
			byte_send += iResult;
		}

		if (iResult == SOCKET_ERROR) {
			error_handling("send failed with error: ", WSAGetLastError());
			closesocket(sender_sock);
			return false;
		}
	}	

	closesocket(sender_sock);
	return true;
}

bool On_Demand_Server(int port)
{
	printf("On Demand mode\n");
	int iResult = 0;
	struct sockaddr_in client;
	socklen_t socksize = sizeof(struct sockaddr_in);

	//create socket
	SOCKET listen_sock; //receiving request

	sockaddr_in TCP_receiver_addr;
	TCP_receiver_addr.sin_family = AF_INET;
	TCP_receiver_addr.sin_port = htons(port);
	TCP_receiver_addr.sin_addr.s_addr = INADDR_ANY;

	listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_sock == INVALID_SOCKET)
	{
		error_handling("\nSOCKET create error: ", WSAGetLastError());
#ifdef WIN32
		WSACleanup();
#endif
		return false;
	}

	//binding the socket
#ifdef WIN32
	iResult = ::bind(listen_sock, (LPSOCKADDR)&TCP_receiver_addr, sizeof(TCP_receiver_addr));
#else
	iResult = ::bind(listen_sock, (SOCKADDR *)&TCP_receiver_addr, sizeof(TCP_receiver_addr));
#endif
	if (iResult == SOCKET_ERROR)
	{
		error_handling("\nUnable to bind with error: ", WSAGetLastError());
		closesocket(listen_sock);
#ifdef WIN32
		WSACleanup();
#endif
		return false;
	}
	else
		printf("\nBinding successfully\n");

	//listen to the socket
	iResult = listen(listen_sock, SOMAXCONN);
	if (iResult == SOCKET_ERROR) {
		error_handling("\nlisten failed with error: ", WSAGetLastError());
		closesocket(listen_sock);
#ifdef WIN32
		WSACleanup();
#endif
		return false;
	}

	while (1)
	{
		SOCKET sender_sock; //connect to client
		sender_sock = accept(listen_sock, (struct sockaddr *)&client, &socksize);
		if (sender_sock == INVALID_SOCKET){
			error_handling("\nAccept failed with error: ", WSAGetLastError());
			closesocket(listen_sock);
#ifdef WIN32
			WSACleanup();
#endif
			break;
		}
		else
			printf("\nAccept the connection successfully\n");

		std::thread(http_client_handling, sender_sock).detach();
	}

	//Don't need the listening socket
	closesocket(listen_sock);

#ifdef WIN32
	WSACleanup();
#endif
	printf("\nConnetion closing...\n");
	return true;

}

char *preload_page(string file_name,long &fileSize)
{
	FILE *file = NULL;
	char *buffer;

	//Open file
	file = fopen(file_name.c_str(), "rb");
	if (!file)
	{
		fputs("File not found\n", stderr);
		//file not found handling
		file_name = "error.htm";
	}

	//Get file length
	fileSize = getFileSize(file);

	//Allocate memory
	buffer = (char*)malloc(sizeof(char)*fileSize);
	if (!buffer)
	{
		fprintf(stderr, "Memory error!");
		fclose(file);
		return false;
	}
	//Read file contents into buffer
	int temp_result = fread(buffer, 1, fileSize, file);
	if (temp_result != fileSize)
	{
		fputs("Reading error", stderr);
		fclose(file);
		return false;
	}
	fclose(file);

	return buffer;
}

struct preload_pointer thread_pool_preload_page()
{
	long index_fileSize,error_fileSize,readme_fileSize;
	char* index_header_pre = preload_page("index.htm",index_fileSize);
	char* error_header_pre = preload_page("error.htm",error_fileSize);
	char* readme_header_pre = preload_page("readme.htm",readme_fileSize);

	char index_header[10240];
	char error_header[10240];
	char readme_header[10240];

	//index_header_preload
	sprintf(index_header, "HTTP/1.1 200 OK\n"
		"Date: Thu, 19 Feb 2015 12:27:04 GMT\n"
		"Connection: close\n"
		"Server: Apache/2.2.3\n"
		"Accept-Ranges: bytes\n"
		"Content-Type: text/html\n"
		"Content-Length: %i\n"
		"\n", index_fileSize);
	long index_header_len = strlen(index_header);

	char *index_reply = (char *)malloc(index_header_len + index_fileSize);
	memset(index_reply, 0, index_header_len + index_fileSize);
	memcpy(index_reply, index_header, index_header_len);
	memcpy(index_reply + index_header_len, index_header_pre, index_fileSize);

	//error_header_preload
	sprintf(error_header, "HTTP/1.1 404 Not Found\n"
		"Date: Thu, 19 Feb 2015 12:27:04 GMT\n"
		"Connection: close\n"
		"Server: Apache/2.2.3\n"
		"Accept-Ranges: bytes\n"
		"Content-Type: text/html\n"
		"Content-Length: %i\n"
		"\n", error_fileSize);
	long error_header_len = strlen(error_header);

	char *error_reply = (char *)malloc(error_header_len + error_fileSize);
	memset(error_reply, 0, error_header_len + error_fileSize);
	memcpy(error_reply, error_header, error_header_len);
	memcpy(error_reply + error_header_len, error_header_pre, error_fileSize);

	//readme_header_preload
	sprintf(readme_header, "HTTP/1.1 200 OK\n"
		"Date: Thu, 19 Feb 2015 12:27:04 GMT\n"
		"Connection: close\n"
		"Server: Apache/2.2.3\n"
		"Accept-Ranges: bytes\n"
		"Content-Type: text/html\n"
		"Content-Length: %i\n"
		"\n", readme_fileSize);
	long readme_header_len = strlen(readme_header);

	char *readme_reply = (char *)malloc(readme_header_len + readme_fileSize);
	memset(readme_reply, 0, readme_header_len + readme_fileSize);
	memcpy(readme_reply, readme_header, readme_header_len);
	memcpy(readme_reply + readme_header_len, readme_header_pre, readme_fileSize);

	preload_pointer pointers;
	pointers.index_point = index_reply;
	pointers.error_point = error_reply;
	pointers.readme_point = readme_reply;
	printf("Preload pages done!\n");
	return pointers;

}

bool thread_pool_client_handling(SOCKET sender_sock)
{
#ifndef WIN32
	signal(SIGPIPE, SIG_IGN);
#endif
	int iResult = 0;

	//-------------Receiving the http request and parse it---------//
	//printf("\nReceiving the metadata\n");
	char *temp_recv_buf = (char*)malloc(DEFAULT_BUFLEN);
	memset(temp_recv_buf, 0, DEFAULT_BUFLEN);
	iResult = recv(sender_sock, temp_recv_buf, DEFAULT_BUFLEN, 0);

	//printf("%s\n",temp_recv_buf);

	string token_array[2];
	int tem_num = 0;
	char *token = strtok(temp_recv_buf, " ");
	while (token != NULL && tem_num<2) {
		token_array[tem_num] = token;
		token = strtok(NULL, " ");
		tem_num++;
	}
	/*
	the first token is GET (use for checking)
	the second token is the file name('/' stands for index.htm)
	*/

	//-------------Creating response data--------------------------------//

	//create file locator
	string file_name = token_array[1];
	string status_code = "200 OK";
	bool file_not_found = false;
	if (strcmp(file_name.c_str(),"/")==0)
		file_name = "index.htm";
	else if (file_name[0]=='/')
		file_name.erase(0, 1);

	//need some special handling for index,error,and readme!!!
	preloaded file_preloaded = none;
	if (strcmp( file_name.c_str(), "index.htm")==0)
		file_preloaded = index_page;
	else if (strcmp( file_name.c_str() , "error.htm")==0)
		file_preloaded = error_page;
	else if (strcmp( file_name.c_str() , "readme.htm")==0)
		file_preloaded = readme_page;
	else
		file_preloaded = none;

	char *reply;

	if (file_preloaded == none)
	{
		//Open file
		FILE *file = NULL;
		char *buffer;
		file = fopen(file_name.c_str(), "rb");
		if (!file)
		{
			fputs("File not found\n", stderr);

			//file not found handling
			file_name = "error.htm";
			status_code = "404 Not Found";
			file_not_found = true;
		}

		if (file_not_found)
		{
			file = fopen(file_name.c_str(), "rb");
		}

		//Get file length
		long fileSize = getFileSize(file);

		//Allocate memory
		buffer = (char*)malloc(sizeof(char)*fileSize);
		if (!buffer)
		{
			fprintf(stderr, "Memory error!\n");
			fclose(file);
			return false;
		}
		//Read file contents into buffer
		int temp_result = fread(buffer, 1, fileSize, file);
		if (temp_result != fileSize)
		{
			fputs("Reading error\n", stderr);
			fclose(file);
			return false;
		}
		fclose(file);

		char header[10240];

		sprintf(header, "HTTP/1.1 %s\n"
		"Date: Thu, 19 Feb 2015 12:27:04 GMT\n"
		"Connection: close\n"
		"Server: Apache/2.2.3\n"
		"Accept-Ranges: bytes\n"
		"Content-Type: text/html\n"
		"Content-Length: %i\n"	
        "\n", status_code.c_str(), fileSize);
		long header_len = strlen(header);

		reply = (char *)malloc(header_len + fileSize);
		memset(reply, 0, header_len + fileSize);
		memcpy(reply, header,header_len);
		memcpy(reply + header_len, buffer, fileSize);
	}
	else{
		//preload the pages
		//preload_pointer pointers = thread_pool_preload_page();
		if (file_preloaded == index_page)
			reply = pointers.index_point;
		else if (file_preloaded == error_page)
			reply = pointers.error_point;
		else if (file_preloaded == readme_page)
			reply = pointers.readme_point;
	}	

	//-------------Entering sender mode and send data--------------------//

	// buffer
	char *sendbuf = (char*)malloc(DEFAULT_BUFLEN);
	long reply_len = strlen(reply);
	memset(sendbuf, 0, DEFAULT_BUFLEN);
	memcpy(sendbuf, reply, reply_len);
	//TCP boundary maintain
	int byte_send = 0;
	while (byte_send < DEFAULT_BUFLEN)
	{
		//iResult = send(sock, sendbuf, sendbuflen, 0);
		iResult = send(sender_sock, sendbuf + byte_send, DEFAULT_BUFLEN - byte_send, 0);
		if (iResult > 0)
		{
			byte_send += iResult;
		}

		if (iResult == SOCKET_ERROR) {
			error_handling("send failed with error: ", WSAGetLastError());
			closesocket(sender_sock);
			return false;
		}
	}

	closesocket(sender_sock);
	return true;
}

void session_thread()
{
	SOCKET sock = INVALID_SOCKET;
	while (true)
	{
		std::unique_lock<std::mutex> lk(lck);
		while (socket_queue.empty())
			cv.wait(lk);
		sock = socket_queue.front();
		socket_queue.pop();
		lk.unlock();
		thread_pool_client_handling(sock);
		//cv.notify_one();
	}
}

void AssignThread(SOCKET sock, int thread_num)
{
	//cv.notify_one();
	std::unique_lock<std::mutex> lk(lck);
	socket_queue.push(sock);
	
	//cv.wait(lk);
	lk.unlock();
	cv.notify_one();
}

bool Thread_Pool_Server(int port, int thread_num)
{
	pointers = thread_pool_preload_page();
	printf("thread-pool mode\n");

	//creating threads beforehand
	for (int i = 0; i < thread_num; i++)
	{
		thread_array.push_back(std::thread(session_thread));
		printf("Creating the thread #%d and add to the pool\n", i);
	}

	int iResult = 0;
	struct sockaddr_in client;
	socklen_t socksize = sizeof(struct sockaddr_in);

	//create socket
	SOCKET listen_sock; //receiving request

	sockaddr_in TCP_receiver_addr;
	TCP_receiver_addr.sin_family = AF_INET;
	TCP_receiver_addr.sin_port = htons(port);
	TCP_receiver_addr.sin_addr.s_addr = INADDR_ANY;

	listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_sock == INVALID_SOCKET)
	{
		error_handling("\nSOCKET create error: ", WSAGetLastError());
#ifdef WIN32
		WSACleanup();
#endif
		return false;
	}

	//binding the socket
#ifdef WIN32
	iResult = ::bind(listen_sock, (LPSOCKADDR)&TCP_receiver_addr, sizeof(TCP_receiver_addr));
#else
	iResult = ::bind(listen_sock, (SOCKADDR *)&TCP_receiver_addr, sizeof(TCP_receiver_addr));
#endif
	if (iResult == SOCKET_ERROR)
	{
		error_handling("\nUnable to bind with error: ", WSAGetLastError());
		closesocket(listen_sock);
#ifdef WIN32
		WSACleanup();
#endif
		return false;
	}
	else
		printf("\nBinding successfully\n");

	//listen to the socket
	iResult = listen(listen_sock, SOMAXCONN);
	if (iResult == SOCKET_ERROR) {
		error_handling("\nlisten failed with error: ", WSAGetLastError());
		closesocket(listen_sock);
#ifdef WIN32
		WSACleanup();
#endif
		return false;
	}

	while (1)
	{
		SOCKET sender_sock; //connect to client
		sender_sock = accept(listen_sock, (struct sockaddr *)&client, &socksize);
		if (sender_sock == INVALID_SOCKET){
			error_handling("\nAccept failed with error: ", WSAGetLastError());
			closesocket(listen_sock);
#ifdef WIN32
			WSACleanup();
#endif
			break;
		}
		//else
			//printf("\nAccept the connection successfully\n");
		//AssignThread(sender_sock,thread_num);
		std::unique_lock<std::mutex> lk(lck);
		socket_queue.push(sender_sock);
		lk.unlock();
		cv.notify_one();
	}

	//Don't need the listening socket
	closesocket(listen_sock);

	for (int i = 0; i < thread_num; i++)
	{
		thread_array[i].join();
	}
	return true;
}

bool Server_mode(string http_mode, int tcp_port, int thread_num)
{
	//--------------------Creating multithread to sned data------------//
	int iResult = 0;
	//initialze Winsock
#ifdef WIN32
	WSADATA wsaData;
	iResult = WSAStartup(MAKEWORD(2, 0), &wsaData);
	if (iResult)
	{
		printf("WSAStartup failed: %d\n", iResult);
		WSACleanup();
		return false;
	}
#endif

	//check mode
	if (strcmp(http_mode.c_str(),"o")==0)
		On_Demand_Server(tcp_port);
	else if(strcmp(http_mode.c_str(),"p")==0)
		Thread_Pool_Server(tcp_port,thread_num);
	else
		printf("\nNo such server mode\n");

	return true;
}

int main(int argc, char *argv[])
{
	string mode = "", protocol = "", hostname = "";
	char* remote_host;
	int refresh_interval = 10, remote_port = 2000, packet_size = 0, num = 0, local_port = 2000;
	int tcp_port = 0, udp_port = 0;
	double rate = 0;

	if (argc <= 2)
	{
		printf("%s\n", warning.c_str());
		printf("\n");
		exit(0);
	}
	else
		mode = argv[1];

	int iResult = 0;

	if (strcmp(mode.c_str(),"c")==0)
	{
		//argument initialization
		printf("Client Mode\n");
		if (!check_arg_num(argc, 'c')){
#ifdef WIN32
			WSACleanup();
#endif
			return false;
		}
		refresh_interval = atoi(argv[2]);
		remote_host = argv[3];
		remote_port = atoi(argv[4]);
		protocol = argv[5];
		packet_size = atoi(argv[6]);
		rate = atof(argv[7]);
		num = atoi(argv[8]);
		if (strcmp(protocol.c_str(),"TCP")==0)
			TCP_Client(remote_port, remote_host, refresh_interval, packet_size, rate, num);
		else if (strcmp(protocol.c_str(),"UDP")==0)
			UDP_Client(remote_port, remote_host, refresh_interval, packet_size, rate, num);

	}
	else if (strcmp(mode.c_str(),"s")==0)
	{
		printf("Server Mode\n");
		if (!check_arg_num(argc, 's')){
#ifdef WIN32
			WSACleanup();
#endif
			return false;
		}
		tcp_port = atoi(argv[2]);
		string http_mode = argv[3];
		//in on-demand mode
		if (strcmp(http_mode.c_str(),"o")==0)
		{
			if (!check_arg_num(argc, 'o'))
			{
#ifdef WIN32
				WSACleanup();
#endif
				return false;
			}
			Server_mode(http_mode, tcp_port,0);		
		}
		//in thread-pool mode
		else if(strcmp(http_mode.c_str(),"p")==0)
		{
			if (!check_arg_num(argc, 'p'))
			{
#ifdef WIN32
				WSACleanup();
#endif
				return false;
			}
			int thread_num = atoi(argv[4]);
			Server_mode(http_mode, tcp_port, thread_num);
		}
		else
		{
			printf("No such mode\n");
		}
	}
	else
	{
		printf("No such mode\n");
	}
	return 0;
}

