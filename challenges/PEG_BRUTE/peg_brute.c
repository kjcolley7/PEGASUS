//
//  peg_brute.c
//  PegasusEar
//
//  Created by Kevin Colley on 11/5/2020.
//

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include "common/macros.h"
#include "common/dynamic_string.h"
#include "server/pegasus_server.h"


typedef struct PegBrutePlugin {
	PegPlugin base;
	EAR* ear;
	dynamic_string port_buffer;
	char* flag;
	size_t flag_len;
	EAR_Byte correct_count;
} PegBrutePlugin;


static bool pegbrute_portWrite(void* portWrite_cookie, uint8_t port_number, EAR_Byte byte) {
	PegBrutePlugin* ctx = portWrite_cookie;
	
	if(ctx->ear->debug_flags & DEBUG_VERBOSE) {
		char ch[5] = {0};
		switch(byte) {
			case '\'':
				strcpy(ch, "\\'");
				break;
			
			case '\n':
				strcpy(ch, "\\n");
				break;
			
			case '\0':
				strcpy(ch, "\\0");
				break;
			
			case '\t':
				strcpy(ch, "\\t");
				break;
			
			default:
				if(0x20 <= byte && byte <= 0x7E) {
					ch[0] = byte;
				}
				else {
					snprintf(ch, sizeof(ch), "\\x%02X", byte);
				}
				break;
		}
		fprintf(stderr, "WRB (%hhu), '%s'\n", port_number, ch);
	}
	
	switch(port_number) {
		case 0:
			// Write byte to stdout
			if(write(STDOUT_FILENO, &byte, 1) != 1) {
				if(!g_interrupted) {
					perror("write");
				}
				return false;
			}
			break;
		
		case 1:
			// On newline, check the password and count how many characters were correct
			if(byte == '\n') {
				ctx->correct_count = 0;
				size_t i;
				for(i = 0; i < ctx->port_buffer.count && ctx->flag[i] != '\0'; i++) {
					if(ctx->port_buffer.elems[i] != ctx->flag[i]) {
						break;
					}
					
					++ctx->correct_count;
				}
				
				// When the password is correct, RDB (1) should return 0xFF
				if(ctx->correct_count == ctx->flag_len) {
					ctx->correct_count = 0xFF;
				}
				
				if(ctx->ear->debug_flags & DEBUG_VERBOSE) {
					fprintf(stderr, "TRY: '%s' -> %u\n", string_cstr(&ctx->port_buffer), ctx->correct_count);
				}
				
				// Mark port buffer as empty
				ctx->port_buffer.count = 0;
			}
			else {
				// Check if the buffer is full
				if(ctx->port_buffer.count >= ctx->flag_len) {
					return false;
				}
				
				string_appendChar(&ctx->port_buffer, byte);
			}
			break;
		
		default:
			return false;
	}
	
	return true;
}

static bool pegbrute_portRead(void* portRead_cookie, uint8_t port_number, EAR_Byte* out_byte) {
	PegBrutePlugin* ctx = portRead_cookie;
	
	if(port_number != 1) {
		return false;
	}
	
	*out_byte = ctx->correct_count;
	
	if(ctx->ear->debug_flags & DEBUG_VERBOSE) {
		fprintf(stderr, "RDB (%hhu) -> 0x%02X\n", port_number, *out_byte);
	}
	
	return true;
}


static void pegbrute_destroy(PegPlugin* plugin) {
	PegBrutePlugin* ctx = (PegBrutePlugin*)plugin;
	free(ctx->flag);
	string_clear(&ctx->port_buffer);
	free(ctx);
}


PegPlugin* PegPlugin_Init(EAR* ear, PegasusLoader* pegload) {
	(void)pegload;
	
	PegBrutePlugin* ctx = calloc(1, sizeof(*ctx));
	if(!ctx) {
		return NULL;
	}
	
	ctx->ear = ear;
	
	// We have a destructor but not an onLoaded function
	ctx->base.fn_destroy = &pegbrute_destroy;
	
	// Read flag into memory
	char flag_data[100];
	FILE* flag_fp = fopen("flag.txt", "r");
	if(!flag_fp) {
		fprintf(stderr, "Flag file missing! Expected flag.txt in the current directory.\n");
		fail();
	}
	if(!fgets(flag_data, sizeof(flag_data), flag_fp)) {
		fprintf(stderr, "Failed to read from flag file! Expected flag.txt in the current directory.\n");
		fail();
	}
	fclose(flag_fp);
	
	if(flag_data[0] == '\0') {
		fprintf(stderr, "Flag file empty! Expected flag.txt in the current directory.\n");
		fail();
	}
	
	char* flag_end = strchr(flag_data, '\n');
	if(flag_end != NULL) {
		*flag_end = '\0';
	}
	
	ctx->flag = strdup(flag_data);
	ctx->flag_len = strlen(ctx->flag);
	
	// Set CPU port r/w function
	EAR_setPorts(ctx->ear, &pegbrute_portRead, &pegbrute_portWrite, ctx);
	
	return &ctx->base;
}

int main(void) {
	if(!PegasusServer_dlopenAndServeWithPlugin(&PegPlugin_Init)) {
		return EXIT_FAILURE;
	}
	
	return EXIT_SUCCESS;
}
