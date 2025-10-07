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
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <signal.h>
#include "common/dynamic_string.h"
#include "libear/ear.h"
#include "libear/bus.h"
#include "libear/mmu.h"
#include "libear/plugin.h"
#include "libeardbg/debugger.h"
#include "kjc_argparse/kjc_argparse.h"
#include "bootrom.h"


// Struct used as the "cookie" object in various callbacks
typedef struct RunPegCookie {
	// CPU core interpreter
	EAR* ear;
	
	// Debugger object with all info
	Debugger* dbg;
	
	// Function pointer for debugger to trace executed instructions
	EAR_ExecHook* dbg_trace;
	
	// File descriptor read from when RDB is executed on port 0
	int in_fd;
	
	// File descriptor written to when WRB is executed on port 0
	int out_fd;
	
	// File descriptor read from when RDB is executed on port 0xF
	int flag_fd;
	
	// True to show kernel debug UART output (port 0xD)
	bool show_debug_uart;
	
	// True if verbose output should be printed
	bool verbose;
	
	// True if a trace of executed instructions should be printed
	bool trace;
	
	// True if kernel-mode instructions should be traced
	bool kernel;
} RunPegCookie;


/*!
 * @brief Add this function as the CPU's exec hook to trace executed instructions.
 * 
 * @param insn Instruction about to be executed
 * @param pc Address of the next instruction to be executed (after this one)
 * @param before True if the hook is called before executing the instruction, false if after
 * @param cond True if the instruction's condition evaluated to true
 * 
 * @return HALT_NONE
 */
static EAR_HaltReason runpeg_trace(void* cookie, EAR_Instruction* insn, EAR_FullAddr pc, bool before, bool cond) {
	RunPegCookie* runpeg = cookie;
	EAR* ear = runpeg->ear;
	EAR_ThreadState* ctx = CTX(*ear);
	
	if(before && runpeg->trace && (runpeg->kernel || !Debugger_isKernelMode(ctx))) {
		// Print the instruction being executed
		EAR_VirtAddr curpc = ctx->cr[CR_INSN_ADDR];
		
		Pegasus* peg = runpeg->dbg->pegs[ear->ctx.active];
		if(peg) {
			Pegasus_Symbol* sym = Pegasus_findSymbolByAddress(peg, curpc);
			if(sym && sym->value == curpc) {
				fprintf(stderr, "  %s:\n", sym->name);
			}
		}
		
		fprintf(stderr, "\t%04X.%04X: %c ", curpc, ctx->r[DPC], cond ? ' ' : '#');
		Debugger_showInstruction(runpeg->dbg, insn, pc, stderr);
	}
	
	return runpeg->dbg_trace ? runpeg->dbg_trace(runpeg->dbg, insn, pc, before, cond) : HALT_NONE;
}


// Called during execution of the `RDB` instruction
static EAR_HaltReason runpeg_portRead(void* cookie, uint8_t port_number, EAR_Byte* out_byte) {
	RunPegCookie* runpeg = cookie;
	EAR_Byte byte;
	
	int fd = -1;
	if(port_number == 0) {
		fd = runpeg->in_fd;
	}
	else if(port_number == 0xF) {
		fd = runpeg->flag_fd;
	}
	
	if(fd < 0) {
		return HALT_BUS_FAULT;
	}
	
	// Read byte from stdin
	ssize_t bytes_read = read(fd, &byte, 1);
	if(bytes_read != 1) {
		if(g_interrupted) {
			return HALT_DEBUGGER;
		}
		
		if(bytes_read < 0) {
			perror("read");
		}
		return HALT_IO_ERROR;
	}
	
	if(runpeg->verbose) {
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
		fprintf(stderr, "RDB '%s', (%d)\n", ch_str, port_number);
	}
	
	*out_byte = byte;
	return HALT_NONE;
}


// Called during execution of the `WRB` instruction
static EAR_HaltReason runpeg_portWrite(void* cookie, uint8_t port_number, EAR_Byte byte) {
	RunPegCookie* runpeg = cookie;
	
	if(runpeg->verbose) {
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
	
	int fd = -1;
	switch(port_number) {
		case 0: //stdout
			fd = runpeg->out_fd;
			break;
		
		case 1: //stderr
			fd = STDERR_FILENO;
			break;
		
		case 0xD: //debug (UART)
			if(!runpeg->show_debug_uart) {
				return HALT_NONE;
			}
			fd = runpeg->out_fd;
			break;
		
		case 0xE: //exit
			exit(byte);
		
		default:
			return HALT_BUS_FAULT;
	}
	
	// Write byte to output
	if(write(fd, &byte, 1) != 1) {
		if(!g_interrupted) {
			perror("write");
			return HALT_BUS_FAULT;
		}
		return HALT_DEBUGGER;
	}
	
	return HALT_NONE;
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
	void* dlhandle;
	
	// Function pointer for the plugin's initialization routine
	PegPlugin_Init_func* init;
	
	// Metadata and function pointers registered by the loaded plugin
	PegPlugin* obj;
	
	// True when the plugin has been initialized successfully
	bool initialized;
};

int main(int argc, char** argv) {
	static EAR ear;
	static MMU mmu;
	static Bus bus;
	static RunPegCookie cookie;
	int ret = EXIT_FAILURE;
	int fd = -1;
	const char* bootromFile = NULL;
	void* rom_map = MAP_FAILED;
	off_t rom_size = 0;
	void* ram_map = MAP_FAILED;
	DebugFlags debugFlags = 0;
	bool flagDebug = false;
	bool debugNonInvasive = false;
	dynamic_array(const char*) functions = {0};
	dynamic_array(const char*) inputFiles = {0};
	dynamic_array(struct {
		void* map;
		size_t size;
	}) inputFileMaps = {0};
	Pegasus* bootpeg = NULL;
	Pegasus* userpeg = NULL;
	dynamic_array(PluginInfo) plugins = {0};
	dynamic_array(PegVar) pluginArgs = {0};
	cookie.in_fd = STDIN_FILENO;
	cookie.out_fd = STDOUT_FILENO;
	cookie.flag_fd = -1;
	const char* listen_address = NULL;
	bool io_quiet = false;
	EAR_HaltReason r = HALT_NONE;
	
	ARGPARSE(argc, argv) {
		ARG('h', "help", NULL) {
			ARGPARSE_HELP();
			return 0;
		}
		
		ARG_INT('t', "timeout", "Max number of seconds to run before exiting", seconds) {
			alarm(seconds);
		}
		
		ARG_STRING(0, "bootrom", "Path to the bootrom image to use (flat binary or PEGASUS file)", filepath) {
			bootromFile = filepath;
		}
		
		ARG_STRING(0, "plugin", "Path to a plugin library to load as a checker module", filepath) {
			PluginInfo p = {0};
			p.path = filepath;
			array_append(&plugins, p);
		}
		
		ARG_STRING(0, "plugin-arg", "Format like 'key=value', will be passed to all loaded checker modules", keyval) {
			const char* equals_pos = strchr(keyval, '=');
			if(equals_pos == NULL) {
				fprintf(stderr, "Error: Missing '=' character in --plugin-arg value\n");
				goto usage;
			}
			
			PegVar pv;
			pv.name = strndup(keyval, equals_pos - keyval);
			pv.value = strdup(equals_pos + 1);
			
			array_append(&pluginArgs, pv);
		}
		
		ARG_STRING(0, "function", "Resolve the named symbol and call it as a function", funcname) {
			array_append(&functions, funcname);
		}
		
		ARG('d', "debug", "Enable the EAR debugger") {
			flagDebug = true;
		}
		
		ARG(0, "debug-noninvasive", "Enable the EAR debugger in non-invasive mode") {
			flagDebug = true;
			debugNonInvasive = true;
		}
		
		ARG('k', "kernel-debug", "Enable kernel debugging") {
			flagDebug = true;
			debugFlags |= DEBUG_KERNEL;
			cookie.kernel = true;
			
			// Might as well automatically show kernel debug UART output when kernel debugging
			cookie.show_debug_uart = true;
		}
		
		ARG(0, "trace", "Print every instruction as it runs (only usermode)") {
			cookie.trace = true;
		}
		
		ARG(0, "kernel-trace", "Print every instruction as it runs (both usermode and kernelmode)") {
			cookie.trace = true;
			cookie.kernel = true;
		}
		
		ARG('u', "uart", "Show output written to port 0xD (kernel debug UART)") {
			cookie.show_debug_uart = true;
		}
		
		ARG('v', "verbose", "Enable verbose mode for EAR emulator") {
			cookie.verbose = true;
		}
		
		ARG_INT(0, "input-fd", "Use a different file descriptor as port 0 input", fd) {
			cookie.in_fd = fd;
		}
		
		ARG_INT(0, "output-fd", "Use a different file descriptor as port 0 output", fd) {
			cookie.out_fd = fd;
		}
		
		ARG_STRING(0, "flag-port-file", "Use the given file as the data to read from port 0xF", flag_file) {
			cookie.flag_fd = open(flag_file, O_RDONLY);
			if(cookie.flag_fd < 0) {
				perror(flag_file);
				goto usage;
			}
		}
		
		// It might make more sense to have the debugger listen (like a debug server). For technical reasons,
		// we instead separate the program's interactive I/O to a socket and keep the debugger I/O on stdin/stdout.
		// This is because the debugger uses the linenoise library for nice terminal interaction like supporting
		// arrow keys, suggested completions, and autocomplete. This library doesn't support non-TTY inputs like
		// a socket.
		ARG_STRING('l', "io-listen", "Use the path to a UNIX domain socket for port 0 input/output", sockpath) {
			// Idea: For challenges where the debugger should be exposed in addition to I/O, have a second
			// binary in the Docker container listening on the challenge port + 1. Upon receiving a connection,
			// it will ask for a debugger session ID (perhaps a GUID), and will use that to look up a path to the
			// UNIX domain socket to connect to. It will then proceed to proxy all data between the UNIX domain
			// socket and the TCP socket (maybe using socat for simplicity).
			//
			// Update: The above idea now exists, as the pegsession module.
			listen_address = sockpath;
		}
		
		ARG(0, "io-quiet", "Don't print an info message when listening for an incoming connection") {
			io_quiet = true;
		}
		
		ARG_POSITIONAL("input1.peg {inputN.peg...}", arg) {
			array_append(&inputFiles, arg);
		}
		
		ARG_END {
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
			plugin->dlhandle = dlopen(string_cstr(&tmp), RTLD_LAZY);
			string_clear(&tmp);
		}
		else {
			plugin->dlhandle = dlopen(plugin->path, RTLD_LAZY);
		}
		
		if(plugin->dlhandle == NULL) {
			fprintf(stderr, "Failed to load plugin %s: %s\n", plugin->path, dlerror());
			goto cleanup;
		}
		
		// The init symbol is required
		plugin->init = dlsym(plugin->dlhandle, PEG_PLUGIN_INIT_SYMBOL);
		if(plugin->init == NULL) {
			fprintf(stderr, "Challenge plugin %s is missing required symbol '%s'!\n", plugin->path, PEG_PLUGIN_INIT_SYMBOL);
			goto cleanup;
		}
	}
	
	// Init CPU and peripherals
	EAR_init(&ear);
	MMU_init(&mmu);
	Bus_init(&bus);
	MMU_setContext(&mmu, &ear.ctx);
	MMU_setBusHandler(&mmu, Bus_accessHandler, &bus);
	EAR_setMemoryHandler(&ear, MMU_memoryHandler, &mmu);
	
	cookie.ear = &ear;
	
	// Print a trace of each instruction as it executes
	EAR_setExecHook(&ear, runpeg_trace, &cookie);
	
	if(flagDebug) {
		if(!debugNonInvasive) {
			debugFlags |= DEBUG_INVASIVE;
		}
	}
	else {
		debugFlags |= DEBUG_DETACHED;
	}
	
	cookie.dbg = Debugger_init(&ear, debugFlags);
	
	// Allow debugger to hook instruction execution
	cookie.dbg_trace = Debugger_execHook;
	
	// Insert debugger as man-in-the-middle between the CPU and the MMU
	Debugger_setMemoryHandler(cookie.dbg, ear.mem_fn, ear.mem_cookie);
	EAR_setMemoryHandler(&ear, Debugger_memoryHandler, cookie.dbg);
	
	// Insert debugger as man-in-the-middle between the MMU and the bus
	Debugger_setBusHandler(cookie.dbg, mmu.bus_fn, mmu.bus_cookie);
	MMU_setBusHandler(&mmu, Debugger_busHandler, cookie.dbg);
	
	// For `pmap` command
	Debugger_setBusDumper(cookie.dbg, Bus_dump);
	
	// Set CPU port r/w function
	EAR_setPorts(&ear, &runpeg_portRead, &runpeg_portWrite, &cookie);
	
	void* bootromData = NULL;
	if(bootromFile) {
		// Open code file
		fd = open(bootromFile, O_RDONLY);
		if(fd < 0) {
			perror(bootromFile);
			goto cleanup;
		}
		
		rom_size = lseek(fd, 0, SEEK_END);
		if(rom_size < 0) {
			perror("lseek");
			goto cleanup;
		}
		
		if(rom_size > EAR_VIRTUAL_ADDRESS_SPACE_SIZE) {
			fprintf(stderr, "Bootrom file %s is too large (0x%llX bytes)\n", bootromFile, (long long)rom_size);
			goto cleanup;
		}
		
		// Map contents of code file into memory
		rom_map = mmap(NULL, rom_size, PROT_READ, MAP_SHARED | MAP_FILE, fd, 0);
		close(fd);
		fd = -1;
		if(rom_map == MAP_FAILED) {
			perror("mmap");
			goto cleanup;
		}
		
		bootromData = rom_map;
	}
	else {
		// Use baked-in bootrom
		bootromData = BOOTROM;
		rom_size = BOOTROM_LEN;
	}
	
	// Check if the bootrom is a PEGASUS file
	bootpeg = Pegasus_new();
	if(!bootpeg) {
		perror("alloc");
		goto cleanup;
	}
	
	// Attempt to parse the bootrom as a PEGASUS file
	PegStatus s = Pegasus_parseFromMemory(bootpeg, bootromData, rom_size, false);
	if(s == PEG_SUCCESS) {
		void* seg_rom;
		size_t seg_rom_size;
		if(!Pegasus_getSegmentData(bootpeg, "@ROM", &seg_rom, &seg_rom_size)) {
			fprintf(stderr, "Error: No @ROM segment in bootrom PEGASUS file %s\n", bootromFile);
			goto cleanup;
		}
		
		void* seg_romdata;
		size_t seg_romdata_size;
		if(Pegasus_getSegmentData(bootpeg, "@ROMDATA", &seg_romdata, &seg_romdata_size)) {
			if(seg_romdata != (char*)seg_rom + seg_rom_size) {
				fprintf(stderr, "Error: @ROMDATA segment in bootrom PEGASUS file %s is not at the end of the @ROM segment\n", bootromFile);
				goto cleanup;
			}
			
			// Map both @ROM and @ROMDATA segments together
			seg_rom_size += seg_romdata_size;
		}
		
		// Map ROM segment data as the first memory region of the bus
		Bus_addMemory(
			&bus, bootromFile, BUS_MODE_READ, 0x000000,
			seg_rom_size, seg_rom
		);
		
		Debugger_addPegasusImage(cookie.dbg, bootpeg, false);
	}
	else {
		Pegasus_destroy(&bootpeg);
		
		// Attach ROM as first memory region of the bus
		Bus_addMemory(
			&bus, bootromFile, BUS_MODE_READ, 0x000000,
			rom_size, bootromData
		);
	}
	
	// Allocate memory for RAM
	ram_map = mmap(
		NULL, EAR_VIRTUAL_ADDRESS_SPACE_SIZE,
		PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS,
		-1, 0
	);
	if(ram_map == MAP_FAILED) {
		perror("mmap");
		return EXIT_FAILURE;
	}
	
	// Attach RAM as second memory region of bus
	EAR_Byte next_region = 1;
	Bus_addMemory(
		&bus, "RAM", BUS_MODE_RDWR,
		next_region++ << EAR_REGION_SHIFT, EAR_VIRTUAL_ADDRESS_SPACE_SIZE,
		ram_map
	);
	
	// Load and map input files in their own regions
	foreach(&inputFiles, pFile) {
		if(next_region == 0xFF) {
			fprintf(stderr, "Too many input files!\n");
			goto cleanup;
		}
		
		fd = open(*pFile, O_RDONLY);
		if(fd < 0) {
			perror(*pFile);
			goto cleanup;
		}
		
		off_t filesize = lseek(fd, 0, SEEK_END);
		if(filesize > EAR_VIRTUAL_ADDRESS_SPACE_SIZE) {
			fprintf(stderr, "File %s is too large (0x%llX bytes)\n", *pFile, (long long)filesize);
			goto cleanup;
		}
		
		filesize = EAR_CEIL_PAGE(filesize);
		
		void* map = mmap(
			NULL, filesize, PROT_READ, MAP_SHARED | MAP_FILE, fd, 0
		);
		if(map == MAP_FAILED) {
			perror(*pFile);
			goto cleanup;
		}
		
		close(fd);
		fd = -1;
		
		element_type(&inputFileMaps) mapitem = {
			.map = map,
			.size = filesize,
		};
		array_append(&inputFileMaps, mapitem);
		Bus_addMemory(&bus, *pFile, BUS_MODE_READ, next_region++ << EAR_REGION_SHIFT, filesize, map);
		
		if(cookie.verbose) {
			fprintf(stderr, "Mapped %s to region %02X\n", *pFile, next_region - 1);
		}
		
		if(!userpeg) {
			userpeg = Pegasus_new();
			if(!userpeg) {
				perror("alloc");
				goto cleanup;
			}
			
			PegStatus s = Pegasus_parseFromMemory(userpeg, map, filesize, false);
			if(s == PEG_SUCCESS) {
				Debugger_addPegasusImage(cookie.dbg, userpeg, true);
			}
			else {
				fprintf(
					stderr, "Error: Failed to parse %s as a PEGASUS file: %s\n",
					*pFile, PegStatus_toString(s)
				);
				Pegasus_destroy(&userpeg);
			}
		}
	}
	
	// Initialize checker plugin(s)
	foreach(&plugins, plugin) {
		plugin->obj = plugin->init(&ear, (int)pluginArgs.count, pluginArgs.elems);
		if(plugin->obj == NULL) {
			fprintf(stderr, "Initializing plugin %s failed.\n", plugin->path);
			goto cleanup;
		}
		
		plugin->initialized = true;
	}
	
	//TODO: how should this work now that PEGASUS files aren't parsed and loaded by runpeg?
	// Call the onLoaded callback function in all loaded plugins
	foreach(&plugins, plugin) {
		if(plugin->obj->fn_onLoaded != NULL) {
			if(!plugin->obj->fn_onLoaded(plugin->obj)) {
				goto cleanup;
			}
		}
	}
	
	// Run the bootloader
	r = Debugger_run(cookie.dbg);
	
	if(r != HALT_NONE) {
		fprintf(stderr, "Halted: %s\n", EAR_haltReasonToString(r));
		if(EAR_FAILED(r)) {
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
	
	foreach(&inputFileMaps, pMap) {
		munmap(pMap->map, pMap->size);
	}
	
	foreach(&plugins, plugin) {
		if(plugin->dlhandle != NULL) {
			dlclose(plugin->dlhandle);
		}
	}
	
	if(ram_map != MAP_FAILED) {
		munmap(ram_map, EAR_VIRTUAL_ADDRESS_SPACE_SIZE);
	}
	
	Pegasus_destroy(&bootpeg);
	
	if(rom_map != MAP_FAILED) {
		munmap(rom_map, rom_size);
	}
	
	if(fd >= 0) {
		close(fd);
	}
	
	if(listen_address != NULL) {
		close(cookie.in_fd);
		if(cookie.out_fd != cookie.in_fd) {
			close(cookie.out_fd);
		}
	}
	
	if(cookie.flag_fd >= 0) {
		close(cookie.flag_fd);
	}
	
	return ret;
}
