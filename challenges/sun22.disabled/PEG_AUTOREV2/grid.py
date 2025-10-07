#!/usr/bin/env python3

"""
Generate number grids where each grid square has an integral value. The value
is the number of squares, either right or down, that you must hop to get to the
next grid square. The goal is to start from the top-left and pick a path that
eventually lands on the bottom-right square. This is directly inspired by the
casino generators "hack" from the NoPixel GTA V server.
"""

import random
import sys
from pprint import pprint

# def example_grid():
# 	return (
# 		(1, 1, 3, 1, 3, 3, 3),
# 		(4, 1, 3, 3, 1, 2, 3),
# 		(4, 3, 1, 3, 3, 2, 4),
# 		(1, 4, 2, 3, 4, 2, 4),
# 		(3, 4, 1, 1, 1, 3, 4),
# 		(4, 3, 2, 1, 4, 1, 2),
# 		(2, 2, 1, 4, 4, 1, 0),
# 	)


def generate_grid(width, height, max_value, seed=None):
	prng = random.Random(seed)
	
	grid = []
	for i in range(height):
		grid.append([0] * width)
	
	# Create one valid path
	path = []
	inputs = []
	x, y = 0, 0
	while (x, y) != (width - 1, height - 1):
		if x == width - 1:
			is_down = True
		elif y == height - 1:
			is_down = False
		else:
			is_down = bool(prng.randint(0, 1))
		
		if is_down:
			dist_until_last = (height - 1) - y
		else:
			dist_until_last = (width - 1) - x
		
		value = prng.randint(1, min(max_value, dist_until_last))
		grid[y][x] = value
		path.append((x, y))
		inputs.append(is_down)
		
		if is_down:
			y += value
		else:
			x += value
	
	# Fill the rest of the squares with random values
	for y in range(height):
		for x in range(width):
			if not grid[y][x]:
				grid[y][x] = prng.randint(1, max_value)
	
	# Make the bottom-right square zero
	grid[height - 1][width - 1] = 0
	
	return grid, path, inputs


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
	
	grid, path, inputs = generate_grid(width, height, max_value, seed)
	
	pprint(grid)
	pprint(path)
	print("Inputs: " + "".join("01"[inp] for inp in inputs))

if __name__ == "__main__":
	main(len(sys.argv), sys.argv)
