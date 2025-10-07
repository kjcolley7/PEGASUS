//
//  submit.c
//  PegasusEar
//
//  Created by Kevin Colley on 11/3/2020.
//

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "libraries/kjc_argparse/kjc_argparse.h"


/*!
 * Maximum allowed size of a PEG file is 70 KiB.
 * It doesn't make sense to allow PEG files much larger than the EAR address space size (64 KiB).
 */
#define PEG_SIZE_MAX (0x1000 * 70)


static bool send_peg_file(int sock, const void* peg_data, uint32_t peg_size) {
	char buf[256] = {0};
	const char* banner = "PEG SIZE?\n";
	size_t banner_len = strlen(banner);
	
	errno = 0;
	ssize_t xfer = recv(sock, buf, banner_len, MSG_WAITALL);
	if(xfer < 0) {
		fprintf(stderr, "Failed to receive data from the server: %s\n", strerror(errno));
		return false;
	}
	else if(xfer != (ssize_t)banner_len) {
		fprintf(stderr, "Premature EOF in server response: %s\n", buf);
		return false;
	}
	
	if(memcmp(buf, banner, banner_len) != 0) {
		fprintf(stderr, "Received unexpected data from server: %s\n", banner);
		return false;
	}
	
	uint32_t net_size = htonl(peg_size);
	errno = 0;
	xfer = send(sock, &net_size, sizeof(net_size), 0);
	if(xfer != sizeof(net_size)) {
		fprintf(stderr, "Failed to send PEGASUS size: %s\n", strerror(errno));
		return false;
	}
	
	banner = "PEG DATA?\n";
	banner_len = strlen(banner);
	
	errno = 0;
	xfer = recv(sock, buf, banner_len, MSG_WAITALL);
	if(xfer < 0) {
		fprintf(stderr, "Failed to receive data from the server: %s\n", strerror(errno));
		return false;
	}
	else if(xfer != (ssize_t)banner_len) {
		fprintf(stderr, "Premature EOF in server response: %s\n", buf);
		return false;
	}
	
	if(memcmp(buf, banner, banner_len) != 0) {
		fprintf(stderr, "Received unexpected data from server: %s\n", banner);
		return false;
	}
	
	errno = 0;
	xfer = send(sock, peg_data, peg_size, 0);
	if(xfer != peg_size) {
		fprintf(stderr, "Failed to send PEGASUS data: %s\n", strerror(errno));
		return false;
	}
	
	fprintf(stderr, "PEGASUS file submitted!\n");
	fprintf(stderr, "Challenge server says:\n");
	
	while(1) {
		errno = 0;
		xfer = recv(sock, buf, sizeof(buf) - 1, MSG_WAITALL);
		if(xfer == 0) {
			break;
		}
		if(xfer < 0) {
			fprintf(stderr, "Error receiving data from server: %s\n", strerror(errno));
			return false;
		}
		
		if(write(STDOUT_FILENO, buf, xfer) != xfer) {
			fprintf(stderr, "Failed to write to stdout\n");
			return false;
		}
	}
	
	return true;
}


int main(int argc, char** argv) {
	int ret = EXIT_FAILURE;
	bool parseSuccess = false;
	const char* pegFile = NULL;
	const char* server = NULL;
	uint16_t port = 0;
	
	ARGPARSE(argc, argv) {
		ARGPARSE_SET_OUTPUT(stderr);
		
		ARG('h', "help", "Displays this help message") {
			ARGPARSE_HELP();
			break;
		}
		
		ARG_STRING('s', "server", "Server address for the challenge server", str) {
			if(server != NULL) {
				fprintf(stderr, "--server cannot be used more than once!\n");
				ARGPARSE_HELP();
				break;
			}
			server = str;
		}
		
		ARG_INT('p', "port", "Port number for the challenge server", num) {
			if(port != 0) {
				fprintf(stderr, "--port cannot be used more than once!\n");
				ARGPARSE_HELP();
				break;
			}
			if(num <= 0 || UINT16_MAX < num) {
				fprintf(stderr, "Port number is out of range!\n");
				ARGPARSE_HELP();
				break;
			}
			port = num;
		}
		
		ARG_POSITIONAL("<solution>.peg", arg) {
			if(pegFile != NULL) {
				fprintf(stderr, "Only one input file may be provided!\n");
				ARGPARSE_HELP();
				break;
			}
			pegFile = arg;
		}
		
		ARG_END() {
			if(pegFile == NULL) {
				fprintf(stderr, "No input files\n");
			}
			else if(server == NULL) {
				fprintf(stderr, "Missing required argument --server\n");
			}
			else if(port == 0) {
				fprintf(stderr, "Missing required argument --port\n");
			}
			else {
				parseSuccess = true;
				break;
			}
			
			ARGPARSE_HELP();
		}
	}
	if(!parseSuccess) {
		return EXIT_FAILURE;
	}
	
	int peg_fd = open(pegFile, O_RDONLY);
	if(peg_fd == -1) {
		perror(pegFile);
		return EXIT_FAILURE;
	}
	
	off_t seek_pos = lseek(peg_fd, 0, SEEK_END);
	if(seek_pos > UINT32_MAX) {
		fprintf(stderr, "PEGASUS file is too large\n");
		close(peg_fd);
		return EXIT_FAILURE;
	}
	
	uint32_t peg_size = (uint32_t)seek_pos;
	void* peg_data = mmap(NULL, peg_size, PROT_READ, MAP_FILE | MAP_PRIVATE, peg_fd, 0);
	close(peg_fd);
	if(peg_data == MAP_FAILED) {
		perror("mmap");
		return EXIT_FAILURE;
	}
	
	char port_str[6];
	snprintf(port_str, sizeof(port_str), "%hu", port);
	
	struct addrinfo* ai = NULL;
	int gai_err = getaddrinfo(server, port_str, NULL, &ai);
	if(gai_err != 0) {
		fprintf(stderr, "Failed to resolve server address %s: %s\n", server, gai_strerror(gai_err));
		return EXIT_FAILURE;
	}
	
	struct addrinfo* cur;
	for(cur = ai; cur != NULL; cur = cur->ai_next) {
		errno = 0;
		int sock = socket(AF_INET, SOCK_STREAM, 0);
		if(sock < 0) {
			fprintf(stderr, "Error creating socket: %s\n", strerror(errno));
			continue;
		}
		
		errno = 0;
		if(connect(sock, cur->ai_addr, cur->ai_addrlen) != 0) {
			fprintf(stderr, "Connecting to %s:%hu failed: %s\n", server, port, strerror(errno));
			close(sock);
			continue;
		}
		
		ret = send_peg_file(sock, peg_data, peg_size) ? EXIT_SUCCESS : EXIT_FAILURE;
		close(sock);
		break;
	}
	
	munmap(peg_data, peg_size);
	if(ai != NULL) {
		freeaddrinfo(ai);
	}
	return ret;
}
