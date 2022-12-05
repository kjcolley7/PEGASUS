//
//  peg_autorev.c
//  PegasusEar
//
//  Created by Kevin Colley on 11/17/2022.
//

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common/macros.h"
#include "pegasus_ear/plugin.h"


typedef struct PegAutorevPlugin {
	PegPlugin base;
	EAR_PortRead* next_read_fn;
	EAR_PortWrite* next_write_fn;
	void* next_cookie;
	EAR* ear;
} PegAutorevPlugin;


static bool cb_portWrite(void* portWrite_cookie, uint8_t port_number, EAR_Byte byte) {
	PegAutorevPlugin* ctx = portWrite_cookie;
	
	// Only intercept writes to port 0xE (for Exit)
	if(port_number != 0xE) {
		if(ctx->next_write_fn != NULL) {
			return ctx->next_write_fn(ctx->next_cookie, port_number, byte);
		}
		return false;
	}
	
	if(ctx->ear->debug_flags & DEBUG_VERBOSE) {
		fprintf(stderr, "WRB (%hhu), 0x%02X\n", port_number, byte);
	}
	
	exit(byte);
}

static bool cb_portRead(void* portRead_cookie, uint8_t port_number, EAR_Byte* out_byte) {
	PegAutorevPlugin* ctx = portRead_cookie;
	
	// Don't intercept anything, just proxy the call along
	if(ctx->next_read_fn != NULL) {
		return ctx->next_read_fn(ctx->next_cookie, port_number, out_byte);
	}
	return false;
}


static void cb_destroy(PegPlugin* plugin) {
	PegAutorevPlugin* ctx = (PegAutorevPlugin*)plugin;
	free(ctx);
}


PegPlugin* PegPlugin_Init(EAR* ear, PegasusLoader* pegload, int var_count, PegVar* vars) {
	(void)pegload;
	(void)var_count;
	(void)vars;
	
	PegAutorevPlugin* ctx = calloc(1, sizeof(*ctx));
	if(!ctx) {
		return NULL;
	}
	
	ctx->ear = ear;
	
	// We have a destructor but not an onLoaded function
	ctx->base.fn_destroy = &cb_destroy;
	
	// Set CPU port r/w functions (ahead of runpeg's)
	ctx->next_cookie = ear->port_cookie;
	ctx->next_read_fn = ear->read_fn;
	ctx->next_write_fn = ear->write_fn;
	EAR_setPorts(ctx->ear, &cb_portRead, &cb_portWrite, ctx);
	
	return &ctx->base;
}


int main(int argc, char** argv, char** envp) {
	if(argc < 2) {
		fprintf(stderr, "Usage: %s program.peg\n", argv[0]);
		return EXIT_FAILURE;
	}
	
	char* runpeg_argv[] = {
		"./runpeg",
		"--plugin", argv[0],
		/* pegasus file */ argv[1],
		NULL
	};
	if(execve("./runpeg", runpeg_argv, envp) < 0) {
		fprintf(stderr, "Failed to execute runpeg: %s\n", strerror(errno));
	}
	
	return EXIT_FAILURE;
}
