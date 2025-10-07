#!/usr/bin/env python3

"""
This module calls into grid.py to generate a numbers grid puzzle. It then
generates PEGASUS assembly instructions that turn that grid of numbers into
a program. The program takes input from the user:

* "0": Go right
* "1": Go down
* "\n": Ignored for ease of playing the game interactively

Each grid square is generated as a block of code that reads one of these input
characters from the user, and then hops N squares either right or down, based
on the input choice. Here, N is the integral value of that grid square. The
code block also does bounds checking. If it tries to hop beyond the edge of the
grid, then it halts the program. When the code block for the bottom-right grid
square is executed, it prints out the flag.

Maybe when a grid puzzle is solved it should only print part of the flag? It
could be done in 4-10 "chunks", or maybe even one puzzle solve prints one
character from the flag. The puzzles could get harder as they go too. Maybe it
starts with 7x7 and values 1-4, but by the end it could even be 20x20 with
values 1-12!
"""

import os.path
import sys
from grid import generate_grid

_SCRIPT_DIR = os.path.dirname(__file__)
_TEMPLATE_FILE_NAME = "block_template.ear"

_ASM_PREFIX = """
.scope
.export @main
@main:
	MOV     DPC, ({WIDTH} * {HEIGHT}) - 1
	//fallthrough into @block_0_0

.scope
.export @grid
@grid:

"""


'''
void give_flag(void) {
	char c;
	while(rdb(FLAG_PORT, &c)) {
		wrb(c);
	}
	exit(0);
}

@.next_char:
	RDB     R1, (0xF)
	WRBN.CC R1
	BRR.CC  @.next_char
'''

_ASM_WIN = """
.scope
.loc @grid + ({WIDTH} * {HEIGHT}) - 1, ({WIDTH} * {HEIGHT}) - 1
.export @block_win
@block_win:
	// Exit with code 101 (used in serve.py to check for success)
	WRB     (0xE), 101
	HLT
"""

with open(os.path.join(_SCRIPT_DIR, _TEMPLATE_FILE_NAME)) as _fp:
	_ASM_BLOCK = _fp.read()


class AsmGrid:
	def __init__(self, width, height, max_value):
		self.width = width
		self.height = height
		self.max_value = max_value
	
	def _asm_block(self, x, y, value):
		fills = {
			"X_POS": x,
			"Y_POS": y,
			"VALUE": value,
			"WIDTH": self.width,
			"HEIGHT": self.height
		}
		
		# Bottom-right grid square is the goal
		if x == self.width - 1 and y == self.height - 1:
			return _ASM_WIN.format(**fills)
		
		if value < 1 or self.max_value < value:
			raise ValueError(
				"Grid square value at (%u, %u) is out of range [1, %u]: %d" % (
					x, y, self.max_value, value
				)
			)
		
		return _ASM_BLOCK.format(**fills)
	
	def _build_asm(self, grid):
		blocks = []
		
		for y, row in enumerate(grid):
			for x, value in enumerate(row):
				blocks.append(self._asm_block(x, y, value))
		
		fills = {
			"WIDTH": self.width,
			"HEIGHT": self.height
		}
		return _ASM_PREFIX.format(**fills) + "\n\n".join(blocks)
	
	def generate(self, seed=None):
		grid, _path, _inputs = generate_grid(self.width, self.height, self.max_value, seed)
		asm = self._build_asm(grid)
		return asm


def asm_grid(width, height, max_value, seed=None):
	builder = AsmGrid(width, height, max_value)
	return builder.generate(seed)


def main(argc, argv):
	for opt in ("-h", "-?", "--help"):
		if opt in argv:
			print("Usage: %s [WIDTH [HEIGHT [MAX_VALUE [SEED]]]]" % argv[0])
			return
	
	for i in range(1, 5):
		if i == 1:
			width = int(argv[i]) if i < argc else 7
		elif i == 2:
			height = int(argv[i]) if i < argc else width
		elif i == 3:
			max_value = int(argv[i]) if i < argc else 4
		elif i == 4:
			seed = argv[i] if i < argc else None
	
	asm = asm_grid(width, height, max_value, seed)
	print(asm)


if __name__ == "__main__":
	main(len(sys.argv), sys.argv)
