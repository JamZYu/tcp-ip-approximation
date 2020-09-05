#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <iostream>
#include "packet_info.h"
#include <deque>
#include <sys/time.h>
#include <thread>
#include <mutex>

using namespace std;

//#define MAXSIZE 1472  // MTU = 1500 including IPv4 header (20 bytes), UDP header (8 bytes) 
// #define SWS	
// #define SEQ_NUM
// #define FRAME_SIZE

int RTT = 40;
int sockfd;
struct addrinfo hints, *servinfo, *p;
int rv;
bool th_end = false;

struct window_content_t
{
	bool is_ack;
	packet_t packet;
	timeval send_time;
	bool sent;
};


deque<window_content_t> window;

mutex win_lock;

void send_wrapper(packet_t &packet)
{
	if (sendto(sockfd, &packet, MAXSIZE, 0, p->ai_addr, p->ai_addrlen) < 0)
	{
		perror("sendto");
		close(sockfd);
		exit(1);
	}
}


bool valid(timeval tv)
{
	timeval now;
	gettimeofday(&now, NULL);
	int diff_ms = (((now.tv_sec - tv.tv_sec) * 1000000) +
				   (now.tv_usec - tv.tv_usec)) /
				  1000;
	//printf(" %d\n", diff_ms);
	if (diff_ms > 2 * RTT)
		return false;
	return true;
}


bool populate_window(FILE* fp, size_t window_size, unsigned long long int &bytesToTransfer, unsigned long long int &next_seq_number)
{
	while (window.size() < window_size)
	{
		packet_t packet;
		size_t numbytes;
		numbytes = fread(packet.data, 1, (MAXDATASIZE < bytesToTransfer? MAXDATASIZE: bytesToTransfer), fp);
		packet.header.type = DATA;
		packet.header.data_size = numbytes;
		packet.header.seq_num = next_seq_number;
		window_content_t window_content;
		window_content.is_ack = false;
		window_content.packet = packet;
		send_wrapper(window_content.packet);
		//printf("Sent %d\n", window_content.packet.header.seq_num);
		gettimeofday(&window_content.send_time, NULL);
		window.push_back(window_content);
		if (numbytes < MAXDATASIZE)
			return true;
		bytesToTransfer -= numbytes;
		next_seq_number++;
	}
	return false;
}

void resend_packets()
{
	for (auto &window_content : window)
	{
		if (!window_content.is_ack)
		{
			//printf("check seq %d", window_content.packet.header.seq_num);
			if (!valid(window_content.send_time))
			{
				printf("lost packet %d", window_content.packet.header.seq_num);
				send_wrapper(window_content.packet);
				gettimeofday(&window_content.send_time, NULL);
			}
		}
	}
}

void receive(int input)
{
	struct timespec tv;
	tv.tv_sec = 0;
    tv.tv_nsec = 20 * 1000 * 1000 / 500; //RTT / 500
	packet_t packet;
	while (!th_end && recvfrom(sockfd, &packet.header, sizeof(header_t), 0, p->ai_addr, &p->ai_addrlen))
	{
		if (packet.header.type == ACK)
		{
			win_lock.lock();
			int index = packet.header.seq_num - window.front().packet.header.seq_num;
			//printf("index %d, rece %d, exist. %d\n", index, packet.header.seq_num, window.front().packet.header.seq_num);
			if (packet.header.seq_num < window.front().packet.header.seq_num || window.empty())
			{
				win_lock.unlock();
				continue;
			}
			if (window[index].packet.header.seq_num != packet.header.seq_num)
				cerr << "BUGG" << endl;
			else
			{
				for (int i = 0 ; i <= index; i++)
					window[i].is_ack = true;
				//printf("rece ack of %d\n", packet.header.seq_num);
				while (!window.empty() && window.front().is_ack)
				{
					window.pop_front();
				}
			}
			while (window.empty())
			{
				if (th_end)
				{
					win_lock.unlock();
					break;
				}
				win_lock.unlock();
				nanosleep(&tv, 0);
				win_lock.lock();
			}
			win_lock.unlock();
		}
		nanosleep(&tv, 0);
	}
}


void reliablyTransfer(char* hostname, char* hostUDPport, char* filename, unsigned long long int bytesToTransfer) {
	//int oribytesToTransfer = bytesToTransfer;
	struct timespec tv;
	size_t window_size = SWS;
	tv.tv_sec = 0;
	unsigned long long int thresh = 10000000;
	cerr << bytesToTransfer << endl;
	cerr << thresh << endl;

	if (bytesToTransfer > thresh)
		tv.tv_nsec = 20 * 1000 * 1000 / 500; //RTT / 500
	else 
		tv.tv_nsec = 20 * 1000 * 1000 / 100; //RTT / 500

	cerr << tv.tv_nsec << endl;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	if ((rv = getaddrinfo(hostname, hostUDPport, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return;
	}

	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("socket");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "failed to bind socket\n");
		return;
	}

	freeaddrinfo(servinfo);

	FILE* fp = fopen(filename, "rb");
	if (fp == NULL) {
		fprintf(stderr, "Failed to open file: %s\n", filename);
		exit(1);
	}

	unsigned long long next_seq_num = 0;
	bool fileloaded = false;
	thread p1(receive, 1);
	while (true)
	{
		win_lock.lock();
		if (!fileloaded)
			fileloaded = populate_window(fp, window_size, bytesToTransfer, next_seq_num);
		resend_packets();
		if (fileloaded && window.empty())
		{
			win_lock.unlock();
			th_end = true;
			break;
		}
		win_lock.unlock();
		nanosleep(&tv, 0);
	}
	p1.join();
	fclose(fp);
	close(sockfd);
}

int main(int argc, char** argv) {
	unsigned long long int numBytes;
	
	if(argc != 5){
		fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
		exit(1);
	}

	numBytes = atoll(argv[4]);
	
	reliablyTransfer(argv[1], argv[2], argv[3], numBytes);
} 
