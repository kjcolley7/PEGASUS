//
//  peg_rev.c
//  PegasusEar
//
//  Created by Kevin Colley on 11/7/2020.
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
#include <errno.h>
#include "common/macros.h"
#include "common/dynamic_string.h"
#include "server/pegasus_server.h"


#define PEG_REV_INPUT_FILE "LicenseChecker.peg"


typedef struct PegRevPlugin {
	PegPlugin base;
	EAR* ear;
	char* flag;
	size_t flag_len;
	size_t flag_read_pos;
} PegRevPlugin;


static bool pegrev_portWrite(void* portWrite_cookie, uint8_t port_number, EAR_Byte byte) {
	PegRevPlugin* ctx = portWrite_cookie;
	
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
		
		default:
			return false;
	}
	
	return true;
}

static bool pegrev_portRead(void* portRead_cookie, uint8_t port_number, EAR_Byte* out_byte) {
	PegRevPlugin* ctx = portRead_cookie;
	
	switch(port_number) {
		case 0:
			// Read byte from stdin
			if(read(STDIN_FILENO, out_byte, 1) != 1) {
				if(!g_interrupted) {
					perror("read");
				}
				return false;
			}
			break;
		
		case 0xF:
			if(ctx->flag_read_pos >= ctx->flag_len) {
				return false;
			}
			*out_byte = ctx->flag[ctx->flag_read_pos++];
			break;
	}
	
	if(ctx->ear->debug_flags & DEBUG_VERBOSE) {
		fprintf(stderr, "RDB (%hhu) -> 0x%02X\n", port_number, *out_byte);
	}
	
	return true;
}


static void pegrev_destroy(PegPlugin* plugin) {
	PegRevPlugin* ctx = (PegRevPlugin*)plugin;
	free(ctx->flag);
	free(ctx);
}


PegPlugin* PegPlugin_Init(EAR* ear, PegasusLoader* pegload) {
	(void)pegload;
	
	PegRevPlugin* ctx = calloc(1, sizeof(*ctx));
	if(!ctx) {
		return NULL;
	}
	
	ctx->ear = ear;
	
	// We have a destructor but not an onLoaded function
	ctx->base.fn_destroy = &pegrev_destroy;
	
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
	
	ctx->flag = strdup(flag_data);
	ctx->flag_len = strlen(ctx->flag);
	ctx->flag_read_pos = 0;
	
	// Set CPU port r/w function
	EAR_setPorts(ctx->ear, &pegrev_portRead, &pegrev_portWrite, ctx);
	
	return &ctx->base;
}


int main(int argc, char** argv, char** envp) {
	(void)argc;
	
	// This challenge simply runs the PEGASUS program and serves its port 0 I/O over the
	// connected TCP socket. Easiest way to do that is to invoke runpeg with this module
	// as a plugin.
	char* runpeg_argv[] = {
		"./runpeg", "--plugin", argv[0], PEG_REV_INPUT_FILE, NULL
	};
	if(execve("./runpeg", runpeg_argv, envp) < 0) {
		fprintf(stderr, "Failed to execute runpeg: %s\n", strerror(errno));
	}
	
	return EXIT_FAILURE;
}
