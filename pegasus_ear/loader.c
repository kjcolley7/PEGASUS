//
//  loader.c
//  PegasusEar
//
//  Created by Kevin Colley on 2/23/20.
//

#include "loader.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "pegasus.h"


static int symbol_cmp(const void* a, const void* b) {
	const Pegasus_Symbol* lhs = *(const Pegasus_Symbol**)a;
	const Pegasus_Symbol* rhs = *(const Pegasus_Symbol**)b;
	
	return strcmp(lhs->name, rhs->name);
}


/*! Initialize a PegasusLoader object with default values */
void PegasusLoader_init(PegasusLoader* self) {
	memset(self, 0, sizeof(*self));
}


/*!
 * @brief Sets the callback function that resolves unresolved symbol imports
 * 
 * @param resolveSymbol Callback function pointer used to resolve missing symbol imports
 * @param resolveSymbol_cookie Opaque value passed directly to the callback
 */
void PegasusLoader_setSymbolResolver(PegasusLoader* self, PegResolveSymbol* resolveSymbol, void* resolveSymbol_cookie) {
	self->resolveSymbol = resolveSymbol;
	self->resolveSymbol_cookie = resolveSymbol_cookie;
}

/*! Returns true when a symbol resolver callback is currently registered */
bool PegasusLoader_hasSymbolResolver(PegasusLoader* self) {
	return self->resolveSymbol != NULL;
}


/*!
 * @brief Sets the callback function that maps segments into target memory
 * 
 * @param mapSegment Callback function pointer used to map segments into target memory
 * @param mapSegment_cookie Opaque value passed directly to the callback
 */
void PegasusLoader_setSegmentMapper(PegasusLoader* self, PegMapSegment* mapSegment, void* mapSegment_cookie) {
	self->mapSegment = mapSegment;
	self->mapSegment_cookie = mapSegment_cookie;
}

/*! Returns true when a segment maper callback is currently registered */
bool PegasusLoader_hasSegmentMapper(PegasusLoader* self) {
	return self->mapSegment != NULL;
}


/*!
 * @brief Sets the callback function that invokes entrypoints while loading a PEGASUS file
 * 
 * @param handleEntry Callback function pointer used to invoke entrypoints
 * @param handleEntry_cookie Opaque value passed directly to the callback
 */
void PegasusLoader_setEntrypointHandler(PegasusLoader* self, PegHandleEntry* handleEntry, void* handleEntry_cookie) {
	self->handleEntry = handleEntry;
	self->handleEntry_cookie = handleEntry_cookie;
}

/*! Returns true when an entrypoint handler callback is currently registered */
bool PegasusLoader_hasEntrypointHandler(PegasusLoader* self) {
	return self->handleEntry != NULL;
}


/*!
 * @brief Add a parsed PEGASUS file object to the loader object
 * 
 * @param peg Parsed PEGASUS file object
 */
void PegasusLoader_add(PegasusLoader* self, Pegasus* peg) {
	// Collect symbols
	foreach(&peg->symbols, psym) {
		if(psym->value == 0xFFFF) {
			array_append(&self->imported_symbols, psym);
		}
		else {
			array_append(&self->exported_symbols, psym);
		}
	}
	
	// Add PEGASUS object to the array
	array_append(&self->pegs, peg);
}


/*!
 * @brief Attempt to resolve all imported symbols using either symbols defined by other added PEGASUS
 * files or with the user provided symbol resolver function. This also applies relocations.
 * 
 * @return PEG_SUCCESS, or an error status code.
 */
PegStatus PegasusLoader_resolve(PegasusLoader* self) {
	// Sort symbol arrays
	qsort(self->imported_symbols.elems, self->imported_symbols.count, element_size(self->imported_symbols), &symbol_cmp);
	qsort(self->exported_symbols.elems, self->exported_symbols.count, element_size(self->exported_symbols), &symbol_cmp);
	
	// Resolve symbol imports
	foreach(&self->imported_symbols, psym) {
		// Search for a defined symbol with the imported name
		Pegasus_Symbol** psym_found = bsearch(
			psym,
			self->exported_symbols.elems,
			self->exported_symbols.count,
			element_size(self->exported_symbols),
			&symbol_cmp
		);
		
		uint16_t resolved_value = 0;
		if(psym_found != NULL) {
			resolved_value = (*psym_found)->value;
		}
		else {
			bool found = false;
			
			// Try calling the user's symbol resolver function (if provided)
			if(self->resolveSymbol) {
				found = self->resolveSymbol(self->resolveSymbol_cookie, (*psym)->name, &resolved_value);
			}
			
			if(!found) {
				return PEG_UNRESOLVED_IMPORT;
			}
		}
		
		// Assign value to import symbol
		(*psym)->value = resolved_value;
	}
	
	// Apply relocations for each PEGASUS file in load order now that imports have been resolved
	foreach(&self->pegs, ppeg) {
		Pegasus* peg = *ppeg;
		
		foreach(&peg->relocs, reloc) {
			if(reloc->symbol_index >= peg->symbols.count) {
				return PEG_BAD_RELOC;
			}
			
			Pegasus_Symbol* sym = &peg->symbols.elems[reloc->symbol_index];
			if(!Pegasus_seek(peg, reloc->fileoff, SEEK_SET)) {
				return PEG_BAD_RELOC;
			}
			
			if(!Pegasus_write(peg, &sym->value, sizeof(sym->value))) {
				return PEG_BAD_RELOC;
			}
		}
	}
	
	return PEG_SUCCESS;
}


/*!
 * @brief Attempt to map each PEGASUS file's segments into target memory and invoke the entrypoints.
 * 
 * @return PEG_SUCCESS, or an error status code.
 */
PegStatus PegasusLoader_load(PegasusLoader* self) {
	if(!self->mapSegment) {
		return PEG_INVALID_PARAMETER;
	}
	
	// For each PEGASUS file, map its segments into memory and then immediately
	// invoke its entrypoint functions in order, before handling the next PEGASUS file.
	foreach(&self->pegs, ppeg) {
		Pegasus* peg = *ppeg;
		
		// Map the PEGASUS file's segments
		foreach(&peg->segments, pseg) {
			// Seek to the start of the segment data
			if(!Pegasus_seek(peg, pseg->foff, SEEK_SET)) {
				return PEG_TRUNC_SEGMENT_DATA;
			}
			
			// Ensure the segment's file size fits
			if(!Pegasus_seek(peg, pseg->fsize, SEEK_CUR)) {
				return PEG_TRUNC_SEGMENT_DATA;
			}
			
			// Seek BACK to the start of the segment data
			if(!Pegasus_seek(peg, pseg->foff, SEEK_SET)) {
				return PEG_TRUNC_SEGMENT_DATA;
			}
			
			// Map the segment into virtual memory
			if(!self->mapSegment(
				self->mapSegment_cookie,
				pseg->vppn,
				pseg->vpage_count,
				Pegasus_getData(peg),
				pseg->fsize,
				pseg->prot
			)) {
				return PEG_MAP_ERROR;
			}
		}
		
		// Invoke this PEGASUS file's entrypoint functions in order
		foreach(&peg->entrypoints, pentry) {
			Pegasus_Entrypoint* entry = *pentry;
			
			// Call entrypoint function
			if(!self->handleEntry(
				self->handleEntry_cookie,
				entry->pc, entry->dpc,
				entry->a0, entry->a1, entry->a2,
				entry->a3, entry->a4, entry->a5
			)) {
				return PEG_ENTRYPOINT_ERROR;
			}
		}
	}
	
	return PEG_SUCCESS;
}


/*! Identical to calling PegasusLoader_resolve() followed by PegasusLoader_load() */
PegStatus PegasusLoader_resolveAndLoad(PegasusLoader* self) {
	// Resolve imported symbols and apply relocations
	PegStatus s = PegasusLoader_resolve(self);
	if(s != PEG_SUCCESS) {
		return s;
	}
	
	// Map the PEGASUS file's segments into memory and invoke entrypoint functions
	return PegasusLoader_load(self);
}


/*! Destroy a PegasusLoader object previously allocated using PegasusLoader_new() */
void PegasusLoader_destroy(PegasusLoader* self) {
	array_clear(&self->imported_symbols);
	array_clear(&self->exported_symbols);
	
	foreach(&self->pegs, ppeg) {
		Pegasus_destroy(ppeg);
	}
	array_clear(&self->pegs);
}


/*!
 * @brief Attempt to lookup the address of a named exported symbol.
 * 
 * @param name Named symbol to lookup
 * @param out_value Output pointer where the symbol's value will be written on success
 * @return True if the symbol was resolved successfully, or false otherwise
 */
bool PegasusLoader_dlsym(PegasusLoader* self, const char* name, uint16_t* out_value) {
	// Create a fake symbol with the name to search for
	Pegasus_Symbol sym = {0};
	Pegasus_Symbol* psym = &sym;
	
	// This cast is safe as the symbol is only read from in symbol_cmp
	sym.name = (char*)name;
	Pegasus_Symbol** psym_found = bsearch(
		&psym,
		self->exported_symbols.elems,
		self->exported_symbols.count,
		element_size(self->exported_symbols),
		&symbol_cmp
	);
	
	if(psym_found == NULL) {
		return false;
	}
	
	*out_value = (*psym_found)->value;
	return true;
}
