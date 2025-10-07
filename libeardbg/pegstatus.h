//
//  pegstatus.h
//  PegasusEar
//
//  Created by Kevin Colley on 3/6/2020.
//


#ifndef PEG_STATUS_H
#define PEG_STATUS_H

//! Error status codes used while parsing and loading PEGASUS files
typedef enum PegStatus {
	PEG_SUCCESS = 0,         //!< No issue while parsing/loading PEGASUS file
	PEG_INVALID_PARAMETER,   //!< One of the PEGASUS API functions was invoked incorrectly
	PEG_IO_ERROR,            //!< Error while opening PEGASUS file from disk
	PEG_TRUNC_HEADER,        //!< EOF while parsing PEGASUS header
	PEG_BAD_MAGIC,           //!< Incorrect magic value in PEGASUS header
	PEG_TRUNC_CMD_HEADER,    //!< EOF while parsing a load command header
	PEG_TRUNC_SEGMENT,       //!< EOF while parsing a segment command
	PEG_TRUNC_SYMTAB,        //!< EOF while parsing a symbol table command
	PEG_TRUNC_RELTAB,        //!< EOF while parsing a relocation table command
	PEG_TRUNC_ENTRYPOINT,    //!< EOF while parsing an entrypoint command
	PEG_TRUNC_SEGMENT_NAME,  //!< EOF while parsing a segment's name
	PEG_TRUNC_SYMBOL_NAME,   //!< EOF while parsing a symbol's name
	PEG_MULTIPLE_SYMTABS,    //!< More than one symbol table command encountered
	PEG_MULTIPLE_RELTABS,    //!< More than one relocation table command encountered
	PEG_BAD_CMD,             //!< Unknown load command kind
	PEG_UNRESOLVED_IMPORT,   //!< Unable to resolve an imported symbol
	PEG_BAD_RELOC,           //!< Failed to apply a relocation
	PEG_TRUNC_SEGMENT_DATA,  //!< EOF while loading segment data
	PEG_MAP_ERROR,           //!< Failed to map segment from PEGASUS file into target memory
	PEG_ENTRYPOINT_ERROR,    //!< Error while invoking entrypoint function in PEGASUS file
} PegStatus;


/*!
 * @brief Get a string description for a PegStatus error code
 * 
 * @param s PegStatus error code
 * 
 * @return String constant that describes the PegStatus error code
 */
static inline const char* PegStatus_toString(PegStatus s) {
	switch(s) {
		case PEG_SUCCESS:
			return "PEGASUccesS!";
		
		case PEG_INVALID_PARAMETER:
			return "A parameter passed to a PEGASUS API function is invalid";
		
		case PEG_IO_ERROR:
			return "Failed to read PEGASUS file";
		
		case PEG_TRUNC_HEADER:
			return "Hit EOF in PEGASUS file while reading header";
		
		case PEG_BAD_MAGIC:
			return "PEGASUS file's magic value is incorrect";
		
		case PEG_TRUNC_CMD_HEADER:
			return "Hit EOF in PEGASUS file while reading load command header";
		
		case PEG_TRUNC_SEGMENT:
			return "Hit EOF in PEGASUS file while reading segment command";
		
		case PEG_TRUNC_SYMTAB:
			return "Hit EOF in PEGASUS file while reading symbol table command";
		
		case PEG_TRUNC_RELTAB:
			return "Hit EOF in PEGASUS file while reading relocation table command";
		
		case PEG_TRUNC_ENTRYPOINT:
			return "Hit EOF in PEGASUS file while reading entrypoint command";
		
		case PEG_TRUNC_SEGMENT_NAME:
			return "Hit EOF in PEGASUS file while reading segment name";
		
		case PEG_TRUNC_SYMBOL_NAME:
			return "Hit EOF in PEGASUS file while reading symbol name";
		
		case PEG_MULTIPLE_SYMTABS:
			return "More than one symbol table encountered in PEGASUS file";
		
		case PEG_MULTIPLE_RELTABS:
			return "More than one relocation table encountered in PEGASUS file";
		
		case PEG_BAD_CMD:
			return "Invalid PEGASUS load command type";
		
		case PEG_UNRESOLVED_IMPORT:
			return "Unable to resolve an imported symbol in PEGASUS file";
		
		case PEG_BAD_RELOC:
			return "Failed to apply a relocation in PEGASUS file";
		
		case PEG_TRUNC_SEGMENT_DATA:
			return "Hit EOF in PEGASUS file while loading segment data";
		
		case PEG_MAP_ERROR:
			return "Failed to map segment from PEGASUS file into target memory";
		
		case PEG_ENTRYPOINT_ERROR:
			return "Error while invoking entrypoint function in PEGASUS file";
		
		
		default:
			return "Unknown error while loading PEGASUS file";
	}
}

#endif /* PEG_STATUS_H */
