//
//  runpeg.c
//  PegasusEar
//
//  Created by Kevin Colley on 3/6/2020.
//

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include "common/dynamic_string.h"
#include "pegasus_ear/ear.h"
#include "pegasus_ear/loader.h"
#include "pegasus_ear/debugger.h"
#include "pegasus_ear/plugin.h"
#include "kjc_argparse/kjc_argparse.h"


typedef struct RunPegCookie {
	EAR* ear;
	Debugger* dbg;
} RunPegCookie;


static bool earcb_portRead(void* portRead_cookie, uint8_t port_number, EAR_Byte* out_byte) {
	RunPegCookie* cookie = portRead_cookie;
	EAR_Byte byte;
	
	if(port_number == 0) {
		// Read byte from stdin
		if(read(STDIN_FILENO, &byte, 1) != 1) {
			if(!g_interrupted) {
				perror("read");
			}
			return false;
		}
	}
	
	if(cookie->ear->debug_flags & DEBUG_VERBOSE) {
		fprintf(stderr, "RDB (%hhu) //0x%02X '%c'\n", port_number, byte, byte);
	}
	
	*out_byte = byte;
	return true;
}


static bool earcb_portWrite(void* portWrite_cookie, uint8_t port_number, EAR_Byte byte) {
	RunPegCookie* cookie = portWrite_cookie;
	
	if(port_number == 0) {
		// Write byte to stdout
		if(write(STDOUT_FILENO, &byte, 1) != 1) {
			if(!g_interrupted) {
				perror("write");
			}
			return false;
		}
	}
	
	if(cookie->ear->debug_flags & DEBUG_VERBOSE) {
		fprintf(stderr, "WRB (%hhu) //0x%02X '%c'\n", port_number, byte, byte);
	}
	
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
static bool pegcb_resolveSymbol(
	void* resolveSymbol_cookie,
	const char* symbol_name,
	uint16_t* out_value
) {
	(void)resolveSymbol_cookie;
	(void)out_value;
	
	fprintf(stderr, "Failed to resolve imported symbol '%s'\n", symbol_name);
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
static bool pegcb_mapSegment(
	void* mapSegment_cookie,
	uint8_t vppn,
	uint8_t vpage_count,
	const void* segmentData,
	uint16_t segmentSize,
	uint8_t prot
) {
	RunPegCookie* cookie = mapSegment_cookie;
	
	EAR_PageNumber segment_ppns[256];
	EAR_PageNumber* phys_pages = NULL;
	
	// Allocate physical pages for this segment's vmsize
	if(prot != EAR_PROT_NONE) {
		if(EAR_allocPhys(cookie->ear, vpage_count, segment_ppns) != vpage_count) {
			fprintf(stderr, "Unable to map segment because physical memory is full\n");
			return false;
		}
		EAR_Size bytes_copied = EAR_copyinPhys(cookie->ear, segment_ppns, vpage_count, 0, segmentData, segmentSize);
		if(bytes_copied != segmentSize) {
			fprintf(stderr, "Segment data is larger than the VM region\n");
			fprintf(stderr, "vpc: 0x%x, segmentSize: 0x%x, bytes_copied: 0x%x\n", vpage_count, segmentSize, bytes_copied);
			return false;
		}
		phys_pages = segment_ppns;
	}
	
	// Set up virtual page mappings
	EAR_addSegment(cookie->ear, vppn * EAR_PAGE_SIZE, vpage_count * EAR_PAGE_SIZE, phys_pages, prot, EAR_NULL);
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
static bool pegcb_handleEntry(
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
	RunPegCookie* cookie = handleEntry_cookie;
	
	EAR_resetRegisters(cookie->ear);
	EAR_HaltReason r = EAR_invokeFunction(cookie->ear, pc, dpc, arg1, arg2, arg3, arg4, arg5, arg6, cookie->dbg == NULL);
	if(cookie->dbg != NULL) {
		r = Debugger_run(cookie->dbg);
	}
	if(EAR_FAILED(r)) {
		fprintf(stderr, "EAR core halted with exception %d: %s\n", r, EAR_haltReasonToString(r));
		return false;
	}
	
	return true;
}


int main(int argc, char** argv) {
	static EAR ear;
	static PegasusLoader pegload;
	static RunPegCookie cookie;
	PegStatus s = PEG_SUCCESS;
	int ret = EXIT_FAILURE;
	EAR_DebugFlags debugFlags = 0;
	bool flagDebug = false;
	const char* funcName = NULL;
	dynamic_array(const char*) inputFiles = {0};
	const char* pluginPath = NULL;
	void* pluginHandle = NULL;
	PegPlugin_Init_func* plugin_init = NULL;
	PegPlugin* plugin = NULL;
	
	ARGPARSE(argc, argv) {
		ARG('h', "help", "Show this help message") {
			ARGPARSE_HELP();
			return 0;
		}
		
		ARG_STRING('p', "plugin", "Path to a plugin library to load as a checker module", arg) {
			pluginPath = arg;
		}
		
		ARG_STRING('f', "function", "Resolve the named symbol and call it as a function", arg) {
			funcName = arg;
		}
		
		ARG('d', "debug", "Enable the EAR debugger") {
			flagDebug = true;
		}
		
		ARG('t', "trace", "Print every instruction as it runs") {
			debugFlags |= DEBUG_TRACE;
		}
		
		ARG('v', "verbose", "Enable verbose mode for EAR emulator") {
			debugFlags |= DEBUG_VERBOSE;
		}
		
		ARG_POSITIONAL("input1.peg {inputN.peg...}", arg) {
			array_append(&inputFiles, arg);
		}
		
		ARG_END() {
			if(inputFiles.count == 0) {
				fprintf(stderr, "Error: No PEG files to run\n");
				ARGPARSE_HELP();
				return EXIT_FAILURE;
			}
		}
	}
	
	// Load a plugin shared library if the --challenge argument was passed
	if(pluginPath != NULL) {
		// The dlopen function only searches paths if the string contains a slash,
		// so add a "./" prefix when the argument doesn't have a slash.
		if(strchr(pluginPath, '/') == NULL) {
			dynamic_string tmp = {0};
			string_append(&tmp, "./");
			string_append(&tmp, pluginPath);
			pluginHandle = dlopen(string_cstr(&tmp), RTLD_LAZY);
			string_clear(&tmp);
		}
		else {
			pluginHandle = dlopen(pluginPath, RTLD_LAZY);
		}
		
		if(pluginHandle == NULL) {
			fprintf(stderr, "Failed to load plugin %s: %s\n", pluginPath, dlerror());
			goto cleanup;
		}
		
		// The init symbol is required
		plugin_init = dlsym(pluginHandle, PEG_PLUGIN_INIT_SYMBOL);
		if(plugin_init == NULL) {
			fprintf(stderr, "Challenge plugin %s is missing required symbol %s!\n", pluginPath, PEG_PLUGIN_INIT_SYMBOL);
			goto cleanup;
		}
	}
	
	// Init CPU
	EAR_init(&ear, debugFlags);
	cookie.ear = &ear;
	
	// Init debugger
	if(flagDebug) {
		cookie.dbg = Debugger_init(&ear);
	}
	
	// Set CPU port r/w function
	EAR_setPorts(&ear, &earcb_portRead, &earcb_portWrite, &cookie);
	
	// Init loader
	PegasusLoader_init(&pegload);
	PegasusLoader_setSymbolResolver(&pegload, &pegcb_resolveSymbol, &cookie);
	PegasusLoader_setSegmentMapper(&pegload, &pegcb_mapSegment, &cookie);
	PegasusLoader_setEntrypointHandler(&pegload, &pegcb_handleEntry, &cookie);
	
	// Initialize checker plugin (if passed as an argument)
	if(plugin_init != NULL) {
		plugin = plugin_init(&ear, &pegload);
		if(plugin == NULL) {
			fprintf(stderr, "Initializing plugin %s failed.\n", pluginPath);
			goto cleanup;
		}
	}
	
	// Parse and collect PEGASUS files from command line arguments
	foreach(&inputFiles, pFile) {
		Pegasus* peg = Pegasus_new();
		if(!peg) {
			fprintf(stderr, "Out of memory\n");
			goto cleanup;
		}
		
		// Parse the PEGASUS file
		s = Pegasus_parseFromFile(peg, *pFile);
		if(s != PEG_SUCCESS) {
			fprintf(stderr, "%s: %s\n", *pFile, PegStatus_toString(s));
			goto cleanup;
		}
		
		// Add the parsed PEGASUS file to the loader
		PegasusLoader_add(&pegload, peg);
	}
	
	// Resolve imported symbols, apply relocations, map segments, and invoke entrypoints
	s = PegasusLoader_resolveAndLoad(&pegload);
	if(s != PEG_SUCCESS) {
		fprintf(stderr, "%s\n", PegStatus_toString(s));
		goto cleanup;
	}
	
	// Call the plugin's onLoaded callback function (if present)
	if(plugin != NULL && plugin->fn_onLoaded != NULL) {
		if(!plugin->fn_onLoaded(plugin)) {
			goto cleanup;
		}
	}
	
	// If the --function argument was passed, resolve that symbol and call it w/ zero for all args
	if(funcName != NULL) {
		EAR_Size funcAddr = EAR_NULL;
		if(!PegasusLoader_dlsym(&pegload, funcName, &funcAddr)) {
			fprintf(stderr, "PEGASUS file missing symbol \"%s\"\n", funcName);
			goto cleanup;
		}
		
		fprintf(stderr, "Calling function %s at 0x%04X...\n", funcName, funcAddr);
		if(!pegcb_handleEntry(&cookie, funcAddr, 0, 0, 0, 0, 0, 0, 0)) {
			goto cleanup;
		}
	}
	
	// Completed successfully
	ret = EXIT_SUCCESS;
	
	
cleanup:
	if(plugin != NULL && plugin->fn_destroy != NULL) {
		plugin->fn_destroy(plugin);
	}
	
	PegasusLoader_destroy(&pegload);
	
	if(pluginHandle != NULL) {
		dlclose(pluginHandle);
	}
	
	return ret;
}
