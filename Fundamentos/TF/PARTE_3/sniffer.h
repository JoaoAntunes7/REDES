#ifndef SNIFFER_H
#define SNIFFER_H

#include <stdint.h>
    
struct ip_header{
	uint8_t ihl : 4;
    uint8_t version : 4;
    uint8_t tos;
    uint16_t total;
    uint16_t id;
    uint16_t frag_off;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t saddr;
    uint32_t daddr;
};

struct tcp_header{
    uint16_t sport;
    uint16_t dport;
    uint32_t seq;
    uint32_t ackn;
    uint8_t offset : 4;
    uint8_t reserved : 4;
    uint8_t code; //flags
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_prt;
};

struct udp_header{
    uint16_t sport;
    uint16_t dport;
    uint16_t len;
    uint16_t checksum;
};

struct dns_header{
	uint16_t id;
	uint16_t flags;
	uint16_t numQ;
	uint16_t numRR;
	uint16_t autoRR;
	uint16_t addRR;
	uint32_t data;
};

#endif //SNIFFER_H
