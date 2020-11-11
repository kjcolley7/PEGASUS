//
//  pegasus_server.c
//  PegasusEar
//
//  Created by Kevin Colley on 11/5/2020.
//

#include "pegasus_server.h"
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <inttypes.h>
#include "common/macros.h"
#include "common/dynamic_string.h"
#include "pegasus_ear/ear.h"
#include "pegasus_ear/loader.h"

/*!
 * Maximum allowed size of a PEG file is 70 KiB.
 * It doesn't make sense to allow PEG files much larger than the EAR address space size (64 KiB).
 */
#define PEG_SIZE_MAX (0x1000 * 70)

//! Allow one attempt per N seconds
#define PEG_ATTEMPT_DELAY 5


typedef struct PegSrvCookie {
	EAR* ear;
	PegasusLoader* pegload;
} PegSrvCookie;


static bool receive_peg(int sock, void** out_peg_data, uint32_t* out_peg_size) {
	char client_dir[100];
	char peg_path[100];
	uint32_t peg_size;
	void* peg_data;
	struct sockaddr_in cli_addr;
	socklen_t cli_len;
	uint32_t ip;
	int err;
	time_t curtime, floortime;
	int peg_fd;
	ssize_t bytes_written;
	
	printf("PEG SIZE?\n");
	peg_size = 0;
	errno = 0;
	if(recv(sock, &peg_size, sizeof(peg_size), 0) != sizeof(peg_size)) {
		printf("Failed to receive PEG SIZE: %s\n", strerror(errno));
		return false;
	}
	
	peg_size = ntohl(peg_size);
	if(peg_size > PEG_SIZE_MAX) {
		printf("PEG SIZE exceeds max! (%u > %u)\n", peg_size, PEG_SIZE_MAX);
		return false;
	}
	
	peg_data = malloc(peg_size);
	if(peg_data == NULL) {
		printf("Out of memory\n");
		return false;
	}
	
	printf("PEG DATA?\n");
	errno = 0;
	if(recv(sock, peg_data, peg_size, MSG_WAITALL) != peg_size) {
		printf("Failed to receive PEG DATA: %s\n", strerror(errno));
		return false;
	}
	
	// Get client's IP address
	cli_len = sizeof(cli_addr);
	if(getpeername(sock, (struct sockaddr*)&cli_addr, &cli_len) != 0) {
		fail();
	}
	ip = ntohl(cli_addr.sin_addr.s_addr);
	
	snprintf(
		client_dir,
		sizeof(client_dir),
		"/peg/%u.%u.%u.%u",
		ip >> 24,
		(ip >> 16) & 255,
		(ip >> 8) & 255,
		ip & 255
	);
	
	err = mkdir(client_dir, 0751);
	if(err != 0 && errno != EEXIST) {
		fail();
	}
	
	// Create timestamp string (rounded down to the nearest 5 seconds to ratelimit attempts)
	curtime = time(NULL);
	floortime = curtime / PEG_ATTEMPT_DELAY * PEG_ATTEMPT_DELAY;
	
	// Create path to where the PEGASUS file will be written
	snprintf(peg_path, sizeof(peg_path), "%s/%ld.peg", client_dir, floortime);
	peg_fd = open(peg_path, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if(peg_fd == -1) {
		printf(
			"PEG/1.1 429 Too Many Requests\n"
			"Retry-After: %ld\n",
			floortime + PEG_ATTEMPT_DELAY - curtime + 1
		);
		return false;
	}
	
	bytes_written = write(peg_fd, peg_data, peg_size);
	if(bytes_written != peg_size) {
		printf("Error writing PEG DATA\n");
		return false;
	}
	
	close(peg_fd);
	
	*out_peg_data = peg_data;
	*out_peg_size = peg_size;
	return true;
}


/*!
 * @brief Callback function pointer used to resolve missing imported symbols
 * 
 * @param resolveSymbol_cookie Opaque value passed directly to the callback
 * @param symbol_name Name of the symbol to be resolved
 * @param out_value Output pointer where the resolved value should be written
 * 
 * @return True if the symbol was resolved successfully, or false otherwise.
 */
static bool pegsrv_resolveSymbol(
	void* resolveSymbol_cookie,
	const char* symbol_name,
	uint16_t* out_value
) {
	(void)resolveSymbol_cookie;
	(void)out_value;
	
	printf("Failed to resolve imported symbol '%s'\n", symbol_name);
	return false;
}


/*!
 * @brief Callback function pointer used to map segments from a PEGASUS file into the target's memory
 * 
 * @param mapSegment_cookie Opaque value passed directly to the callback
 * @param vppn Virtual page number where the segment expects to be loaded
 * @param vpage_count Number of pages requested in the virtual mapping for this segment
 * @param segmentData Pointer to the segment's data bytes
 * @param segmentSize Number of bytes in `segmentData` to be mapped
 * @param prot Memory protections that should be applied to the virtual mapping
 * 
 * @return True if the segment was mapped successfully, or false otherwise.
 */
static bool pegsrv_mapSegment(
	void* mapSegment_cookie,
	uint8_t vppn,
	uint8_t vpage_count,
	const void* segmentData,
	uint16_t segmentSize,
	uint8_t prot
) {
	EAR* ear = mapSegment_cookie;
	
	EAR_PageNumber segment_ppns[256];
	EAR_PageNumber* phys_pages = NULL;
	
	// Allocate physical pages for this segment's vmsize
	if(prot != EAR_PROT_NONE) {
		if(EAR_allocPhys(ear, vpage_count, segment_ppns) != vpage_count) {
			fprintf(stderr, "Unable to map segment because physical memory is full\n");
			return false;
		}
		if(EAR_copyinPhys(ear, segment_ppns, vpage_count, 0, segmentData, segmentSize) != segmentSize) {
			fprintf(stderr, "Segment data is larger than the VM region\n");
			return false;
		}
		phys_pages = segment_ppns;
	}
	
	// Set up virtual page mappings
	EAR_addSegment(ear, vppn * EAR_PAGE_SIZE, vpage_count * EAR_PAGE_SIZE, phys_pages, prot, EAR_NULL);
	return true;
}


/*!
 * @brief Callback function pointer used to handle entrypoint commands while loading a PEGASUS file
 * 
 * @param handleEntry_cookie Opaque value passed directly to the callback
 * @param pc Initial PC value which points the code that should be executed
 * @param dpc Initial DPC value to be used while invoking the entrypoint
 * @param arg1 First argument to the entrypoint function
 * @param arg2 Second argument to the entrypoint function
 * @param arg3 Third argument to the entrypoint function
 * @param arg4 Fourth argument to the entrypoint function
 * @param arg5 Fifth argument to the entrypoint function
 * @param arg6 Sixth argument to the entrypoint function
 * 
 * @return True if the entrypoint function completed successfully, or false otherwise.
 */
static bool pegsrv_handleEntry(
	void* handleEntry_cookie,
	uint16_t pc,
	uint16_t dpc,
	uint16_t arg1,
	uint16_t arg2,
	uint16_t arg3,
	uint16_t arg4,
	uint16_t arg5,
	uint16_t arg6
) {
	EAR* ear = handleEntry_cookie;
	
	EAR_resetRegisters(ear);
	EAR_HaltReason r = EAR_invokeFunction(ear, pc, dpc, arg1, arg2, arg3, arg4, arg5, arg6, true);
	if(EAR_FAILED(r)) {
		printf("EAR core halted: %s\n", EAR_haltReasonToString(r));
		return false;
	}
	
	return true;
}


/*!
 * @brief Receive a PEGASUS file over a socket, then parse and add it to the loader.
 * 
 * @param pegload PEGASUS file loader to initialize and add the PEGASUS file to
 * @param sock A client should be connected to this socket and will send a PEGASUS file over it
 * 
 * @return True on success, or false otherwise
 */
static bool PegasusServer_parseFromSocket(PegasusLoader* pegload, int sock) {
	void* peg_data = NULL;
	uint32_t peg_size = 0;
	if(!receive_peg(sock, &peg_data, &peg_size)) {
		return false;
	}
	
	PegStatus s = PEG_SUCCESS;
	
	// Parse PEGASUS file from memory
	Pegasus* peg = Pegasus_new();
	if(!peg) {
		fprintf(stderr, "Out of memory\n");
		return false;
	}
	
	// Parse the PEGASUS file
	s = Pegasus_parseFromMemory(peg, peg_data, peg_size);
	if(s != PEG_SUCCESS) {
		fprintf(stderr, "%s\n", PegStatus_toString(s));
		return false;
	}
	
	// Add the parsed PEGASUS file to the loader
	PegasusLoader_add(pegload, peg);
	return true;
}

/*!
 * @brief Run the PEGASUS server using the provided plugin.
 * 
 * @param plugin_init Function pointer to the plugin initialization function
 * 
 * @return True on success, or false on failure
 */
bool PegasusServer_serveWithPlugin(PegPlugin_Init_func* plugin_init) {
	static EAR ear;
	static PegasusLoader pegload;
	
	bool success = false;
	PegPlugin* plugin = NULL;
	
	// Init CPU
	EAR_init(&ear, 0);
	
	// Init loader
	PegasusLoader_init(&pegload);
	
	// Init plugin
	plugin = plugin_init(&ear, &pegload);
	if(plugin == NULL) {
		goto cleanup;
	}
	
	// Init server, receive PEGASUS file over connected socket, and parse it
	if(!PegasusServer_parseFromSocket(&pegload, STDIN_FILENO)) {
		goto cleanup;
	}
	
	// Configure loader callbacks
	if(!PegasusLoader_hasSymbolResolver(&pegload)) {
		PegasusLoader_setSymbolResolver(&pegload, &pegsrv_resolveSymbol, &ear);
	}
	
	if(!PegasusLoader_hasSegmentMapper(&pegload)) {
		PegasusLoader_setSegmentMapper(&pegload, &pegsrv_mapSegment, &ear);
	}
	
	if(!PegasusLoader_hasEntrypointHandler(&pegload)) {
		PegasusLoader_setEntrypointHandler(&pegload, &pegsrv_handleEntry, &ear);
	}
	
	// Resolve imported symbols, apply relocations, map segments, and invoke entrypoints
	PegStatus s = PegasusLoader_resolveAndLoad(&pegload);
	if(s != PEG_SUCCESS) {
		fprintf(stderr, "%s\n", PegStatus_toString(s));
		goto cleanup;
	}
	
	// Invoke onLoaded callback if defined
	if(plugin->fn_onLoaded != NULL) {
		if(!plugin->fn_onLoaded(plugin)) {
			goto cleanup;
		}
	}
	
	success = true;
	
cleanup:
	if(plugin->fn_destroy != NULL) {
		plugin->fn_destroy(plugin);
	}
	
	PegasusLoader_destroy(&pegload);
	return success;
}
