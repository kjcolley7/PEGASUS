//
//  pegasus.h
//  PegasusEar
//
//  Created by Kevin Colley on 5/11/19.
//

#ifndef PEG_PEGASUS_H
#define PEG_PEGASUS_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "common/dynamic_array.h"
#include "pegstatus.h"


#define SIZE_THROUGH_FIELD(objtype, field) (offsetof(objtype, field) + sizeof(((objtype*)NULL)->field))

typedef struct Pegasus Pegasus;
typedef struct Pegasus_Header Pegasus_Header;
typedef struct Pegasus_Segment Pegasus_Segment;
typedef struct Pegasus_Symbol Pegasus_Symbol;
typedef struct Pegasus_Relocation Pegasus_Relocation;
typedef struct Pegasus_Entrypoint Pegasus_Entrypoint;

struct Pegasus_Header {
	char magic[8];
	char arch[4];
} __attribute__((packed));

#define PEGASUS_MAGIC "\x7fPEGASUS"
#define PEGASUS_ARCH_EAR "_EAR"

struct Pegasus {
	void* peg_data;
	size_t peg_size;
	size_t peg_pos;
	bool should_free;
	
	Pegasus_Header header;
	
	dynamic_array(Pegasus_Segment) segments;
	dynamic_array(Pegasus_Symbol) symbols;
	dynamic_array(Pegasus_Relocation) relocs;
	
	// This is an array of pointers into the PEGASUS file data.
	// If the entrypoint commands were just copied while parsing,
	// then relocations wouldn't be able to affect them (at least
	// not without a lot of extra bookkeeping).
	dynamic_array(Pegasus_Entrypoint*) entrypoints;
};

struct Pegasus_Segment {
	char* name;
	uint8_t vppn;
	uint8_t vpage_count;
	uint16_t foff;
	uint16_t fsize;
	uint8_t prot;
};

struct Pegasus_Entrypoint {
	uint16_t a0, a1, a2, a3, a4, a5, pc, dpc;
} __attribute__((packed));

struct Pegasus_Symbol {
	char* name;
	uint16_t value;
	uint16_t index;
};

struct Pegasus_Relocation {
	uint16_t symbol_index;
	uint16_t fileoff;
} __attribute__((packed));


/*! Allocate storage for a Pegasus object */
Pegasus* Pegasus_new(void);

/*!
 * @brief Parse a pegasus file from the given file path.
 * 
 * @param filename Path to the pegasus file to load
 */
PegStatus Pegasus_parseFromFile(Pegasus* self, const char* filename);

/*!
 * @brief Parse a pegasus file loaded into memory.
 * 
 * @param data Pointer to the beginning of the PEGASUS file data
 * @param size Number of bytes in the PEGASUS file data
 * @param shouldFree True if free() should be called on the data pointer
 *                   when the Pegasus object is destructed
 */
PegStatus Pegasus_parseFromMemory(Pegasus* self, void* data, size_t size, bool shouldFree);

/*!
 * @brief Seek to a specific position in an opened PEGASUS file.
 * 
 * @param offset Signed offset in bytes from the seek position
 * @param whence One of SEEK_SET, SEEK_CUR, or SEEK_END, with the same semantics as fseek
 * @return True if the seek was successful, or false otherwise
 */
bool Pegasus_seek(Pegasus* self, ssize_t offset, int whence);

/*!
 * @brief Attempt to read a specific number of bytes from the current seek position in
 * an opened PEGASUS file.
 * 
 * @param data Pointer to where the data from the PEGASUS file should be written
 * @param nbytes Number of bytes that should be read from the PEGASUS file
 * @return True if the data was successfully read, or false otherwise
 */
bool Pegasus_read(Pegasus* self, void* data, size_t nbytes);

/*!
 * @brief Attempt to write a specific number of bytes to the current seek position in an opened
 * PEGASUS file. This will only affect the in-memory contents of the PEGASUS file, not the
 * contents on disk.
 * 
 * @param data Source pointer where data should be copied from
 * @param nbytes Number of bytes to copy from the input into the loaded PEGASUS file contents
 * @return True if the data was successfully written, or false otherwise
 */
bool Pegasus_write(Pegasus* self, const void* data, size_t nbytes);

/*!
 * @brief Retrieves a pointer to the current file position within the PEGASUS file data.
 * 
 * @return Pointer to the current position within the PEGASUS file data
 */
void* Pegasus_getData(Pegasus* self);

/*! Destroy a Pegasus object instance that came from a call to Pegasus_new() */
void Pegasus_destroy(Pegasus** pself);

#endif /* PEG_PEGASUS_H */
