#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include "pegasus_ear/plugin.h"


#ifndef PEG_DEBUG_PROG
#error Missing required macro PEG_DEBUG_PROG
#endif /* PEG_DEBUG_PROG */

#ifndef PEGSESSION_PORT
#error Missing required macro PEGSESSION_PORT
#endif /* PEGSESSION_PORT */

#ifndef PEG_SESSIONS_MOUNT_POINT
#error Missing required macro PEG_SESSIONS_MOUNT_POINT
#endif /* PEG_SESSIONS_MOUNT_POINT */

// Pegasus session UNIX sockets are at /pegasus-sessions/peg.[0-9a-f]{32}
#define PEG_SESSION_ID_PREFIX PEG_SESSIONS_MOUNT_POINT "/peg."


typedef struct PegDebugPlugin {
	PegPlugin base;
	EAR_PortRead* next_read_fn;
	EAR_PortWrite* next_write_fn;
	void* next_cookie;
	EAR* ear;
	char* flag;
	size_t flag_len;
	size_t flag_read_pos;
} PegDebugPlugin;


static bool pegdebug_portWrite(void* portWrite_cookie, uint8_t port_number, EAR_Byte byte) {
	PegDebugPlugin* ctx = portWrite_cookie;
	if(port_number == 0) {
		if(ctx->next_write_fn != NULL) {
			return ctx->next_write_fn(ctx->next_cookie, port_number, byte);
		}
	}
	return false;
}

static bool pegdebug_portRead(void* portRead_cookie, uint8_t port_number, EAR_Byte* out_byte) {
	PegDebugPlugin* ctx = portRead_cookie;
	
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

static void pegdebug_destroy(PegPlugin* plugin) {
	PegDebugPlugin* ctx = (PegDebugPlugin*)plugin;
	free(ctx->flag);
	free(ctx);
}

PegPlugin* PegPlugin_Init(EAR* ear, PegasusLoader* pegload) {
	(void)pegload;
	
	PegDebugPlugin* ctx = calloc(1, sizeof(*ctx));
	if(!ctx) {
		return NULL;
	}
	
	ctx->ear = ear;
	
	// We have a destructor but not an onLoaded function
	ctx->base.fn_destroy = &pegdebug_destroy;
	
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
	EAR_setPorts(ctx->ear, &pegdebug_portRead, &pegdebug_portWrite, ctx);
	
	return &ctx->base;
}


int main(int argc, char** argv, char** envp) {
	(void)argc;
	(void)argv;
	int ret = EXIT_FAILURE;
	
	ret = chdir("/ctf");
	if(ret < 0) {
		fprintf(stderr, "Error: Failed to change to CTF directory: %s\n", strerror(errno));
		goto out;
	}
	
	int random_fd = open("/dev/urandom", O_RDONLY);
	if(random_fd < 0) {
		fprintf(stderr, "Error: Unable to open random device: %s\n", strerror(errno));
		goto out;
	}
	
	char randbytes[8];
	if(read(random_fd, randbytes, sizeof(randbytes)) != sizeof(randbytes)) {
		fprintf(stderr, "Error: Failed to read random data from device: %s\n", strerror(errno));
		goto out;
	}
	
	// Create a securely random session ID for the PEGASUS connection
	char listen_address[sizeof(PEG_SESSION_ID_PREFIX) - 1 + sizeof(randbytes) * 2 + 1] = PEG_SESSION_ID_PREFIX;
	char* randstr = listen_address + strlen(listen_address);
	size_t randstr_len = sizeof(listen_address) - (randstr - listen_address) - 1;
	unsigned i;
	const char* hexchars = "0123456789abcdef";
	for(i = 0; i < randstr_len; i += 2) {
		randstr[i] = hexchars[(randbytes[i / 2] >> 4) & 0xf];
		randstr[i + 1] = hexchars[randbytes[i / 2] & 0xf];
	}
	listen_address[sizeof(listen_address) - 1] = '\0';
	
	// Get path of this program to pass as the plugin path for runpeg
	char progpath[1024];
	errno = 0;
	ssize_t progpathlen = readlink("/proc/self/exe", progpath, sizeof(progpath) - 1);
	if(progpathlen < 0) {
		fprintf(stderr, "Error: Failed to get path of current executable: %s\n", strerror(errno));
		goto out;
	}
	progpath[progpathlen] = '\0';
	
	printf("Connect to the PEGASUS session on port %d with session ID '%s'.\n", PEGSESSION_PORT, randstr);
	
	// This challenge simply runs the PEGASUS program and serves its port 0 I/O over the
	// connected TCP socket. Easiest way to do that is to invoke runpeg with this module
	// as a plugin.
	char* runpeg_argv[] = {
		"runpeg",
		"--plugin", progpath,
		"--io-listen", listen_address,
		"--io-quiet",
		"--debug",
		PEG_DEBUG_PROG,
		NULL
	};
	if(execve("/usr/local/bin/runpeg", runpeg_argv, envp) < 0) {
		fprintf(stderr, "Failed to execute runpeg: %s\n", strerror(errno));
	}
	
out:
	if(random_fd != -1) {
		close(random_fd);
	}
	
	return ret;
}
