//
//  loader.h
//  PegasusEar
//
//  Created by Kevin Colley on 3/6/2020.
//

#ifndef PEG_LOADER_H
#define PEG_LOADER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "common/dynamic_array.h"
#include "pegasus.h"


typedef struct PegasusLoader PegasusLoader;

/*!
 * @brief Callback function pointer used to resolve missing imported symbols
 * 
 * @param resolveSymbol_cookie Opaque value passed directly to the callback
 * @param symbol_name Name of the symbol to be resolved
 * @param out_value Output pointer where the resolved value should be written
 * 
 * @return True if the symbol was resolved successfully, or false otherwise.
 */
typedef bool PegResolveSymbol(
	void* resolveSymbol_cookie,
	const char* symbol_name,
	uint16_t* out_value
);

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
typedef bool PegMapSegment(
	void* mapSegment_cookie,
	uint8_t vppn,
	uint8_t vpage_size,
	const void* segmentData,
	uint16_t segmentSize,
	uint8_t prot
);

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
typedef bool PegHandleEntry(
	void* handleEntry_cookie,
	uint16_t pc,
	uint16_t dpc,
	uint16_t arg1,
	uint16_t arg2,
	uint16_t arg3,
	uint16_t arg4,
	uint16_t arg5,
	uint16_t arg6
);


/*! Effectively opaque object containing state for loading PEGASUS files */
struct PegasusLoader {
	/// Array of parsed PEGASUS files
	dynamic_array(Pegasus*) pegs;
	
	/// Array sorted by name containing symbols that have definitions
	dynamic_array(Pegasus_Symbol*) exported_symbols;
	
	/// Array sorted by name containing symbols that need definitions
	dynamic_array(Pegasus_Symbol*) imported_symbols;
	
	/// Callback function pointer used to resolve missing symbol imports
	PegResolveSymbol* resolveSymbol;
	void* resolveSymbol_cookie;
	
	/// Callback function pointer used to map segments into target memory
	PegMapSegment* mapSegment;
	void* mapSegment_cookie;
	
	/// Callback function pointer used to invoke entrypoints
	PegHandleEntry* handleEntry;
	void* handleEntry_cookie;
};


/*! Initialize a PegasusLoader object with default values */
void PegasusLoader_init(PegasusLoader* self);

/*!
 * @brief Sets the callback function that resolves unresolved symbol imports
 * 
 * @param resolveSymbol Callback function pointer used to resolve missing symbol imports
 * @param resolveSymbol_cookie Opaque value passed directly to the callback
 */
void PegasusLoader_setSymbolResolver(PegasusLoader* self, PegResolveSymbol* resolveSymbol, void* resolveSymbol_cookie);

/*! Returns true when a symbol resolver callback is currently registered */
bool PegasusLoader_hasSymbolResolver(PegasusLoader* self);

/*!
 * @brief Sets the callback function that maps segments into target memory
 * 
 * @param mapSegment Callback function pointer used to map segments into target memory
 * @param mapSegment_cookie Opaque value passed directly to the callback
 */
void PegasusLoader_setSegmentMapper(PegasusLoader* self, PegMapSegment* mapSegment, void* mapSegment_cookie);

/*! Returns true when a segment maper callback is currently registered */
bool PegasusLoader_hasSegmentMapper(PegasusLoader* self);

/*!
 * @brief Sets the callback function that invokes entrypoints while loading a PEGASUS file
 * 
 * @param handleEntry Callback function pointer used to invoke entrypoints
 * @param handleEntry_cookie Opaque value passed directly to the callback
 */
void PegasusLoader_setEntrypointHandler(PegasusLoader* self, PegHandleEntry* handleEntry, void* handleEntry_cookie);

/*! Returns true when an entrypoint handler callback is currently registered */
bool PegasusLoader_hasEntrypointHandler(PegasusLoader* self);

/*!
 * @brief Add a parsed PEGASUS file object to the loader object
 * 
 * @param peg Parsed PEGASUS file object
 */
void PegasusLoader_add(PegasusLoader* self, Pegasus* peg);

/*!
 * @brief Attempt to resolve all imported symbols using either symbols defined by other added PEGASUS
 * files or with the user provided symbol resolver function. This also applies relocations.
 * 
 * @return PEG_SUCCESS, or an error status code.
 */
PegStatus PegasusLoader_resolve(PegasusLoader* self);

/*!
 * @brief Attempt to map each PEGASUS file's segments into target memory and invoke the entrypoints.
 * 
 * @return PEG_SUCCESS, or an error status code.
 */
PegStatus PegasusLoader_load(PegasusLoader* self);

/*! Identical to calling PegasusLoader_resolve() followed by PegasusLoader_load() */
PegStatus PegasusLoader_resolveAndLoad(PegasusLoader* self);

/*! Destroy a PegasusLoader object previously allocated using PegasusLoader_new() */
void PegasusLoader_destroy(PegasusLoader* self);

/*!
 * @brief Attempt to lookup the address of a named exported symbol.
 * 
 * @param name Named symbol to lookup
 * @param out_value Output pointer where the symbol's value will be written on success
 * @return True if the symbol was resolved successfully, or false otherwise
 */
bool PegasusLoader_dlsym(PegasusLoader* self, const char* name, uint16_t* out_value);

#endif /* PEG_LOADER_H */
