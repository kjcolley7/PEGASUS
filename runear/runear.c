//
//  runear.c
//  PegasusEar
//
//  Created by Kevin Colley on 4/24/19.
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "pegasus_ear/ear.h"


static bool earcb_port_read(void* cookie, uint8_t port_number, EAR_Byte* out_byte) {
	// Cookie is unused
	(void)cookie;
	
	// Only port 0 is supported
	if(port_number != 0) {
		return false;
	}
	
	// Read byte from stdin
	if(read(STDIN_FILENO, out_byte, 1) != 1) {
		if(!g_interrupted) {
			perror("read");
		}
		return false;
	}
	
	return true;
}


static bool earcb_port_write(void* cookie, uint8_t port_number, EAR_Byte byte) {
	// Cookie is unused
	(void)cookie;
	
	// Only port 0 is supported
	if(port_number != 0) {
		return false;
	}
	
	// Write byte to stdout
	if(write(STDOUT_FILENO, &byte, 1) != 1) {
		if(!g_interrupted) {
			perror("write");
		}
		return false;
	}
	return true;
}


int main(int argc, char** argv) {
	static EAR ear;
	EAR_HaltReason r;
	
	if(argc != 2) {
		fprintf(stderr, "Usage: %s input.earbin\n", argv[0]);
		return EXIT_FAILURE;
	}
	
	// Init CPU
	EAR_init(&ear, 0);
	
	// Open code file
	int fd = open(argv[1], O_RDONLY);
	if(fd < 0) {
		perror(argv[1]);
		return EXIT_FAILURE;
	}
	
	// Map contents of code file into memory
	ssize_t code_size = lseek(fd, 0, SEEK_END);
	void* map = mmap(NULL, code_size, PROT_READ, MAP_SHARED | MAP_FILE, fd, 0);
	close(fd);
	if(map == MAP_FAILED) {
		perror("mmap");
		return EXIT_FAILURE;
	}
	
	// Allocate and fill physical memory pages w/ code contents
	EAR_PageNumber code_ppns[256];
	uint8_t page_count = EAR_CEIL_PAGE(code_size) / EAR_PAGE_SIZE;
	if(EAR_allocPhys(&ear, page_count, code_ppns) != page_count) {
		fprintf(stderr, "Out of physical memory pages!\n");
		return EXIT_FAILURE;
	}
	if(EAR_copyinPhys(&ear, code_ppns, page_count, 0, map, code_size) != code_size) {
		fprintf(stderr, "Failed to copy code into address space\n");
		return EXIT_FAILURE;
	}
	munmap(map, code_size);
	
	// Map code segment in virtual memory
	EAR_Size code_vmaddr = 0x100;
	EAR_addSegment(&ear, code_vmaddr, EAR_CEIL_PAGE(code_size), code_ppns, EAR_PROT_READ | EAR_PROT_EXECUTE, EAR_NULL);
	
	// Set CPU port r/w function
	EAR_setPorts(&ear, &earcb_port_read, &earcb_port_write, NULL);
	
	// Call target function
	r = EAR_invokeFunction(&ear, code_vmaddr, 0, 0, 0, 0, 0, 0, 0, true);
	if(r != HALT_RETURN) {
		fprintf(stderr, "Unexpected halt reason %d: %s\n", r, EAR_haltReasonToString(r));
	}
	
	return 0;
}
