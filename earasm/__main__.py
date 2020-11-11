import sys
from .layout import LAYOUT, ENTRYPOINTS
from .assembler import *
from .linker import *


def main():
	if len(sys.argv) < 3:
		print("Usage: %s input1.ear [ input2.ear ... ] output.peg" % sys.argv[0])
		sys.exit(1)
	
	# Initial empty context
	asm = Assembler(LAYOUT)
	
	for arg in sys.argv[1:-1]:
		# Read input assembly source file
		with open(arg, "r") as asm_fp:
			asmstr = asm_fp.read()
		
		# Collect assembly pieces into their segments
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
	data = linker.link_binary()
	
	# Write the assembled binary output file
	with open(sys.argv[-1], "wb") as out_fp:
		out_fp.write(data)

if __name__ == "__main__":
	main()
