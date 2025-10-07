//
//  pegasus.c
//  PegasusEar
//
//  Created by Kevin Colley on 5/11/19.
//

#include "pegasus.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "common/dynamic_string.h"
#include "libear/types.h"


typedef uint16_t Pegasus_CommandType;
#define PEGCMD_SEGMENT ((Pegasus_CommandType)1)
#define PEGCMD_ENTRYPOINT ((Pegasus_CommandType)2)
#define PEGCMD_SYMTAB ((Pegasus_CommandType)3)
#define PEGCMD_RELTAB ((Pegasus_CommandType)4)

typedef struct Pegasus_CommandHeader Pegasus_CommandHeader;
struct Pegasus_CommandHeader {
	uint16_t cmdsize;
	Pegasus_CommandType cmdtype;
};


static PegStatus Pegasus_parse(Pegasus* self);


static bool Pegasus_readString(Pegasus* self, dynamic_string* str) {
	if(!str) {
		return false;
	}
	
	// Mark string as empty
	str->count = 0;
	
	// Read first byte of the lestring
	char c = 0;
	if(!Pegasus_read(self, &c, 1)) {
		return false;
	}
	
	// Add first character to the string
	string_appendChar(str, c & 0x7f);
	
	// Special case: when first byte is zero, it's the empty string
	if(c == 0) {
		return true;
	}
	
	// Keep going as long as the previous byte's continuation bit is set
	while(c & 0x80) {
		if(!Pegasus_read(self, &c, 1)) {
			return false;
		}
		
		// Append current byte's character value to the string
		string_appendChar(str, c & 0x7f);
	}
	
	return true;
}


/*! Allocate storage for a Pegasus object. */
Pegasus* Pegasus_new(void) {
	return calloc(1, sizeof(Pegasus));
}


/*!
 * @brief Parse a pegasus file from the given file path.
 * 
 * @param filename Path to the pegasus file to load
 */
PegStatus Pegasus_parseFromFile(Pegasus* self, const char* filename) {
	PegStatus s = PEG_SUCCESS;
	int fd = -1;
	dynamic_array(char) file_data = {0};
	char read_buf[256];
	ssize_t bytes_read = 0;
	
	// Normal filename
	if(fd == -1) {
		fd = open(filename, O_RDONLY);
		if(fd < 0) {
			s = PEG_IO_ERROR;
			goto cleanup;
		}
	}
	
	while(true) {
		bytes_read = read(fd, read_buf, sizeof(read_buf));
		if(bytes_read < 0) {
			s = PEG_IO_ERROR;
			goto cleanup;
		}
		else if(bytes_read == 0) {
			break;
		}
		
		array_extend(&file_data, read_buf, bytes_read);
	}
	
	array_shrink(&file_data);
	
	// Parse pegasus file
	s = Pegasus_parseFromMemory(self, file_data.elems, file_data.count, true);
	if(s != PEG_SUCCESS) {
		array_clear(&file_data);
		goto cleanup;
	}
	
	// When the above succeeds, the Pegasus object takes ownership of the
	// memory allocated for file_data. Therefore, don't array_clear() it.
	
cleanup:
	if(fd != -1) {
		close(fd);
	}
	
	return s;
}


/*!
 * @brief Parse a pegasus file loaded into memory.
 * 
 * @param data Pointer to the beginning of the PEGASUS file data
 * @param size Number of bytes in the PEGASUS file data
 * @param shouldFree True if free() should be called on the data pointer
 *                   when the Pegasus object is destructed
 */
PegStatus Pegasus_parseFromMemory(Pegasus* self, void* data, size_t size, bool shouldFree) {
	if(!data) {
		return PEG_INVALID_PARAMETER;
	}
	
	if(self->should_free) {
		destroy(&self->peg_data);
	}
	
	memset(self, 0, sizeof(*self));
	self->peg_data = data;
	self->peg_size = size;
	self->should_free = shouldFree;
	
	return Pegasus_parse(self);
}


/*!
 * @brief Seek to a specific position in an opened PEGASUS file.
 * 
 * @param offset Signed offset in bytes from the seek position
 * @param whence One of SEEK_SET, SEEK_CUR, or SEEK_END, with the same semantics as fseek
 * @return True if the seek was successful, or false otherwise
 */
bool Pegasus_seek(Pegasus* self, ssize_t offset, int whence) {
	switch(whence) {
		case SEEK_SET:
			if(offset < 0 || (size_t)offset > self->peg_size) {
				return false;
			}
			self->peg_pos = (size_t)offset;
			break;
		
		case SEEK_END:
			if(offset > 0 || (size_t)-offset > self->peg_size) {
				return false;
			}
			self->peg_pos = self->peg_size - (size_t)-offset;
			break;
		
		case SEEK_CUR:
			if(offset >= 0) {
				// offset is nonnegative
				ssize_t bytesFromEnd = self->peg_size - self->peg_pos;
				if(offset > bytesFromEnd) {
					return false;
				}
				self->peg_pos += offset;
			}
			else {
				// offset is negative
				if((size_t)-offset > self->peg_pos) {
					return false;
				}
				self->peg_pos -= (size_t)-offset;
			}
			break;
		
		default:
			return false;
	}
	
	return true;
}


/*!
 * @brief Attempt to read a specific number of bytes from the current seek position in
 *        an opened PEGASUS file.
 * 
 * @param data Pointer to where the data from the PEGASUS file should be written
 * @param nbytes Number of bytes that should be read from the PEGASUS file
 * @return True if the data was successfully read, or false otherwise
 */
bool Pegasus_read(Pegasus* self, void* data, size_t nbytes) {
	const void* src = Pegasus_getData(self);
	if(!Pegasus_seek(self, nbytes, SEEK_CUR)) {
		return false;
	}
	
	memcpy(data, src, nbytes);
	return true;
}


/*!
 * @brief Attempt to write a specific number of bytes to the current seek position in
 *        an opened PEGASUS file.
 * 
 * @note This will only affect the in-memory contents of the PEGASUS file, not the contents on disk.
 * @param data Source pointer where data should be copied from
 * @param nbytes Number of bytes to copy from the input into the loaded PEGASUS file contents
 * @return True if the data was successfully written, or false otherwise
 */
bool Pegasus_write(Pegasus* self, const void* data, size_t nbytes) {
	void* dst = Pegasus_getData(self);
	if(!Pegasus_seek(self, nbytes, SEEK_CUR)) {
		return false;
	}
	
	memcpy(dst, data, nbytes);
	return true;
}


static PegStatus Pegasus_parse(Pegasus* self) {
	PegStatus s = PEG_SUCCESS;
	dynamic_string nameBuffer = {0};
	
	// Read in header
	if(!Pegasus_read(self, &self->header, sizeof(self->header))) {
		s = PEG_TRUNC_HEADER;
		goto cleanup;
	}
	
	// Check magic field value
	if(memcmp(self->header.magic, PEGASUS_MAGIC, sizeof(self->header.magic)) != 0) {
		s = PEG_BAD_MAGIC;
		goto cleanup;
	}
	
	// Read command count
	uint16_t numcmds;
	if(!Pegasus_read(self, &numcmds, sizeof(numcmds))) {
		s = PEG_TRUNC_HEADER;
		goto cleanup;
	}
	
	// Read in load commands
	uint16_t i, j;
	for(i = 0; i < numcmds; i++) {
		// Read load command header
		size_t cmdStart = self->peg_pos;
		Pegasus_CommandHeader cmd;
		if(!Pegasus_read(self, &cmd, sizeof(cmd))) {
			s = PEG_TRUNC_CMD_HEADER;
			goto cleanup;
		}
		
		// Sanity check command size
		if(cmd.cmdsize < sizeof(cmd)) {
			s = PEG_TRUNC_CMD_HEADER;
			goto cleanup;
		}
		
		// Process the load command depending on its type
		size_t cmdEnd = cmdStart + cmd.cmdsize;
		switch(cmd.cmdtype) {
			case PEGCMD_SEGMENT: {
				Pegasus_Segment seg = {0};
				if(!Pegasus_read(self, &seg.virtual_page, SIZE_THROUGH_FIELD(Pegasus_Segment, prot) - offsetof(Pegasus_Segment, virtual_page))) {
					s = PEG_TRUNC_SEGMENT;
					goto cleanup;
				}
				if(!Pegasus_readString(self, &nameBuffer)) {
					s = PEG_TRUNC_SEGMENT_NAME;
					goto cleanup;
				}
				seg.name = string_dup(&nameBuffer);
				array_append(&self->segments, seg);
				break;
			}
			
			case PEGCMD_ENTRYPOINT: {
				Pegasus_Entrypoint* pentry = Pegasus_getData(self);
				if(!Pegasus_seek(self, sizeof(*pentry), SEEK_CUR)) {
					s = PEG_TRUNC_ENTRYPOINT;
					goto cleanup;
				}
				
				array_append(&self->entrypoints, pentry);
				break;
			}
			
			case PEGCMD_SYMTAB: {
				// Illegal to have multiple symbol tables
				if(self->symbols.count != 0) {
					s = PEG_MULTIPLE_SYMTABS;
					goto cleanup;
				}
				
				// Read symbol table size
				uint16_t sym_count = 0;
				if(!Pegasus_read(self, &sym_count, sizeof(sym_count))) {
					s = PEG_TRUNC_SYMTAB;
					goto cleanup;
				}
				
				for(j = 0; j < sym_count; j++) {
					// Re-align offset
					if(self->peg_pos & 1) {
						if(!Pegasus_seek(self, 1, SEEK_CUR)) {
							s = PEG_TRUNC_SYMTAB;
							goto cleanup;
						}
					}
					
					// Read symbol value
					Pegasus_Symbol sym = {0};
					if(!Pegasus_read(self, &sym.value, sizeof(sym.value))) {
						s = PEG_TRUNC_SYMTAB;
						goto cleanup;
					}
					
					// Read symbol name
					if(!Pegasus_readString(self, &nameBuffer)) {
						s = PEG_TRUNC_SYMBOL_NAME;
						goto cleanup;
					}
					sym.name = string_dup(&nameBuffer);
					sym.index = j;
					array_append(&self->symbols, sym);
				}
				
				break;
			}
			
			case PEGCMD_RELTAB: {
				// Illegal to have multiple reloc tables
				if(self->relocs.count != 0) {
					s = PEG_MULTIPLE_RELTABS;
					goto cleanup;
				}
				
				// Read reloc table size
				uint16_t reloc_count = 0;
				if(!Pegasus_read(self, &reloc_count, sizeof(reloc_count))) {
					s = PEG_TRUNC_RELTAB;
					goto cleanup;
				}
				
				for(j = 0; j < reloc_count; j++) {
					Pegasus_Relocation reloc = {0};
					if(!Pegasus_read(self, &reloc, sizeof(reloc))) {
						s = PEG_TRUNC_RELTAB;
						goto cleanup;
					}
					
					array_append(&self->relocs, reloc);
				}
				
				break;
			}
			
			default:
				s = PEG_BAD_CMD;
				goto cleanup;
		}
		
		// Seek past the end of this load command
		Pegasus_seek(self, cmdEnd, SEEK_SET);
	}
	
cleanup:
	string_clear(&nameBuffer);
	return s;
}


/*!
 * @brief Retrieves a pointer to the current file position within the PEGASUS file data.
 * 
 * @return Pointer to the current position within the PEGASUS file data
 */
void* Pegasus_getData(Pegasus* self) {
	return (char*)self->peg_data + self->peg_pos;
}


/*!
 * @brief Attempt to get the address and size of a named segment's data.
 * 
 * @param name Name of segment to look up
 * @param out_data Output pointer to the segment data address
 * @param out_size Output size of the segment data
 * @return True on success, or false otherwise
 */
bool Pegasus_getSegmentData(Pegasus* self, const char* name, void** out_data, size_t* out_size) {
	bool success = false;
	size_t orig_pos = self->peg_pos;
	*out_data = NULL;
	*out_size = 0;
	
	Pegasus_Segment* found = NULL;
	foreach(&self->segments, seg) {
		if(!strcmp(seg->name, name)) {
			found = seg;
			break;
		}
	}
	if(!found) {
		return false;
	}
	
	uint32_t foff = (uint32_t)found->file_page * EAR_PAGE_SIZE;
	if(!Pegasus_seek(self, foff, SEEK_SET)) {
		goto out;
	}
	
	*out_data = Pegasus_getData(self);
	*out_size = (uint32_t)found->present_page_count * EAR_PAGE_SIZE;
	success = true;
	
out:
	self->peg_pos = orig_pos;
	return success;
}


static int cmp_syms_by_name(const void* a, const void* b) {
	const Pegasus_Symbol* sym_a = *(const Pegasus_Symbol**)a;
	const Pegasus_Symbol* sym_b = *(const Pegasus_Symbol**)b;
	
	return strcmp(sym_a->name, sym_b->name);
}


static int cmp_syms_by_value(const void* a, const void* b) {
	const Pegasus_Symbol* sym_a = *(const Pegasus_Symbol**)a;
	const Pegasus_Symbol* sym_b = *(const Pegasus_Symbol**)b;
	
	return (int)sym_a->value - (int)sym_b->value;
}


static int search_sym_by_name(const void* key, const void* elem) {
	const char* name = key;
	const Pegasus_Symbol* sym = *(const Pegasus_Symbol**)elem;
	
	return strcmp(name, sym->name);
}


/*!
 * @brief Look up a symbol by its name.
 * 
 * @param name Name of the symbol to look up
 * @return Symbol if found, or NULL otherwise
 */
Pegasus_Symbol* Pegasus_findSymbolByName(Pegasus* self, const char* name) {
	if(self->symbols_sorted_by_name.count != self->symbols.count) {
		array_clear(&self->symbols_sorted_by_name);
		
		foreach(&self->symbols, pSym) {
			array_append(&self->symbols_sorted_by_name, pSym);
		}
		
		qsort(
			self->symbols_sorted_by_name.elems, self->symbols_sorted_by_name.count,
			element_size(&self->symbols_sorted_by_name), cmp_syms_by_name
		);
	}
	
	Pegasus_Symbol** found = bsearch(
		name, self->symbols_sorted_by_name.elems, self->symbols_sorted_by_name.count,
		element_size(&self->symbols_sorted_by_name), search_sym_by_name
	);
	
	return found ? *found : NULL;
}


/*!
 * @brief Look up the nearest symbol before the given address.
 * 
 * @param addr Virtual address to look up
 * @return Symbol if found, or NULL otherwise
 */
Pegasus_Symbol* Pegasus_findSymbolByAddress(Pegasus* self, uint16_t addr) {
	if(self->symbols_sorted_by_value.count != self->symbols.count) {
		array_clear(&self->symbols_sorted_by_value);
		
		foreach(&self->symbols, pSym) {
			array_append(&self->symbols_sorted_by_value, pSym);
		}
		
		qsort(
			self->symbols_sorted_by_value.elems, self->symbols_sorted_by_value.count,
			element_size(&self->symbols_sorted_by_value), cmp_syms_by_value
		);
	}
	
	if(!self->symbols_sorted_by_value.count) {
		// No symbols to search
		return NULL;
	}
	
	ssize_t lo = 0, hi = self->symbols_sorted_by_value.count - 1;
	while(lo <= hi) {
		ssize_t mid = (lo + hi + 1) / 2;
		Pegasus_Symbol* sym = *array_at(&self->symbols_sorted_by_value, mid);
		
		if(addr > sym->value) {
			// Symbol is before the target address
			lo = mid + 1;
		}
		else if(addr < sym->value) {
			// Symbol is after the target address
			hi = mid - 1;
		}
		else {
			return sym;
		}
	}
	
	if(lo == 0) {
		// No symbols before the target address
		return NULL;
	}
	
	// Return the closest symbol before the target address
	return *array_at(&self->symbols_sorted_by_value, lo - 1);
}


/*! Destroy a Pegasus object instance that came from a call to Pegasus_new() */
void Pegasus_destroy(Pegasus** pself) {
	Pegasus* self = *pself;
	if(!self) {
		return;
	}
	
	*pself = NULL;
	
	array_clear(&self->symbols_sorted_by_name);
	array_clear(&self->symbols_sorted_by_value);
	
	foreach(&self->segments, pseg) {
		destroy(&pseg->name);
	}
	array_clear(&self->segments);
	
	foreach(&self->symbols, psym) {
		destroy(&psym->name);
	}
	array_clear(&self->symbols);
	
	array_clear(&self->entrypoints);
	array_clear(&self->relocs);
	
	if(self->should_free) {
		destroy(&self->peg_data);
	}
	
	destroy(&self);
}
