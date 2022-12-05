import sys
from .driver import assemble


def main():
	if len(sys.argv) < 3:
		print("Usage: %s input1.ear [ input2.ear ... ] output.peg" % sys.argv[0])
		sys.exit(1)
	
	asm_strs = []
	
	for arg in sys.argv[1:-1]:
		# Read input assembly source from stdin or file
		if arg == "-":
			asmstr = sys.stdin.read()
		else:
			with open(arg, "r") as asm_fp:
				asmstr = asm_fp.read()
		
		# Collect assembly pieces into their segments
		asm_strs.append(asmstr)
	
	data = assemble(asm_strs)
	
	# Write the assembled binary output file
	out_fname = sys.argv[-1]
	if out_fname == "-":
		sys.stdout.buffer.write(data)
	else:
		with open(out_fname, "wb") as out_fp:
			out_fp.write(data)

if __name__ == "__main__":
	main()
