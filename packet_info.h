#ifndef __PACKET_INFO_H__
#define __PACKET_INFO_H__

#define MAXSIZE 1472  // MTU = 1500 including IPv4 header (20 bytes), UDP header (8 bytes) 

#define DATA 0
#define ACK 1


struct header_t
{
    char type;
    size_t seq_num;
    size_t data_size;
};

#define MAXDATASIZE (MAXSIZE - sizeof(header_t))

struct packet_t
{
    header_t header;
    char data[MAXDATASIZE];
};

#define SWS 32;
#define RWS 32;



#endif
