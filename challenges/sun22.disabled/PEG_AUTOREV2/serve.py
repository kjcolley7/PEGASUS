#!/usr/bin/env python3
import os
import sys
import signal
from random import SystemRandom
from binascii import hexlify
import subprocess

from asm_grid import asm_grid
from earasm.driver import assemble


TIME_PER_LEVEL = 30


# 102 elements
_seed_parts = (
	"wa", "ra",  "ya", "ma",  "pa",  "ba",  "ha",  "na",  "da", "ta",  "za", "sa",  "ga",  "ka", "a",
	      "ri",        "mi",  "pi",  "bi",  "hi",  "ni",        "chi", "ji", "shi", "gi",  "ki", "i",
	      "ru",  "yu", "mu",  "pu",  "bu",  "hu",  "nu",        "tsu", "zu", "su",  "gu",  "ku", "u",
	      "re",        "me",  "pe",  "be",  "he",  "ne",  "de", "te",  "ze", "se",  "ge",  "ke", "e",
	"wo", "ro",  "yo", "mo",  "po",  "bo",  "ho",  "no",  "do", "to",  "zo", "so",  "go",  "ko", "o",
	      "rya",       "mya", "pya", "bya", "hya", "nya",       "cha", "ja", "sha", "gya", "kya",
	      "ryu",       "myu", "pyu", "byu", "hyu", "nyu",       "chu", "ju", "shu", "gyu", "kyu",
	      "ryo",       "myo", "pyo", "byo", "hyo", "nyo",       "cho", "jo", "sho", "gyo", "kyo",
	                                               "n"
)
_seed_part_count = None


# A value of 64 sets _seed_part_count to 10
SEED_MIN_ENTROPY_BITS = 64

def generate_random_seed():
	global _seed_part_count
	if not _seed_part_count:
		possibilities = len(_seed_parts)
		part_count = 1
		while possibilities < (1 << SEED_MIN_ENTROPY_BITS):
			possibilities *= len(_seed_parts)
			part_count += 1
		
		_seed_part_count = part_count
	
	true_rng = SystemRandom()
	
	parts = []
	for i in range(_seed_part_count):
		parts.append(true_rng.choice(_seed_parts))
	
	return "".join(parts[:len(parts)//2]) + "_" + "".join(parts[len(parts)//2:])


# (width, height, max value per cell)
_difficulty_sizes = (
	(7, 7, 4),
	(8, 8, 5),
	(10, 10, 5),
	(12, 12, 6),
	(16, 40, 10),
)

def main():
	tl = os.getenv("TIMELIMIT")
	if tl:
		signal.alarm(int(tl))
	
	# pw = os.getenv("PASSWORD")
	# if pw:
	# 	sys.stdout.write("Password: ")
	# 	sys.stdout.flush()
		
	# 	pwin = input()
	# 	if pwin != pw:
	# 		print("Incorrect password")
	# 		return
	
	try:
		with open("/ctf/flag.txt") as fp:
			flag = fp.read().strip()
	except FileNotFoundError:
		print("Missing flag.txt file!")
		return
	
	flag_parts = []
	flag_n = len(flag)
	flag_d = len(_difficulty_sizes)
	flag_x = (flag_n + flag_d - 1) // flag_d
	
	for i in range(flag_d):
		flag_parts.append(flag[i * flag_x:(i+1) * flag_x])
	
	for i, params in enumerate(_difficulty_sizes):
		seed = generate_random_seed()
		
		# generate maze
		asm = asm_grid(params[0], params[1], params[2], seed)
		
		# assemble to peg bytes
		peg = assemble(asm)
		
		# send peg contents
		print("||| Challenge level %d/%d |||" % (i + 1, flag_d + 1))
		print("You have %u seconds to solve this level." % (TIME_PER_LEVEL,))
		print("Here comes a PEGASUS program (reference id: %s) on the next line. Size: %u bytes (encoded as hex)." % (seed, len(peg),))
		print(peg.hex())
		
		peg_fd = os.memfd_create("%s.peg" % (seed,))
		
		try:
			os.set_inheritable(peg_fd, True)
			bytes_written = os.write(peg_fd, peg)
			
			assert bytes_written == len(peg)
			
			os.lseek(peg_fd, 0, os.SEEK_SET)
			
			# print("DEBUG: wrote %d bytes to memfd file" % bytes_written)
			
			# run PEGASUS
			print("Running now...")
			p = subprocess.run(
				[
					"/usr/local/bin/runpeg",
					"--plugin", "peg_autorev_checker.so",
					"/proc/self/fd/%d" % (peg_fd,)
				],
				pass_fds=(peg_fd,),
				timeout=TIME_PER_LEVEL
			)
			
			# check if worked (101 is defined in asm_grid.py as the exit code for success)
			if p.returncode != 101:
				print("Sorry, you failed :(")
				# print(repr(p))
				return
			
			# print flag part
			print("Success! Here's part %d of the flag: '%s'" % (i + 1, flag_parts[i]))
			print("The next level will be harder...")
		finally:
			os.close(peg_fd)


if __name__ == "__main__":
	main()
