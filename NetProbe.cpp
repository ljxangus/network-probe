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

/*
TODO:
1. Precise Rate Control
*/
#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib,"ws2_32.lib")
#else // Assume Linux
#include <sys/types.h>
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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include "es_TIMER.H"
using namespace std;

#define DEFAULT_BUFLEN 30
#define DEFAULT_UDP_PORT 4180

std::mutex m;
unique_lock<mutex> lck(m, defer_lock);

int tcp_client_num = 0, udp_client_num = 0;
double aggregate_rate = 0;
double disply_elapse_time = 0;
unsigned int display_pkts = 0, display_lost_pkts = 0;
double display_rate = 0, display_jitter = 0, display_lost_rate = 0;
bool packet_send_not_finished = true;
bool start_counting_time = false;
bool last_is_127_or_m1 = false;
string warning = "Incorrect input format.\n\nUsage: NetProbe.exe[mode:s/r/h] <parameters depending on mode>\n\n========================================================================\n's' [ref_int] [remote_host] [remote_port] [protocol] [pkt_size] [rate] [num]\n'r' [ref_int] [local_host] [local_port] [protocol] [pkt_size]\n'h' [hostname]\n========================================================================";

void error_handling(char *str, int error_num)
{
	printf("\n%s: %d\n", str, error_num);
	packet_send_not_finished = false;
}

bool boundary_check(int a)
{
	if (last_is_127_or_m1 && (a == 128 || a == 0))
	{
		last_is_127_or_m1 = false;
		return true;
	}
	else if (a == 127 || a == -1)
		last_is_127_or_m1 = true;
	return false;
}

bool check_arg_num(int argc, char mode)
{
	bool result = false;
	if (mode == 'c' && argc == 9)
		result = true;
	else if (mode == 's'&& argc == 5)
		result = true;
	else
		printf("%s\n", warning.c_str());
	return result;
}

void display_clients_num(int refresh_interval)
{
	while (1)
	{
#ifdef WIN32
		lck.lock();
#endif
		if (aggregate_rate<1000)
			printf("\rAggregate Rate [%.2fbps] # of TCP Clients [%d] # of UDP CLients [%d]", aggregate_rate, tcp_client_num, udp_client_num);
		else if (aggregate_rate >= 1000 && aggregate_rate < 1000 * 1000)
			printf("\rAggregate Rate [%.2fkbps] # of TCP Clients [%d] # of UDP CLients [%d]", aggregate_rate / 1000, tcp_client_num, udp_client_num);
		else if (aggregate_rate >= 1000 * 1000 && aggregate_rate < 1000 * 1000 * 1000)
			printf("\rAggregate Rate [%.2fMbps] # of TCP Clients [%d] # of UDP CLients [%d]", aggregate_rate / 1000 / 1000, tcp_client_num, udp_client_num);
		else
			printf("\rAggregate Rate [%.2fbps] # of TCP Clients [%d] # of UDP CLients [%d]", aggregate_rate, tcp_client_num, udp_client_num);
		cout.flush();
#ifdef WIN32
		lck.unlock();
#endif
		this_thread::sleep_for(chrono::milliseconds(refresh_interval));
	}
}

void display_send(int refresh_interval)
{
	ES_FlashTimer timer = ES_FlashTimer();
	timer.Start();
	while (packet_send_not_finished)
	{
		disply_elapse_time = timer.Elapsed();
		cout.flush();
#ifdef WIN32
		lck.lock();
#endif
		if (display_rate<1000)
			printf("\rElapsed [%.1fs] Pkts [%d] Rate [%.2fbps] Jitter [%.3fms]", disply_elapse_time / 1000, display_pkts, display_rate, display_jitter);
		else if (display_rate >= 1000 && display_rate < 1000 * 1000)
			printf("\rElapsed [%.1fs] Pkts [%d] Rate [%.2fkbps] Jitter [%.3fms]", disply_elapse_time / 1000, display_pkts, display_rate / 1000, display_jitter);
		else if (display_rate >= 1000 * 1000 && display_rate < 1000 * 1000 * 1000)
			printf("\rElapsed [%.1fs] Pkts [%d] Rate [%.2fMbps] Jitter [%.3fms]", disply_elapse_time / 1000, display_pkts, display_rate / 1000 / 1000, display_jitter);
		else
			printf("\rElapsed [%.1fs] Pkts [%d] Rate [%.2fbps] Jitter [%.3fms]", disply_elapse_time / 1000, display_pkts, display_rate, display_jitter);
#ifdef WIN32
		lck.unlock();
#endif
		cout.flush();
		this_thread::sleep_for(chrono::milliseconds(refresh_interval));
	}
	disply_elapse_time = timer.Elapsed();
#ifdef WIN32
	lck.lock();
#endif
	if (display_rate<1000)
		printf("\rElapsed [%.1fs] Pkts [%d] Rate [%.2fbps] Jitter [%.3fms]", disply_elapse_time / 1000, display_pkts, display_rate, display_jitter);
	else if (display_rate >= 1000 && display_rate < 1000 * 1000)
		printf("\rElapsed [%.1fs] Pkts [%d] Rate [%.2fkbps] Jitter [%.3fms]", disply_elapse_time / 1000, display_pkts, display_rate / 1000, display_jitter);
	else if (display_rate >= 1000 * 1000 && display_rate < 1000 * 1000 * 1000)
		printf("\rElapsed [%.1fs] Pkts [%d] Rate [%.2fMbps] Jitter [%.3fms]", disply_elapse_time / 1000, display_pkts, display_rate / 1000 / 1000, display_jitter);
	else
		printf("\rElapsed [%.1fs] Pkts [%d] Rate [%.2fbps] Jitter [%.3fms]", disply_elapse_time / 1000, display_pkts, display_rate, display_jitter);
#ifdef WIN32
	lck.unlock();
#endif
	cout.flush();
}

void display_receive(int refresh_interval)
{
	while (!start_counting_time)
	{
		cout.flush();
#ifdef WIN32
		lck.lock();
#endif
		printf("\rElapsed [%.1fs] Pkts [%d] Lost [%d, %.2f%%] Rate [%.2fbps] Jitter [%.3fms]", disply_elapse_time / 1000, display_pkts, display_lost_pkts, display_lost_rate, display_rate, display_jitter);
#ifdef WIN32
		lck.unlock();
#endif
		this_thread::sleep_for(chrono::milliseconds(refresh_interval));
	}

	ES_FlashTimer timer = ES_FlashTimer();
	timer.Start();
	while (packet_send_not_finished)
	{
		cout.flush();
#ifdef WIN32
		lck.lock();
#endif
		disply_elapse_time = timer.Elapsed();
		if (display_rate<1000)
			printf("\rElapsed [%.1fs] Pkts [%d] Lost [%d, %.2f%%] Rate [%.2fbps] Jitter [%.3fms]", disply_elapse_time / 1000, display_pkts, display_lost_pkts, display_lost_rate, display_rate, display_jitter);
		else if (display_rate >= 1000 && display_rate < 1000 * 1000)
			printf("\rElapsed [%.1fs] Pkts [%d] Lost [%d, %.2f%%] Rate [%.2fkbps] Jitter [%.3fms]", disply_elapse_time / 1000, display_pkts, display_lost_pkts, display_lost_rate, display_rate / 1000, display_jitter);
		else if (display_rate >= 1000 * 1000 && display_rate < 1000 * 1000 * 1000)
			printf("\rElapsed [%.1fs] Pkts [%d] Lost [%d, %.2f%%] Rate [%.2fMbps] Jitter [%.3fms]", disply_elapse_time / 1000, display_pkts, display_lost_pkts, display_lost_rate, display_rate / 1000 / 1000, display_jitter);
		else
			printf("\rElapsed [%.1fs] Pkts [%d] Lost [%d, %.2f%%] Rate [%.2fbps] Jitter [%.3fms]", disply_elapse_time / 1000, display_pkts, display_lost_pkts, display_lost_rate, display_rate, display_jitter);
#ifdef WIN32
		lck.unlock();
#endif
		cout.flush();
		this_thread::sleep_for(chrono::milliseconds(refresh_interval));
	}
#ifdef WIN32
	lck.lock();
#endif
	disply_elapse_time = timer.Elapsed();
	if (display_rate<1000)
		printf("\rElapsed [%.1fs] Pkts [%d] Lost [%d, %.2f%%] Rate [%.2fbps] Jitter [%.3fms]", disply_elapse_time / 1000, display_pkts, display_lost_pkts, display_lost_rate, display_rate, display_jitter);
	else if (display_rate >= 1000 && display_rate < 1000 * 1000)
		printf("\rElapsed [%.1fs] Pkts [%d] Lost [%d, %.2f%%] Rate [%.2fkbps] Jitter [%.3fms]", disply_elapse_time / 1000, display_pkts, display_lost_pkts, display_lost_rate, display_rate / 1000, display_jitter);
	else if (display_rate >= 1000 * 1000 && display_rate < 1000 * 1000 * 1000)
		printf("\rElapsed [%.1fs] Pkts [%d] Lost [%d, %.2f%%] Rate [%.2fMbps] Jitter [%.3fms]", disply_elapse_time / 1000, display_pkts, display_lost_pkts, display_lost_rate, display_rate / 1000 / 1000, display_jitter);
	else
		printf("\rElapsed [%.1fs] Pkts [%d] Lost [%d, %.2f%%] Rate [%.2fbps] Jitter [%.3fms]", disply_elapse_time / 1000, display_pkts, display_lost_pkts, display_lost_rate, display_rate, display_jitter);
#ifdef WIN32
	lck.unlock();
#endif
	cout.flush();

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

bool TCP_Server_Send(SOCKET &sender_sock)
{
	int iResult = 0;
	lck.lock();
	tcp_client_num++;
	lck.unlock();

	//-------------Receiving the request data and configure first---------//
	printf("\nReceiving the metadata\n");
	char *temp_recv_buf = (char*)malloc(30);
	memset(temp_recv_buf, 0, 30);
	iResult = recv(sender_sock, temp_recv_buf, 30, 0);

	char metadata[30], temp_pkg_size[10], temp_pkg_num[10], temp_rate[10];
	memcpy(metadata, temp_recv_buf, 30);
	for (int i = 0; i < 10; i++)
	{
		temp_pkg_size[i] = metadata[i];
		temp_pkg_num[i] = metadata[10 + i];
		temp_rate[i] = metadata[20 + i];
	}
	int pkg_size, pkg_num, rate;
	pkg_size = atoi(temp_pkg_size);
	pkg_num = atoi(temp_pkg_num);
	rate = atoi(temp_rate);

	//-------------Entering sender mode and send data--------------------//

	//create Timer
	ES_FlashTimer timer = ES_FlashTimer();
	long total_sent_bytes = 0, last_total_bytes = 0;
	double current_rate = 0, duration = 0, last_dur = 0;
	long old_jit_time = 0, starting_time = 0;
	double jitter_old = 0, jitter_new = 0;

	// buffer
	char *sendbuf = (char*)malloc(pkg_size);
	int sendbuflen = pkg_size;
	memset(sendbuf, 0, sendbuflen);
	int i = 1;
	//memcpy(sendbuf, &i, sendbuflen);
	bool infinite_pkg = false;
	long seq_num = 1;

	if (pkg_num == 0)
		infinite_pkg = true;

	double gap = (pkg_size*1.0 / rate) * 1000; //in ms unit

	timer.Start();
	starting_time = timer.Elapsed();
	old_jit_time = starting_time;

	int temp_iResult = 0;

	while (pkg_num > 0 || infinite_pkg)
	{
		//add sequence number
		char temp_num[8];
#ifdef WIN32
		_itoa_s(seq_num, temp_num, 10);
#else
		sprintf(temp_num, "%d", seq_num);
#endif
		for (int j = 0; j < 8; j++)
			sendbuf[j] = temp_num[j];

		//TCP boundary maintain
		int byte_send = 0;
		while (byte_send < sendbuflen)
		{
			//iResult = send(sock, sendbuf, sendbuflen, 0);
			iResult = send(sender_sock, sendbuf + byte_send, sendbuflen - byte_send, 0);
			last_dur = timer.Elapsed();
			if (iResult > 0)
			{
				total_sent_bytes += iResult;
				temp_iResult += iResult;
				byte_send += iResult;
			}

			if (iResult == SOCKET_ERROR) {
				error_handling("send failed with error: ", WSAGetLastError());
				lck.lock();
				tcp_client_num--;
				lck.unlock();
				closesocket(sender_sock);
				return false;
			}
		}

		if (--pkg_num == 0)
			break;
		seq_num++;

		//rate controlling from second packet
		if (rate > 0 && seq_num > 1)
		{
			//check current rate
			duration = timer.Elapsed();
			current_rate = total_sent_bytes*1.0 / (duration / 1000);

			//rate control
			if (gap*seq_num - last_dur > 0)
				Sleep(gap*seq_num - last_dur);
			else
				Sleep(gap);
		}
		else if (rate == 0 && seq_num > 1)
		{
			//check current rate
			duration = timer.Elapsed();
			current_rate = total_sent_bytes*1.0 / (duration / 1000);
		}
		else if (seq_num == 1)
		{
			duration = timer.Elapsed();
			current_rate = pkg_size / 1;
			if (gap - duration > 0)
				Sleep(gap - duration);
			else
				Sleep(10);

		}

		last_total_bytes += temp_iResult;
		temp_iResult = 0;

		//update the display attributes
		lck.lock();
		aggregate_rate += current_rate;
		lck.unlock();
	}
	closesocket(sender_sock);
	lck.lock();
	tcp_client_num--;
	lck.unlock();
	return true;
}

bool TCP_Server_Handling(int tcp_port)
{
	int iResult = 0;
	struct sockaddr_in client;
	socklen_t socksize = sizeof(struct sockaddr_in);

	//create socket
	SOCKET listen_sock; //receiving request

	sockaddr_in TCP_receiver_addr;
	TCP_receiver_addr.sin_family = AF_INET;
	TCP_receiver_addr.sin_port = htons(tcp_port);
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
	iResult = bind(listen_sock, (SOCKADDR *)&TCP_receiver_addr, sizeof(TCP_receiver_addr));
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

	//thread vector
	//vector<std::thread> thread_vector;
	
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

		std::thread (TCP_Server_Send, sender_sock).detach();
	}

	//Don't need the listening socket
	closesocket(listen_sock);

	// shutdown the connection since no more data will be sent
	packet_send_not_finished = false;
#ifdef WIN32
	WSACleanup();
#endif
	printf("\nConnetion closing...\n");
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

	//binding socket
	iResult = ::bind(sock, (sockaddr *)&UDP_Reiceiver_addr, sizeof(UDP_Reiceiver_addr));
	if (iResult == SOCKET_ERROR)
	{
		error_handling("Unable to bind with error: ", WSAGetLastError());
		closesocket(sock);
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
	printf("\npacket request send to %s:%d!\n", remote_host,remote_port);
	
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

bool UDP_Server_Send(sockaddr_in client_info, int pkg_size,int pkg_num,int rate)
{
	int iResult = 0;
	lck.lock();
	udp_client_num++;
	lck.unlock();

	SOCKET send_sock = INVALID_SOCKET;

	// buffer
	//char sendbuf[DEFAULT_BUFLEN];
	char *sendbuf = (char*)malloc(pkg_size);
	int sendbuflen = pkg_size;
	memset(sendbuf, 0, sendbuflen);

	bool infinite_pkg = false;
	long seq_num = 1;
	ES_FlashTimer timer = ES_FlashTimer();
	long total_sent_bytes = 0;
	double current_rate = 0, duration = 0, last_dur = 0;
	long old_jit_time = 0, starting_time = 0;

	sockaddr_in UDP_Reiceiver_addr;
	UDP_Reiceiver_addr.sin_family = AF_INET;
	UDP_Reiceiver_addr.sin_port = client_info.sin_port;
	UDP_Reiceiver_addr.sin_addr.s_addr = client_info.sin_addr.s_addr;

	send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (send_sock == SOCKET_ERROR)
	{
		error_handling("SOCKET create error: ", WSAGetLastError());
		closesocket(send_sock);
		lck.lock();
		udp_client_num--;
		lck.unlock();
		return false;
	}

	if (pkg_num == 0)
		infinite_pkg = true;

	double gap = (pkg_size *1.0 / rate) * 1000; //in ms unit

	timer.Start();
	start_counting_time = true;
	starting_time = timer.Elapsed();
	old_jit_time = starting_time;
	while (pkg_num>0 || infinite_pkg)
	{
		//add sequnce_number
		char temp_num[8];
#ifdef WIN32
		_itoa_s(seq_num, temp_num, 10);
#else
		sprintf(temp_num, "%d", seq_num);
#endif
		for (int j = 0; j < 8; j++)
			sendbuf[j] = temp_num[j];

		iResult = sendto(send_sock, sendbuf, sendbuflen, 0, (sockaddr*)&UDP_Reiceiver_addr, sizeof(UDP_Reiceiver_addr));
		//printf("Packet Send to %s:%d", inet_ntoa(UDP_Reiceiver_addr.sin_addr), ntohs(UDP_Reiceiver_addr.sin_port));
		last_dur = timer.Elapsed();
		if (iResult == SOCKET_ERROR)
		{
			error_handling("sendto() failed with error: ", WSAGetLastError());
			lck.lock();
			tcp_client_num--;
			lck.unlock();
			closesocket(send_sock);
			return false;
		}

		//rate controlling
		if (rate > 0 && seq_num > 1)
		{
			//check current rate
			duration = timer.Elapsed();
			current_rate = total_sent_bytes * 1.0 / (duration / 1000);

			//rate control
			if (gap*seq_num - last_dur>0)
				Sleep(gap*seq_num - last_dur);
			else
				Sleep(gap);
		}
		else if (rate == 0 && seq_num > 1)
		{
			//check current rate
			duration = timer.Elapsed();
			current_rate = total_sent_bytes * 1.0 / (duration / 1000);
		}
		else if (seq_num == 1)
		{
			duration = double(timer.Elapsed());
			current_rate = pkg_size;
			Sleep(gap - duration);
		}

		total_sent_bytes += iResult;
		if (--pkg_num == 0)
			break;
		seq_num++;

		//update the display attributes
		lck.lock();
		aggregate_rate += current_rate;
		lck.unlock();

	}

	//update the display attributes
	lck.lock();
	udp_client_num--;
	lck.unlock();

	packet_send_not_finished = false;

	closesocket(send_sock);
	return true;
}

bool UDP_Server_Handling(int udp_port)
{
	int iResult = 0;
	struct sockaddr_in client;
	socklen_t client_socksize = sizeof(struct sockaddr_in);

	//create socket
	SOCKET server_sock; //receiving request

	sockaddr_in UDP_Reiceiver_addr;
	socklen_t addr_len = sizeof(UDP_Reiceiver_addr);

	UDP_Reiceiver_addr.sin_family = AF_INET;
	UDP_Reiceiver_addr.sin_port = htons(udp_port);
	UDP_Reiceiver_addr.sin_addr.s_addr = INADDR_ANY;

	server_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (server_sock == SOCKET_ERROR)
	{
		error_handling("SOCKET create error: ", WSAGetLastError());
		closesocket(server_sock);
#ifdef WIN32
		WSACleanup();
#endif
		return false;
	}

	//binding socket
	iResult = ::bind(server_sock, (sockaddr *)&UDP_Reiceiver_addr, sizeof(UDP_Reiceiver_addr));
	if (iResult == SOCKET_ERROR)
	{
		error_handling("Unable to bind with error: ", WSAGetLastError());
		closesocket(server_sock);
#ifdef WIN32
		WSACleanup();
#endif
		return false;
	}
	printf("Waiting for request...\n");

	//UDP buffer
	char *recvbuf = (char*)malloc(30);
	memset(recvbuf, 0, 30);

	while (1)
	{
		iResult = recvfrom(server_sock, recvbuf, 30, 0, (sockaddr*)&client, &client_socksize);
		printf("\nPacket Received!\n");
		char metadata[30], temp_pkg_size[10], temp_pkg_num[10], temp_rate[10];
		memcpy(metadata, recvbuf, 30);
		for (int i = 0; i < 10; i++)
		{
			temp_pkg_size[i] = metadata[i];
			temp_pkg_num[i] = metadata[10 + i];
			temp_rate[i] = metadata[20 + i];
		}
		int pkg_size, pkg_num, rate;
		pkg_size = atoi(temp_pkg_size);
		pkg_num = atoi(temp_pkg_num);
		rate = atoi(temp_rate);

		std::thread(UDP_Server_Send, client, pkg_size, pkg_num, rate).detach();
	}

	//Don't need the listening socket
	closesocket(server_sock);

	// shutdown the connection since no more data will be sent
	packet_send_not_finished = false;
#ifdef WIN32
	WSACleanup();
#endif
	printf("\nConnetion closing...\n");
	return true;
}

bool Server_mode(int ref_inter, int tcp_port, int udp_port)
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

	//TCP thread
	std::thread th_tcp(TCP_Server_Handling, tcp_port);
	printf("TCP Connection thread created\n");

	//UDP Thread
	std::thread th_udp(UDP_Server_Handling, udp_port);
	printf("UDP Connection thread created\n");

	//Display Thread
	printf("Dssplay Connection thread created\n");
	std::thread th_display(display_clients_num, ref_inter);

	th_tcp.join();
	th_udp.join();
	th_display.join();

	return true;
}

int main(int argc, char *argv[])
{
	string mode = "", protocol = "", hostname = "";
	char* remote_host;
	int refresh_interval = 10, remote_port = 2000, packet_size = 0, num = 0, local_port = 2000;
	int tcp_port = 0,udp_port = 0;
	double rate = 0;

	if (argc <= 2)
	{
		printf("%s\n", "Incorrect input format.\n\nUsage: NetProbe.exe[mode:s/r/h] <parameters depending on mode>\n\n========================================================================\n's' [ref_int] [remote_host] [remote_port] [protocol] [pkt_size] [rate] [num]\n'r' [ref_int] [local_host] [local_port] [protocol] [pkt_size]\n'h' [hostname]\n========================================================================");
		printf("\n");
		exit(0);
	}
	else
		mode = argv[1];

	int iResult = 0;

	if (mode == "c")
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
		if (protocol == "TCP")
			TCP_Client(remote_port, remote_host, refresh_interval, packet_size, rate, num);
		else if (protocol == "UDP")
			UDP_Client(remote_port, remote_host, refresh_interval, packet_size, rate, num);

	}
	else if (mode == "s")
	{
		printf("Server Mode\n");
		if (!check_arg_num(argc, 's')){
#ifdef WIN32
			WSACleanup();
#endif
			return false;
		}
		refresh_interval = atoi(argv[2]);
		tcp_port = atoi(argv[3]);
		udp_port = atoi(argv[4]);

		Server_mode(refresh_interval, tcp_port, udp_port);
	}
	else
	{
		printf("No such mode\n");
	}
	system("Pause");
	return 0;
}

