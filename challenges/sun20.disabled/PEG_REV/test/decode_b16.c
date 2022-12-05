#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef uint16_t u16;
typedef uint8_t u8;
typedef uint8_t lestring;

u16 decode_b16(
	lestring* str,             // A0
	u16 size,                  // A1
	u8* buf                    // A2
) {
	while(size-- > 0) {
		u8 c = *str;           // R4
		if(!(c & 0x80)) {
			return size;
		}
		++str;
		
		c -= 'k' | 0x80;
		if(c > 0xF) {
			return size;
		}
		
		u8 byte = *str++;      // R6
		u8 d = byte & 0x7F;    // R5
		d -= 'A';
		if(d > 0xF) {
			return size;
		}
		
		if(d == byte) {
			size = 0;
		}
		*buf++ = c | (d << 4);
	}
	return 0;
}


int main(void) {
	char in[31] = {0};
	fgets(in, sizeof(in), stdin);
	char* end = strchr(in, '\n');
	if(end) {
		*end = '\0';
	}
	if(in[0] == '\0') {
		return -1;
	}
	
	lestring in_les[30] = {0};
	size_t i;
	for(i = 0; in[i + 1] != '\0'; i++) {
		in_les[i] = (uint8_t)in[i] | 0x80;
	}
	in_les[i] = (uint8_t)in[i];
	size_t in_len = i + 1;
	
	uint8_t out[sizeof(in_les) / 2] = {0};
	
	u16 ret = decode_b16(in_les, sizeof(out), out);
	printf("decode_b16 returned %u\n", ret);
	
	for(i = 0; i < in_len; i++) {
		printf("in_les[%zu] = 0x%02X ('%c')\n", i, in_les[i], in_les[i] & 0x7f);
	}
	for(i = 0; i < sizeof(out); i++) {
		printf("out[%zu] = 0x%02X\n", i, out[i]);
	}
	
	return 0;
}
