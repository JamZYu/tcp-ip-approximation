#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include "packet_info.h"
#include <iostream>
#include <deque>
#include <sys/time.h>
#include <mutex>
#include <thread>

using namespace std;

#define MAXSIZE 1472  // MTU = 1500 including IPv4 header (20 bytes), UDP header (8 bytes) 
// #define RWS 
// #define SEQ_NUM
// #define FRAME_SIZE

int sockfd;
struct addrinfo hints, *servinfo, *p;
int rv;
struct sockaddr_storage their_addr;
socklen_t addr_len;

bool address_defined = false;

unsigned long long expected_seq_num = 0;

mutex esn_lock;
bool th_end = false;

struct window_content_t
{
	packet_t packet;
	bool received;
};

void send_wrapper(packet_t &packet)
{
	if (!address_defined) return;
	if (sendto(sockfd, &packet.header, sizeof(header_t), 0, (struct sockaddr *)&their_addr, addr_len) < 0)
	{
		perror("sendto");
		close(sockfd);
		exit(1);
	}
}

void populate_window(deque<window_content_t>& window, size_t window_size)
{
	while (window.size() < window_size)
	{
		window_content_t window_content;
		window_content.received = false;
		window.push_back(window_content);
	}
}

bool receive(deque<window_content_t>& window, int window_size, FILE* fp)
{
	packet_t packet;
	//printf("rec\n");
	if (recvfrom(sockfd, &packet, MAXSIZE, 0, (struct sockaddr*) &their_addr, &addr_len))
	{
		//printf("inrec\n");
		address_defined = true;
		if (packet.header.type == DATA)
		{
			unsigned long long index = packet.header.seq_num - expected_seq_num;
			//printf("index, %d\n", index);
			if (index >= window_size)
				return false;
			else
			{
				window[index].packet = packet;
				window[index].received = true;
				while (!window.empty() && window.front().received)
				{
					if (window.front().packet.header.type == DATA)
					{
						fwrite(window.front().packet.data, 1, window.front().packet.header.data_size, fp);
						packet.header.type = ACK;
						esn_lock.lock();
						packet.header.seq_num = expected_seq_num;
						//printf("Rece %d\n", packet.header.seq_num);
						send_wrapper(packet);
						expected_seq_num++;
						esn_lock.unlock();
						if (window.front().packet.header.data_size < MAXDATASIZE)
							return true;
					}
					window.pop_front();
				}
			}
		}
	}
	return false;
}

void send_seq_number(int input)
{
	struct timespec tv;
	tv.tv_sec = 0;
    tv.tv_nsec = 20 * 1000 * 1000 / 100; //RTT / 500
	while(!th_end)
	{
		if (expected_seq_num > 0)
		{
			esn_lock.lock();
			packet_t packet;
			packet.header.type = ACK;
			packet.header.seq_num = expected_seq_num - 1;
			send_wrapper(packet);
			//printf("Rece %d\n", packet.header.seq_num);
			esn_lock.unlock();
		}
		nanosleep(&tv, 0);
	}
}


void reliablyReceive(char* myUDPport, char* destinationFile) {
	deque<window_content_t> window;

	struct timespec tv;
	tv.tv_sec = 0;
    tv.tv_nsec = 20 * 1000 * 1000 / 100; //RTT / 500

	int window_size = RWS;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;  // IPv4
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	if ((rv = getaddrinfo(NULL, myUDPport, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return;
	}

	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("listener: socket");
			continue;
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("listener: bind");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "failed to bind socket\n");
		return;
	}

	freeaddrinfo(servinfo);

	FILE* fp = fopen(destinationFile, "wb");
	if (fp == NULL) {
		fprintf(stderr, "Failed to open file: %s\n", destinationFile);
		exit(1);
	}

	addr_len = sizeof their_addr;
	bool filecompleted = false;

	thread p1(send_seq_number, 0);

	while (!filecompleted)
	{
		populate_window(window, window_size);
		filecompleted = receive(window, window_size, fp);
	}
	th_end = true;
	p1.join();
	fclose(fp);
	close(sockfd);
}

int main(int argc, char** argv) {
	if(argc != 3) {
		fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
		exit(1);
	}

	reliablyReceive(argv[1], argv[2]);
}
