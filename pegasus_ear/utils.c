#include "utils.h"
#include <stdint.h>

void ear_xxd(const void* data, EAR_Size size, EAR_Size* base_offset, FILE* fp) {
	const uint8_t* bytes = data;
	EAR_Size offset, byte_idx, col, base;
	
	base = base_offset ? *base_offset : 0;
	
	for(offset = 0; offset < size; offset += 0x10) {
		// Offset indicator
		col = fprintf(fp, "%04x:", base + offset);
		
		// 8 columns of 2 hex encoded bytes
		for(byte_idx = 0; byte_idx < 0x10 && offset + byte_idx < size; byte_idx++) {
			if(byte_idx % 2 == 0) {
				col += fprintf(fp, " ");
			}
			
			col += fprintf(fp, "%02x", bytes[offset + byte_idx]);
		}
		
		// Align column up to ASCII view
		if(col < 47) {
			fprintf(fp, "%*s", 47 - col, "");
		}
		
		// ASCII view
		for(byte_idx = 0; byte_idx < 0x10 && offset + byte_idx < size; byte_idx++) {
			uint8_t c = bytes[offset + byte_idx];
			
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
