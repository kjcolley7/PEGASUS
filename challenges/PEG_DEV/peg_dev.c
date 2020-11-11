//
//  peg_dev.c
//  PegasusEar
//
//  Created by Kevin Colley on 11/1/2020.
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

//! Max number of bytes in the port buffer
#define PEG_MAX_PORT_DATA 100

//! Number of random tests that must be passed to solve this challenge
#define PEG_NUM_TESTS 100

//! Symbol name for the challenge function
#define PEG_SYM_UADD32 "uadd32_write"

//! Symbol name for the win function
#define PEG_SYM_WIN "win"


typedef struct PegDevPlugin {
	PegPlugin base;
	EAR* ear;
	PegasusLoader* pegload;
	dynamic_string port_buffer;
} PegDevPlugin;


static bool pegdev_portWrite(void* portWrite_cookie, uint8_t port_number, EAR_Byte byte) {
	PegDevPlugin* ctx = portWrite_cookie;
	
	// fprintf(stderr, "WRB (%hhu), '%c'\n", port_number, byte);
	
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
			if(ctx->port_buffer.count >= PEG_MAX_PORT_DATA) {
				return false;
			}
			
			string_appendChar(&ctx->port_buffer, byte);
			break;
		
		default:
			return false;
	}
	
	return true;
}


static void pegdev_destroy(PegPlugin* plugin) {
	PegDevPlugin* ctx = (PegDevPlugin*)plugin;
	
	string_clear(&ctx->port_buffer);
	free(ctx);
}

static bool pegdev_onLoaded(PegPlugin* plugin) {
	PegDevPlugin* ctx = (PegDevPlugin*)plugin;
	
	bool success = false;
	EAR_HaltReason r = HALT_NONE;
	int rand_fd = open("/dev/urandom", O_RDONLY);
	if(rand_fd < 0) {
		fail();
	}
	
	// Set CPU port r/w function
	EAR_setPorts(ctx->ear, NULL, &pegdev_portWrite, ctx);
	
	EAR_Size uadd32_addr = EAR_NULL;
	if(!PegasusLoader_dlsym(ctx->pegload, PEG_SYM_UADD32, &uadd32_addr)) {
		fprintf(stderr, "PEGASUS file missing symbol \"%s\"\n", PEG_SYM_UADD32);
		goto cleanup;
	}
	
	unsigned i;
	for(i = 0; i < PEG_NUM_TESTS; i++) {
		static const uint32_t fixed_tests[][2] = {
			{0x00000002, 0x00000002},
			{0x00000004, 0xFFFFFFFF},
			{0x11223344, 0x10101010},
			{0x00223344, 0x00101010},
			{0x11111111, 0xEEEEEEEF},
			{0x00000000, 0x00000000},
		};
		uint32_t rand_values[2];
		const uint32_t* values = NULL;
		
		if(i < ARRAY_COUNT(fixed_tests)) {
			values = fixed_tests[i];
		}
		else {
			if(read(rand_fd, rand_values, sizeof(rand_values)) != sizeof(rand_values)) {
				fail();
			}
			values = rand_values;
		}
		
		uint32_t sum = values[0] + values[1];
		char expected[PEG_MAX_PORT_DATA];
		snprintf(expected, sizeof(expected), "0x%08X\n", sum);
		
		fprintf(
			stderr,
			"TEST %u/%u: 0x%" PRIx32 " + 0x%" PRIx32 " = 0x%" PRIx32 "\n",
			i + 1,
			PEG_NUM_TESTS,
			values[0],
			values[1],
			sum
		);
		
		// Clear the port buffer
		ctx->port_buffer.count = 0;
		
		EAR_Size x_lo = values[0] & 0xFFFF;
		EAR_Size x_hi = values[0] >> 16;
		EAR_Size y_lo = values[1] & 0xFFFF;
		EAR_Size y_hi = values[1] >> 16;
		EAR_resetRegisters(ctx->ear);
		r = EAR_invokeFunction(ctx->ear, uadd32_addr, 0, x_lo, x_hi, y_lo, y_hi, 0, 0, true);
		if(r != HALT_RETURN) {
			fprintf(stderr, "EAR exception raised: %s\n", EAR_haltReasonToString(r));
			goto cleanup;
		}
		
		char* actual = string_cstr(&ctx->port_buffer);
		if(strcmp(actual, expected) != 0) {
			fprintf(stderr, "Test failed!\n");
			fprintf(stderr, "Expected: %s", expected);
			fprintf(stderr, "Actual: %s\n", actual);
			goto cleanup;
		}
	}
	
	printf("All tests passed! Calling win(flag)...\n");
	
	EAR_Size win_addr;
	if(!PegasusLoader_dlsym(ctx->pegload, PEG_SYM_WIN, &win_addr)) {
		fprintf(stderr, "PEGASUS file missing symbol \"%s\"\n", PEG_SYM_WIN);
	}
	
	char flag_data[256];
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
	
	// Encode flag as an lestring
	char* p = flag_data;
	if(*p == '\0') {
		fprintf(stderr, "Flag file empty! Expected flag.txt in the current directory.\n");
		fail();
	}
	while(p[1] != '\0') {
		*p++ |= 0x80;
	}
	
	EAR_PageNumber flag_ppn;
	if(!EAR_allocPhys(ctx->ear, 1, &flag_ppn)) {
		fprintf(stderr, "Unable to allocate a physical memory page for the flag. How is this possible?\n");
		goto cleanup;
	}
	
	EAR_copyinPhys(ctx->ear, &flag_ppn, 1, 0, flag_data, strlen(flag_data) + 1);
	EAR_Size flag_addr = EAR_addSegment(ctx->ear, EAR_NULL, EAR_PAGE_SIZE, &flag_ppn, EAR_PROT_READ, EAR_NULL);
	if(flag_addr == EAR_NULL) {
		fprintf(stderr, "Unable to map virtual page for flag contents. Did you really map every page table entry? You absolute madlad.\n");
		goto cleanup;
	}
	
	r = EAR_invokeFunction(ctx->ear, win_addr, 0, flag_addr, 0, 0, 0, 0, 0, true);
	if(EAR_FAILED(r)) {
		fprintf(stderr, "EAR exception raised: %s\n", EAR_haltReasonToString(r));
		goto cleanup;
	}
	
	printf("Done calling win(flag), hope you got it ;)\n");
	success = true;
	
cleanup:
	close(rand_fd);
	return success;
}

PegPlugin* PegPlugin_Init(EAR* ear, PegasusLoader* pegload) {
	(void)pegload;
	
	PegDevPlugin* ctx = calloc(1, sizeof(*ctx));
	if(!ctx) {
		return NULL;
	}
	
	ctx->ear = ear;
	ctx->pegload = pegload;
	
	ctx->base.fn_destroy = &pegdev_destroy;
	ctx->base.fn_onLoaded = &pegdev_onLoaded;
	
	return &ctx->base;
}

int main(void) {
	if(!PegasusServer_dlopenAndServeWithPlugin(&PegPlugin_Init)) {
		return EXIT_FAILURE;
	}
	
	return EXIT_SUCCESS;
}
