//
//  peg_pwn.c
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
#include "pegasus_ear/plugin.h"


#define PEG_PWN_INPUT_FILE "bof.peg"


typedef struct PegPwnPlugin {
	PegPlugin base;
	EAR_PortRead* next_read_fn;
	EAR_PortWrite* next_write_fn;
	void* next_cookie;
	EAR* ear;
	char* flag;
	size_t flag_len;
	size_t flag_read_pos;
} PegPwnPlugin;


static bool pegpwn_portWrite(void* portWrite_cookie, uint8_t port_number, EAR_Byte byte) {
	PegPwnPlugin* ctx = portWrite_cookie;
	if(port_number == 0) {
		if(ctx->next_write_fn != NULL) {
			return ctx->next_write_fn(ctx->next_cookie, port_number, byte);
		}
	}
	return false;
}

static bool pegpwn_portRead(void* portRead_cookie, uint8_t port_number, EAR_Byte* out_byte) {
	PegPwnPlugin* ctx = portRead_cookie;
	
	if(port_number != 0xF) {
		if(ctx->next_read_fn != NULL) {
			return ctx->next_read_fn(ctx->next_cookie, port_number, out_byte);
		}
		return false;
	}
	
	if(ctx->flag_read_pos >= ctx->flag_len) {
		return false;
	}
	*out_byte = ctx->flag[ctx->flag_read_pos++];
	
	if(ctx->ear->debug_flags & DEBUG_VERBOSE) {
		fprintf(stderr, "RDB (%hhu) -> 0x%02X\n", port_number, *out_byte);
	}
	
	return true;
}


static void pegpwn_destroy(PegPlugin* plugin) {
	PegPwnPlugin* ctx = (PegPwnPlugin*)plugin;
	free(ctx->flag);
	free(ctx);
}


PegPlugin* PegPlugin_Init(EAR* ear, PegasusLoader* pegload) {
	(void)pegload;
	
	PegPwnPlugin* ctx = calloc(1, sizeof(*ctx));
	if(!ctx) {
		return NULL;
	}
	
	ctx->ear = ear;
	
	// We have a destructor but not an onLoaded function
	ctx->base.fn_destroy = &pegpwn_destroy;
	
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
	
	// Set CPU port r/w functions (ahead of runpeg's)
	ctx->next_cookie = ear->port_cookie;
	ctx->next_read_fn = ear->read_fn;
	ctx->next_write_fn = ear->write_fn;
	EAR_setPorts(ctx->ear, &pegpwn_portRead, &pegpwn_portWrite, ctx);
	
	return &ctx->base;
}


int main(int argc, char** argv, char** envp) {
	(void)argc;
	
	// This challenge simply runs the PEGASUS program and serves its port 0 I/O over the
	// connected TCP socket. Easiest way to do that is to invoke runpeg with this module
	// as a plugin.
	char* runpeg_argv[] = {
		"./runpeg", "--plugin", argv[0], PEG_PWN_INPUT_FILE, NULL
	};
	if(execve("./runpeg", runpeg_argv, envp) < 0) {
		fprintf(stderr, "Failed to execute runpeg: %s\n", strerror(errno));
	}
	
	return EXIT_FAILURE;
}
