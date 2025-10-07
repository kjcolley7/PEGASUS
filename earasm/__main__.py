import argparse
import sys
import json
from typing import TextIO
from .driver import assemble
from .layout import DEFAULT_LAYOUT


def main():
	parser = argparse.ArgumentParser(
		prog="earasm",
		description="Assemble EAR assembly source files into a PEGASUS binary."
	)
	
	parser.add_argument(
		"inputs", metavar="input.ear",
		nargs="+",
		help="Input EAR assembly source file(s)"
	)
	parser.add_argument(
		"-o", "--output", metavar="<output.peg>",
		default="-",
		help="Output PEGASUS binary file"
	)
	parser.add_argument(
		"-I", "--search", metavar="<search-dir>",
		action="append", dest="search_paths", default=[],
		help="Add a directory to the assembler's search path (for .import directives)"
	)
	parser.add_argument(
		"--layout", metavar="<layout.json>",
		help="JSON layout file for defining segments and entrypoints"
	)
	parser.add_argument(
		"-s", "--flat-segment", metavar="<segment>",
		action="append", dest="flat_segnames", default=[],
		help="Output a flat binary with the specified segments concatenated (no headers)"
	)
	parser.add_argument(
		"--dump-symbols", metavar="<symbols.txt>",
		nargs="?", type=argparse.FileType("w"), const="-",
		help="Dump symbol table to specified file"
	)
	args = parser.parse_args()
	
	if args.output == sys.stdout.buffer and args.dump_symbols == sys.stdout:
		parser.error("Cannot write both output binary and symbol table to stdout")
	
	layout = DEFAULT_LAYOUT
	if args.layout:
		with open(args.layout, "r") as layout_fp:
			layout = json.load(layout_fp)
	
	asm_strs: list[tuple[str, str]] = []
	for filename in args.inputs:
		if filename == "-":
			filename = None
			asmstr = sys.stdin.read()
		else:
			with open(filename, "r") as fp:
				asmstr = fp.read()
		
		# Collect assembly pieces into their segments
		asm_strs.append((filename, asmstr))
	
	data = assemble(
		asm_strs,
		layout=layout,
		search_paths=args.search_paths,
		flat_segnames=args.flat_segnames,
		dump_symbols=args.dump_symbols
	)
	
	if args.dump_symbols and args.dump_symbols != sys.stdout:
		args.dump_symbols.close()
	
	# Write the assembled binary output file
	if args.output == "-":
		sys.stdout.buffer.write(data)
	else:
		with open(args.output, "wb") as out_fp:
			out_fp.write(data)

if __name__ == "__main__":
	main()
