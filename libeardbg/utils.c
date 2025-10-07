#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include "common/macros.h"

void ear_xxd(
	const void* data, uint32_t size, uint32_t* base_offset,
	const char* prefix, int addr_digits, FILE* fp
) {
	const uint8_t* bytes = data;
	uint32_t offset, byte_idx, col, ascii_col, addr;
	uint32_t base = base_offset ? *base_offset : 0;
	uint8_t c;
	
	if(addr_digits == 4) {
		ascii_col = 47;
	}
	else {
		ASSERT(addr_digits == 6);
		ascii_col = 50;
	}
	
	if(prefix) {
		ascii_col += strlen(prefix);
	}
	
	for(offset = 0; offset < size; offset += 0x10) {
		col = 0;
		
		// Prefix
		if(prefix) {
			col += fprintf(fp, "%s", prefix);
		}
		
		// Offset indicator
		addr = base + offset;
		if(addr_digits == 4) {
			ASSERT((addr >> 16) == 0);
			col += fprintf(fp, "%04x|", addr);
		}
		else {
			ASSERT((addr >> 24) == 0);
			col += fprintf(fp, "%02x:%04x|", addr >> 16, addr & 0xffff);
		}
		
		// 8 columns of 2 hex encoded bytes
		for(byte_idx = 0; byte_idx < 0x10 && offset + byte_idx < size; byte_idx++) {
			if(byte_idx % 2 == 0) {
				col += fprintf(fp, " ");
			}
			
			col += fprintf(fp, "%02x", bytes[offset + byte_idx]);
		}
		
		// Align column up to ASCII view
		if(col < ascii_col) {
			fprintf(fp, "%*s", ascii_col - col, "");
		}
		
		// ASCII view
		for(byte_idx = 0; byte_idx < 0x10 && offset + byte_idx < size; byte_idx++) {
			c = bytes[offset + byte_idx];
			if(0x20 <= c && c <= 0x7e) {
				// Standard printable ASCII
				fprintf(fp, "%c", c);
			}
			else {
				// Don't try to print this character as it isn't easily printable ASCII
				fprintf(fp, ".");
			}
		}
		
		// End of hexdump line
		fprintf(fp, "\n");
	}
	
	// Update file position tracker
	if(base_offset) {
		*base_offset += size;
	}
}
