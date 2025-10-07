import os

def debug_print(*args, **kwargs):
	if os.environ.get("EARASM_DEBUG", "0") != "0":
		print(*args, **kwargs)
