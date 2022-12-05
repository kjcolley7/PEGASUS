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
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <signal.h>
#include "common/dynamic_string.h"
#include "pegasus_ear/ear.h"
#include "pegasus_ear/loader.h"
#include "pegasus_ear/debugger.h"
#include "pegasus_ear/plugin.h"
#include "kjc_argparse/kjc_argparse.h"


// Struct used as the "cookie" object in various callbacks
typedef struct RunPegCookie {
	// CPU core interpreter
	EAR* ear;
	
	// Debugger object with all info
	Debugger* dbg;
	
	// File descriptor read from when RDB is executed on port 0
	int in_fd;
	
	// File descriptor written to when WRB is executed on port 0
	int out_fd;
} RunPegCookie;


// Called during execution of the `RDB` instruction
static bool earcb_portRead(void* portRead_cookie, uint8_t port_number, EAR_Byte* out_byte) {
	RunPegCookie* cookie = portRead_cookie;
	EAR_Byte byte;
	
	if(port_number != 0) {
		return false;
	}
	
	// Read byte from stdin
	if(read(cookie->in_fd, &byte, 1) != 1) {
		if(!g_interrupted) {
			perror("read");
		}
		return false;
	}
	
	if(cookie->ear->debug_flags & DEBUG_VERBOSE) {
		char ch[5] = {0};
		const char* ch_str = NULL;
		switch(byte) {
			case '\'':
				ch_str = "\\'";
				break;
			
			case '\r':
				ch_str = "\\r";
				break;
			
			case '\n':
				ch_str = "\\n";
				break;
			
			case '\0':
				ch_str = "\\0";
				break;
			
			case '\t':
				ch_str = "\\t";
				break;
			
			default:
				if(0x20 <= byte && byte <= 0x7E) {
					ch[0] = byte;
				}
				else {
					snprintf(ch, sizeof(ch), "\\x%02X", byte);
				}
				ch_str = ch;
				break;
		}
		fprintf(stderr, "RDB '%s'\n", ch_str);
	}
	
	*out_byte = byte;
	return true;
}


// Called during execution of the `WRB` instruction
static bool earcb_portWrite(void* portWrite_cookie, uint8_t port_number, EAR_Byte byte) {
	RunPegCookie* cookie = portWrite_cookie;
	
	if(cookie->ear->debug_flags & DEBUG_VERBOSE) {
		char ch[5] = {0};
		const char* ch_str = NULL;
		switch(byte) {
			case '\'':
				ch_str = "\\'";
				break;
			
			case '\r':
				ch_str = "\\r";
				break;
			
			case '\n':
				ch_str = "\\n";
				break;
			
			case '\0':
				ch_str = "\\0";
				break;
			
			case '\t':
				ch_str = "\\t";
				break;
			
			default:
				if(0x20 <= byte && byte <= 0x7E) {
					ch[0] = byte;
				}
				else {
					snprintf(ch, sizeof(ch), "\\x%02X", byte);
				}
				ch_str = ch;
				break;
		}
		fprintf(stderr, "WRB (%hhu), '%s'\n", port_number, ch_str);
	}
	
	if(port_number != 0) {
		return false;
	}
	
	// Write byte to stdout
	if(write(cookie->out_fd, &byte, 1) != 1) {
		if(!g_interrupted) {
			perror("write");
		}
		return false;
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
	
	// TODO: Implement this
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
	EAR_HaltReason r = EAR_invokeFunction(cookie->ear, pc, dpc, arg1, arg2, arg3, arg4, arg5, arg6, true);
	if(EAR_FAILED(r)) {
		fprintf(stderr, "EAR core halted with exception %d: %s\n", r, EAR_haltReasonToString(r));
		return false;
	}
	
	return true;
}


// When listening for a connection to a UNIX domain socket, be careful to ensure that
// the socket is always deleted even when this program is killed by alarm().
const char* g_unix_bind = NULL;
static void unlink_unix_socket(int signum) {
	if(g_unix_bind != NULL) {
		unlink(g_unix_bind);
		_exit(signum);
	}
}


static int listen_for_connection(const char* listen_address, bool io_quiet) {
	int err = -1;
	int conn = -1;
	int sock = -1;
	socklen_t socklen = 0;
	struct sockaddr* psa = NULL;
	bool did_unix_bind = false;
	unsigned i;
	static const int signals_to_catch[] = {
		SIGALRM,
		SIGINT,
		SIGSEGV,
		SIGABRT,
		SIGPIPE,
		SIGTERM,
		SIGHUP,
	};
	
	// There are two forms of socket listen addresses handled here.
	//
	// 1. <hostname>:<port>
	// 2. <path to UNIX domain socket>
	const char* port_str = strchr(listen_address, ':');
	if(port_str != NULL) {
		// This is form 1, meaning we will listen for incoming TCP connections on the given host and port.
		char* hostname;
		if(port_str == listen_address) {
			hostname = NULL;
		}
		else {
			hostname = strndup(listen_address, port_str - listen_address);
		}
		port_str++;
		
		struct addrinfo hints = {0};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE;
		
		// Do a domain name lookup on the provided hostname
		struct addrinfo* ais = NULL;
		int gai_err = getaddrinfo(hostname, port_str, &hints, &ais);
		destroy(&hostname);
		if(gai_err != 0) {
			fprintf(stderr, "Error: Couldn't resolve hostname \"%s\": %s\n", listen_address, gai_strerror(gai_err));
			goto out;
		}
		
		// Try listening to each returned address
		struct addrinfo* ai;
		int first_errno = 0;
		for(ai = ais; ai != NULL; ai = ai->ai_next) {
			errno = 0;
			sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
			if(sock == -1) {
				if(first_errno == 0) {
					first_errno = errno;
				}
				continue;
			}
			
			errno = 0;
			int err = bind(sock, ai->ai_addr, ai->ai_addrlen);
			if(err == 0) {
				socklen = ai->ai_addrlen;
				break;
			}
			
			if(first_errno == 0) {
				first_errno = errno;
			}
			
			close(sock);
			sock = -1;
		}
		
		// Done with name lookup
		freeaddrinfo(ais);
		
		if(ai == NULL) {
			fprintf(stderr, "Error: Unable to bind to an address for %s:%s", listen_address, port_str);
			if(first_errno != 0) {
				fprintf(stderr, ": %s\n", strerror(first_errno));
			}
			else {
				fprintf(stderr, "\n");
			}
			goto out;
		}
	}
	else {
		// This is form 2, meaning we will listen for incoming UNIX domain socket connections at the given path.
		errno = 0;
		sock = socket(AF_UNIX, SOCK_STREAM, 0);
		if(sock < 0) {
			fprintf(stderr, "Error: Couldn't create UNIX socket: %s\n", strerror(errno));
			goto out;
		}
		
		struct sockaddr_un sau = {0};
		sau.sun_family = AF_UNIX;
		
		// Make sure there's no truncation.
		if(strlen(listen_address) >= sizeof(sau.sun_path)) {
			fprintf(stderr, "Error: Listen address is too long (%s)\n", listen_address);
			goto out;
		}
		strncpy(sau.sun_path, listen_address, sizeof(sau.sun_path) - 1);
		
		// Enable signal handler to delete the UNIX domain socket in case of a fatal signal.
		g_unix_bind = listen_address;
		for(i = 0; i < ARRAY_COUNT(signals_to_catch); i++) {
			signal(signals_to_catch[i], &unlink_unix_socket);
		}
		
		// Binding to a UNIX socket will create the filesystem entry.
		errno = 0;
		err = bind(sock, (struct sockaddr*)&sau, sizeof(sau));
		if(err != 0) {
			fprintf(stderr, "Error: Couldn't bind to UNIX socket at %s: %s\n", listen_address, strerror(errno));
			goto out;
		}
		
		did_unix_bind = true;
		
		// Set permissions of UNIX socket (tried doing fchmod() before bind(), didn't work)
		errno = 0;
		err = chmod(listen_address, 0777);
		if(err < 0) {
			fprintf(stderr, "Error: Failed to change UNIX socket permissions: %s\n", strerror(errno));
			goto out;
		}
		
		socklen = sizeof(sau);
	}
	
	if(!io_quiet) {
		fprintf(stderr, "Listening for incoming connection on %s...\n", listen_address);
	}
	
	// We now have some bound socket (either TCP or UNIX domain) and need
	// to listen to it for a single incoming connection.
	errno = 0;
	if(listen(sock, 1) != 0) {
		fprintf(stderr, "Error: Unable to listen on I/O socket: %s\n", strerror(errno));
		goto out;
	}
	
	psa = malloc(socklen);
	if(!psa) {
		goto out;
	}
	
	// Accept the incoming connection, giving us the connection socket
	errno = 0;
	conn = accept(sock, psa, &socklen);
	if(conn < 0) {
		fprintf(stderr, "Error: Failed to accept incoming connection: %s\n", strerror(errno));
		goto out;
	}
	
out:
	destroy(&psa);
	
	// We've received the only connection we care about, and can now close the listening socket.
	if(sock != -1) {
		close(sock);
	}
	
	// When we listened on a UNIX socket, we can also now delete the socket from the filesystem safely.
	if(did_unix_bind) {
		unlink(listen_address);
		
		// Uninstall the signal handlers now that the UNIX socket has been deleted.
		g_unix_bind = NULL;
		for(i = 0; i < ARRAY_COUNT(signals_to_catch); i++) {
			signal(signals_to_catch[i], SIG_DFL);
		}
	}
	
	return conn;
}


typedef struct PluginInfo PluginInfo;
struct PluginInfo {
	// Filesystem path to a plugin module to load (plugin.so)
	const char* path;
	
	// Handle returned by dlsym() for the plugin module
	void* handle;
	
	// Function pointer for the plugin's initialization routine
	PegPlugin_Init_func* init;
	
	// Metadata and function pointers registered by the loaded plugin
	PegPlugin* obj;
	
	// True when the plugin has been initialized successfully
	bool initialized;
};

int main(int argc, char** argv) {
	static EAR ear;
	static PegasusLoader pegload;
	static RunPegCookie cookie;
	PegStatus s = PEG_SUCCESS;
	int ret = EXIT_FAILURE;
	EAR_DebugFlags debugFlags = 0;
	bool flagDebug = false;
	dynamic_array(const char*) functions = {0};
	dynamic_array(const char*) inputFiles = {0};
	dynamic_array(PluginInfo) plugins = {0};
	dynamic_array(PegVar) pluginArgs = {0};
	cookie.in_fd = STDIN_FILENO;
	cookie.out_fd = STDOUT_FILENO;
	const char* listen_address = NULL;
	bool io_quiet = false;
	
	ARGPARSE(argc, argv) {
		ARG('h', "help", "Show this help message") {
			ARGPARSE_HELP();
			return 0;
		}
		
		ARG_STRING('p', "plugin", "Path to a plugin library to load as a checker module", arg) {
			PluginInfo p = {0};
			p.path = arg;
			array_append(&plugins, p);
		}
		
		ARG_STRING('a', "plugin-arg", "Format like 'key=value', will be passed to all loaded checker modules", arg) {
			const char* equals_pos = strchr(arg, '=');
			if(equals_pos == NULL) {
				fprintf(stderr, "Error: Missing '=' character in --plugin-arg value\n");
				goto usage;
			}
			
			PegVar pv;
			pv.name = strndup(arg, equals_pos - arg);
			pv.value = strdup(equals_pos + 1);
			
			array_append(&pluginArgs, pv);
		}
		
		ARG_STRING('f', "function", "Resolve the named symbol and call it as a function", arg) {
			array_append(&functions, arg);
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
		
		ARG_INT(0, "input-fd", "Use a different file descriptor as port 0 input", num) {
			cookie.in_fd = num;
		}
		
		ARG_INT(0, "output-fd", "Use a different file descriptor as port 0 output", num) {
			cookie.out_fd = num;
		}
		
		// It might make more sense to have the debugger listen (like a debug server). For technical reasons,
		// we instead separate the program's interactive I/O to a socket and keep the debugger I/O on stdin/stdout.
		// This is because the debugger uses the linenoise library for nice terminal interaction like supporting
		// arrow keys, suggested completions, and autocomplete. This library doesn't support non-TTY inputs like
		// a socket.
		ARG_STRING('l', "io-listen", "Use the path to a UNIX domain socket for port 0 input/output", path) {
			// Idea: For challenges where the debugger should be exposed in addition to I/O, have a second
			// binary in the Docker container listening on the challenge port + 1. Upon receiving a connection,
			// it will ask for a debugger session ID (perhaps a GUID), and will use that to look up a path to the
			// UNIX domain socket to connect to. It will then proceed to proxy all data between the UNIX domain
			// socket and the TCP socket (maybe using socat for simplicity).
			//
			// Update: The above idea now exists, as the pegsession module.
			listen_address = path;
		}
		
		ARG(0, "io-quiet", "Don't print an info message when listening for an incoming connection") {
			io_quiet = true;
		}
		
		ARG_POSITIONAL("input1.peg {inputN.peg...}", arg) {
			array_append(&inputFiles, arg);
		}
		
		ARG_END() {
			if(inputFiles.count == 0) {
				fprintf(stderr, "Error: No PEG files to run\n");
				goto usage;
			}
			
			if(io_quiet && listen_address == NULL) {
				fprintf(stderr, "The --io-quiet argument is meaningless without --io-listen.\n");
				goto usage;
			}
			
			if(listen_address != NULL) {
				if(cookie.in_fd != STDIN_FILENO) {
					fprintf(stderr, "Error: Cannot specify both --input-fd and --io-listen!\n");
					goto usage;
				}
				if(cookie.out_fd != STDOUT_FILENO) {
					fprintf(stderr, "Error: Cannot specify both --output-fd and --io-listen!\n");
					goto usage;
				}
			}
			
			// All good!
			break;
			
		usage:
			ARGPARSE_HELP();
			exit(EXIT_FAILURE);
		}
	}
	
	// When --io-listen is passed, listen on the specified socket and use it for I/O operations on port 0.
	if(listen_address != NULL) {
		int conn = listen_for_connection(listen_address, io_quiet);
		if(conn < 0) {
			exit(EXIT_FAILURE);
		}
		cookie.in_fd = conn;
		cookie.out_fd = conn;
	}
	
	// Load plugin shared libraries if any were passed with the --plugin argument
	foreach(&plugins, plugin) {
		// The dlopen function only searches paths if the string contains a slash,
		// so add a "./" prefix when the argument doesn't have a slash.
		if(strchr(plugin->path, '/') == NULL) {
			dynamic_string tmp = {0};
			string_append(&tmp, "./");
			string_append(&tmp, plugin->path);
			plugin->handle = dlopen(string_cstr(&tmp), RTLD_LAZY);
			string_clear(&tmp);
		}
		else {
			plugin->handle = dlopen(plugin->path, RTLD_LAZY);
		}
		
		if(plugin->handle == NULL) {
			fprintf(stderr, "Failed to load plugin %s: %s\n", plugin->path, dlerror());
			goto cleanup;
		}
		
		// The init symbol is required
		plugin->init = dlsym(plugin->handle, PEG_PLUGIN_INIT_SYMBOL);
		if(plugin->init == NULL) {
			fprintf(stderr, "Challenge plugin %s is missing required symbol '%s'!\n", plugin->path, PEG_PLUGIN_INIT_SYMBOL);
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
	
	// Initialize checker plugin(s)
	foreach(&plugins, plugin) {
		plugin->obj = plugin->init(&ear, &pegload, (int)pluginArgs.count, pluginArgs.elems);
		if(plugin->obj == NULL) {
			fprintf(stderr, "Initializing plugin %s failed.\n", plugin->path);
			goto cleanup;
		}
		
		plugin->initialized = true;
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
	
	// Call the onLoaded callback function in all loaded plugins
	foreach(&plugins, plugin) {
		if(plugin->obj->fn_onLoaded != NULL) {
			if(!plugin->obj->fn_onLoaded(plugin->obj)) {
				goto cleanup;
			}
		}
	}
	
	// For each function that was passed with --function, resolve its symbol and call it w/ zero for all args
	foreach(&functions, pFuncName) {
		EAR_Size funcAddr = EAR_NULL;
		if(!PegasusLoader_dlsym(&pegload, *pFuncName, &funcAddr)) {
			fprintf(stderr, "Unable to resolve PEGASUS symbol \"%s\"\n", *pFuncName);
			goto cleanup;
		}
		
		fprintf(stderr, "Calling function \"%s\" at 0x%04X...\n", *pFuncName, funcAddr);
		if(!pegcb_handleEntry(&cookie, funcAddr, 0, 0, 0, 0, 0, 0, 0)) {
			goto cleanup;
		}
	}
	
	// Completed successfully
	ret = EXIT_SUCCESS;
	
	
cleanup:
	foreach(&plugins, plugin) {
		if(plugin->initialized) {
			plugin->obj->fn_destroy(plugin->obj);
		}
	}
	
	PegasusLoader_destroy(&pegload);
	
	foreach(&plugins, plugin) {
		if(plugin->handle != NULL) {
			dlclose(plugin->handle);
		}
	}
	
	if(listen_address != NULL) {
		close(cookie.in_fd);
		if(cookie.out_fd != cookie.in_fd) {
			close(cookie.out_fd);
		}
	}
	
	return ret;
}
