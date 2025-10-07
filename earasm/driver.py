from .assembler import *
from .linker import *
from typing import Any, Optional, TextIO

def assemble(
		asm_strs: str | list[tuple[str, str]],
		layout: dict[str, Any],
		search_paths: list[str]=[],
		flat_segnames: list[str]=[],
		dump_symbols: Optional[TextIO]=None,
) -> bytes:
	asm = Assembler(
		layout=layout,
		search_paths=search_paths,
		dump_symbols=dump_symbols
	)
	
	# Consume all assembly strings
	if isinstance(asm_strs, str):
		asm.add_input(asm_strs)
	else:
		for filename, asmstr in asm_strs:
			asm.add_input(asmstr, filename)
	
	# Need size of header to know how many pages the @PEG segment needs.
	# This is because the @TEXT segment starts right after the @PEG segment.
	for seg in asm.segments:
		if seg.is_header:
			fakelinker = Linker(layout)
			
			# Need symbols to compute symbol table size (part of header)
			for name in asm.get_export_names():
				fakelinker.add_symbol(name, 0)
			
			peg_header_size = fakelinker.precompute_header_size()
			seg.vmsize = page_ceil(peg_header_size)
			break
	
	# Assemble all segments
	segments = asm.assemble()
	
	# Handle simpler flat file format (raw binary, no data segment)
	if flat_segnames:
		for i, segname in enumerate(flat_segnames):
			if not segname.startswith("@"):
				flat_segnames[i] = "@" + segname
		
		segmap = {name: data.ljust(vmsize, b"\0") for (name, _addr, vmsize, data) in segments}
		
		flat = b""
		for segname in flat_segnames:
			if segname not in segmap:
				raise KeyError(f"Missing segment: {segname}")
			
			if len(flat) % PAGE_SIZE != 0:
				# Pad to page size
				flat += b"\x00" * (PAGE_SIZE - len(flat) % PAGE_SIZE)
			flat += segmap[segname]
		
		return flat
	
	# Resolve entrypoint labels
	entrypoints: list[Label] = []
	for symname in layout.get("entrypoints", []):
		try:
			entrypoints.append(asm.resolve(symname))
		except NameError:
			pass
	
	# Link segments together into output file
	linker = Linker(layout)
	
	# Add segments to linker
	for segname, vmaddr, vmsize, segdata in segments:
		linker.add_segment(segname, vmaddr, vmsize, segdata)
	
	# Add entrypoints to linker
	for entry in entrypoints:
		linker.add_entrypoint(PC=entry.value, DPC=entry.calldpc)
	
	# Add exported labels to the symbol table
	for name, value in asm.get_exports():
		linker.add_symbol(name, value)
	
	# Get fully linked output file contents
	return linker.link_binary()
