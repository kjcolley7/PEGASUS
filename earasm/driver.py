from .layout import LAYOUT, ENTRYPOINTS
from .assembler import *
from .linker import *

def assemble(asm_strs):
	# Initial empty context
	asm = Assembler(LAYOUT)
	
	# Consume all assembly strings
	if isinstance(asm_strs, str):
		asm.add_input(asm_strs)
	else:
		for asmstr in asm_strs:
			asm.add_input(asmstr)
	
	# Assemble all segments
	segments = asm.assemble()
	
	# Resolve entrypoint labels
	entrypoints = []
	for symname in ENTRYPOINTS:
		try:
			entrypoints.append(asm.resolve(symname[1:]))
		except NameError:
			pass
	
	# Link segments together into output file
	linker = Linker(LAYOUT)
	
	# Add segments to linker
	for segname, vmaddr, segdata in segments:
		linker.add_segment(segname, vmaddr, segdata)
	
	# Add entrypoints to linker
	for entry in entrypoints:
		linker.add_entrypoint(entry.value)
	
	# Add exported labels to the symbol table
	for name, value in asm.get_exports():
		linker.add_symbol(name, value)
	
	# Get fully linked output file contents
	return linker.link_binary()
