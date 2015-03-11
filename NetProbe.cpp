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
#include "es_TIMER.H"
using namespace std;

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "27015"

#ifdef WIN32
std::mutex m;
unique_lock<mutex> lck(m, defer_lock);
#else
std::mutex m;
unique_lock<mutex> lck(m, defer_lock);
#endif
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
	if (mode == 's' && argc == 9)
		result = true;
	else if (mode == 'r'&& argc == 7)
		result = true;
	else if (mode == 'h'&& argc == 3)
		result = true;
	else
		printf("%s\n", warning);
	return result;
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

bool TCP_Send(int remote_port, char* remote_host, int ref_inter, int pkg_size, double rate, int pkg_num)
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

	//create new thread
	start_counting_time = true;
	std::thread th(display_send, ref_inter);

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
		th.join();
		return false;
	}
	//printf("Connecting Succesfully!\n");
	if (pkg_num == 0)
		infinite_pkg = true;

	double gap = (pkg_size / rate) * 1000; //in ms unit

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
			iResult = send(sock, sendbuf + byte_send, sendbuflen - byte_send, 0);
			last_dur = timer.Elapsed();
			if (iResult > 0)
			{
				total_sent_bytes += iResult;
				temp_iResult += iResult;
				byte_send += iResult;
			}

			if (iResult == -1) {
				error_handling("send failed with error: ", WSAGetLastError());
				closesocket(sock);
#ifdef WIN32
				WSACleanup();
#endif
				th.join();
				return false;
			}
		}

		if (--pkg_num == 0)
			break;
		seq_num++;

		//counting jitter
		long temp_t = timer.Elapsed();
		double T = (temp_t - starting_time) *1.0 / seq_num;
		jitter_new = (jitter_old * (seq_num - 1) + temp_t - old_jit_time - T) *1.0 / seq_num;
		jitter_old = jitter_new;
		old_jit_time = temp_t;

		//rate controlling from second packet
		if (rate > 0 && seq_num > 1)
		{
			//check current rate
			duration = timer.Elapsed();
			current_rate = total_sent_bytes*1.0 / (duration / 1000);

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
		//duration = timer.Elapsed() / 1000;
		//disply_elapse_time = duration;
		display_pkts = seq_num;
		display_rate = current_rate;
		display_jitter = jitter_new;
		if (display_jitter < 0) display_jitter = 0;
		lck.unlock();
	}

	// shutdown the connection since no more data will be sent
	closesocket(sock);
#ifdef WIN32
	WSACleanup();
#endif
	th.join();
	printf("\nConnetion closing...\n");
	return true;

	}

bool TCP_recv(int local_port, char* local_host, int ref_interv, int pkg_size)
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
	SOCKET listen_sock; //receiving request
	SOCKET client_sock; //connect to client

	//create Timer
	ES_FlashTimer timer = ES_FlashTimer();

	// buffer
	char *recvbuf = (char *)malloc(pkg_size);
	int recvbuflen = pkg_size;

	long seq_num = 1;
	unsigned int lost_pkg_num = 0;
	double duration = 0, current_rate = 0;
	long total_recv_bytes = 0, total_pkg_num = 0;
	long old_jit_time = 0, starting_time = 0;
	double jitter_old = 0, jitter_new = 0;

	sockaddr_in TCP_receiver_addr;
	TCP_receiver_addr.sin_family = AF_INET;
	TCP_receiver_addr.sin_port = htons(local_port);
	TCP_receiver_addr.sin_addr.s_addr = INADDR_ANY;//inet_addr(local_host);

	listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_sock == INVALID_SOCKET)
	{
		error_handling("SOCKET create error: ", WSAGetLastError());
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
		error_handling("Unable to bind with error: ", WSAGetLastError());
		closesocket(listen_sock);
#ifdef WIN32
		WSACleanup();
#endif
		return false;
	}
	else
		printf("Binding successfully\n");
	//listen to the socket
	iResult = listen(listen_sock, SOMAXCONN);
	if (iResult == SOCKET_ERROR) {
		error_handling("listen failed with error: ", WSAGetLastError());
		closesocket(listen_sock);
#ifdef WIN32
		WSACleanup();
#endif
		return false;
	}
	client_sock = accept(listen_sock, NULL, NULL);
	if (client_sock == INVALID_SOCKET){
		error_handling("Accept failed with error: ", WSAGetLastError());
		closesocket(listen_sock);
#ifdef WIN32
		WSACleanup();
#endif
		return false;
	}
	else
		printf("Accept the connection successfully\n");
	//Don't need the listening socket
	closesocket(listen_sock);

	//add a new thread
	std::thread th(display_receive, ref_interv);

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
			iResult = recv(client_sock, recvbuf + byte_recv, recvbuflen - byte_recv, 0);
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
			printf("\nConnection closing...\n");
			break;
		}
		else if (iResult<0)
		{
			error_handling("Recv failed with error: ", WSAGetLastError());
			closesocket(client_sock);
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
	closesocket(client_sock);
#ifdef WIN32
	WSACleanup();
#endif
	return true;
}

bool UDP_send(int remote_port, char* remote_host, int ref_inter, int pkg_size, double rate, int pkg_num)
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
	SOCKET send_sock = INVALID_SOCKET;

	std::thread th(display_send, ref_inter);

	// buffer
	//char sendbuf[DEFAULT_BUFLEN];
	char *sendbuf = (char*)malloc(pkg_size);
	int sendbuflen = pkg_size;
	memset(sendbuf, 0, sendbuflen);
	int i = 1;
	//memcpy(sendbuf, &i, sendbuflen);

	bool infinite_pkg = false;
	long seq_num = 1;
	ES_FlashTimer timer = ES_FlashTimer();
	long total_sent_bytes = 0;
	double current_rate = 0, duration = 0, last_dur = 0;
	long old_jit_time = 0, starting_time = 0;
	double jitter_old = 0, jitter_new = 0;

	sockaddr_in UDP_Reiceiver_addr;
	UDP_Reiceiver_addr.sin_family = AF_INET;
	UDP_Reiceiver_addr.sin_port = htons(remote_port);
	UDP_Reiceiver_addr.sin_addr.s_addr = inet_addr(remote_host);

	send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (send_sock == SOCKET_ERROR)
	{
		error_handling("SOCKET create error: ", WSAGetLastError());
#ifdef WIN32
		WSACleanup();
#endif
		return false;
	}

	//add a new thread

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
		last_dur = timer.Elapsed();
		if (iResult == SOCKET_ERROR || iResult == 0)
		{
			error_handling("sendto() failed with error: ", WSAGetLastError());
			closesocket(send_sock);
#ifdef WIN32
			WSACleanup();
#endif
			th.join();
			return false;
		}

		//counting jitter
		long temp_t = timer.Elapsed();
		double T = (temp_t - starting_time) *1.0 / seq_num;
		jitter_new = (jitter_old * (seq_num - 1) + temp_t - old_jit_time - T) *1.0 / seq_num;
		jitter_old = jitter_new;
		old_jit_time = temp_t;

		//rate controlling
		if (rate > 0 && seq_num > 1)
		{
			//check current rate
			duration = timer.Elapsed();
			current_rate = total_sent_bytes * 1.0 / (duration / 1000);
			//printf("\nPack sent %d!\n", seq_num);
			//printf("Total bytes is %d and duration is %f\n", total_sent_bytes, duration);

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
			//printf("\nGap is %f and duration is %f\n", gap, duration);
			//printf("\nPack sent %d!\n", seq_num);
			//printf("Total bytes is %d and duration is %f\n", total_sent_bytes, duration);
			Sleep(gap - duration);
		}

		total_sent_bytes += iResult;
		if (--pkg_num == 0)
			break;
		seq_num++;
		//lost packet debug purpose
		//seq_num++;

		//update the display attributes
		lck.lock();
		//disply_elapse_time = duration;
		display_pkts = seq_num;
		display_rate = current_rate;
		display_jitter = jitter_new;
		if (display_jitter < 0) display_jitter = 0;
		lck.unlock();

	}

	//update the display attributes
	lck.lock();
	//disply_elapse_time = duration;
	display_pkts = seq_num;
	display_rate = current_rate;
	display_jitter = jitter_new;
	if (display_jitter < 0) display_jitter = 0;
	lck.unlock();

	packet_send_not_finished = false;

	closesocket(send_sock);
	th.join();
	return true;
}

bool UDP_recv(int local_port, char* local_host, int ref_interv, int pkg_size)
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
	SOCKET recv_sock = INVALID_SOCKET;

	// buffer
	//char sendbuf[DEFAULT_BUFLEN];
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

	sockaddr_in UDP_Reiceiver_addr;
#ifdef WIN32
	int addr_len = sizeof(UDP_Reiceiver_addr);
#else
	socklen_t addr_len = sizeof(UDP_Reiceiver_addr);
#endif
	UDP_Reiceiver_addr.sin_family = AF_INET;
	UDP_Reiceiver_addr.sin_port = htons(local_port);
	UDP_Reiceiver_addr.sin_addr.s_addr = INADDR_ANY;

	recv_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (recv_sock == SOCKET_ERROR)
	{
		error_handling("SOCKET create error: ", WSAGetLastError());
#ifdef WIN32
		WSACleanup();
#endif
		return false;
	}

	//binding socket
	iResult = ::bind(recv_sock, (sockaddr *)&UDP_Reiceiver_addr, sizeof(UDP_Reiceiver_addr));
	if (iResult == SOCKET_ERROR)
	{
		error_handling("Unable to bind with error: ", WSAGetLastError());
		closesocket(recv_sock);
#ifdef WIN32
		WSACleanup();
#endif
		return false;
	}
	printf("Waiting for data...\n");

	//add a new thread
	std::thread th(display_receive, ref_interv);
	start_counting_time = false;
	bool temp_start_time = false;

	while (1)
	{
		memset(recvbuf, 0, recvbuflen);
		iResult = recvfrom(recv_sock, recvbuf, recvbuflen, 0, (sockaddr*)&UDP_Reiceiver_addr, &addr_len);
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

		lost_pkg_num = seq_num - total_pkt_num;

		//---------------------------------------//

		if (iResult == SOCKET_ERROR)
		{
			error_handling("recvfrom() failed with error: ", WSAGetLastError());
			th.join();
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
		//duration = timer.Elapsed() / 1000;
		//disply_elapse_time = duration;
		display_pkts = seq_num - 1;
		display_rate = current_rate;
		display_lost_pkts = lost_pkg_num;
		display_lost_rate = lost_pkg_num*1.0 / total_pkt_num * 100;
		display_jitter = jitter_new;
		lck.unlock();
	}
	packet_send_not_finished = false;
	closesocket(recv_sock);
#ifdef WIN32
	WSACleanup();
#endif
	th.join();
	return true;
}

int main(int argc, char *argv[])
{
	string mode = "", protocol = "", hostname = "";
	char* remote_host;
	char* local_host;
	int refresh_interval = 10, remote_port = 2000, packet_size = 0, num = 0, local_port = 2000;
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

	if (mode == "s")
	{
		//argument initialization
		printf("Sender Mode\n");
		if (!check_arg_num(argc, 's')){
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
#ifdef WIN32
		if (protocol == "TCP")
			TCP_Send(remote_port, remote_host, refresh_interval, packet_size, rate, num);
		else if (protocol == "UDP")
			UDP_send(remote_port, remote_host, refresh_interval, packet_size, rate, num);
#else
		if (protocol == "TCP")
			TCP_Send(remote_port, remote_host, refresh_interval, packet_size, rate, num);
		else if (protocol == "UDP")
			UDP_send(remote_port, remote_host, refresh_interval, packet_size, rate, num);
#endif
	}
	else if (mode == "r")
	{
		printf("Receiver Mode\n");
		if (!check_arg_num(argc, 'r')){
#ifdef WIN32
			WSACleanup();
#endif
			return false;
		}
		refresh_interval = atoi(argv[2]);
		local_host = argv[3];
		local_port = atoi(argv[4]);
		protocol = argv[5];
		packet_size = atoi(argv[6]);
#ifdef WIN32
		if (protocol == "TCP")
			TCP_recv(local_port, local_host, refresh_interval, packet_size);
		else if (protocol == "UDP")
			UDP_recv(local_port, local_host, refresh_interval, packet_size);
#else
		if (protocol == "TCP")
			TCP_recv(local_port, local_host, refresh_interval, packet_size);
		else if (protocol == "UDP")
			UDP_recv(local_port, local_host, refresh_interval, packet_size);
#endif
	}
	else
	{
		printf("No such mode\n");
	}
	return 0;
}

