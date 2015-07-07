#pragma once
#include <stdint.h>

typedef struct {
	uint8_t type;
	uint8_t user;
	uint16_t seq;
	uint16_t len;
	uint8_t data[8192];
} packet_t, *packet_p;

//#define PACKET_HELLO    1
//#define PACKET_WELCOME  2
//#define PACKET_DATA     3
//#define PACKET_JOIN     4
//#define PACKET_BYE      5
const unsigned char PACKET_HELLO	= 1;
const unsigned char PACKET_WELCOME	= 2;
const unsigned char PACKET_DATA		= 3;
const unsigned char PACKET_JOIN		= 4;
const unsigned char PACKET_BYE		= 5;
