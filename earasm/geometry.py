import struct

try:
	b"lol".hex()
except AttributeError:
	def bytes_hex(bs: bytes) -> str:
		return bs.encode("hex")
else:
	def bytes_hex(bs: bytes) -> str:
		return bs.hex()

if len(bytes([1])) == 1:
	def list2bytes(l: list) -> bytes:
		return bytes(l)
else:
	def list2bytes(l: list) -> bytes:
		return "".join(x if isinstance(x, str) else chr(x) for x in l)

PAGE_SIZE = 0x100
PAGE_MASK = 0xFF

def page_floor(addr: int) -> int:
	return addr & ~PAGE_MASK

def page_ceil(addr: int) -> int:
	return page_floor(addr + PAGE_SIZE - 1)

def p8(x: int) -> bytes:
	if x >> 8 not in [-1, 0]:
		raise ValueError(f"Value {x} doesn't fit in 8 bits")
	return struct.pack("<B", x & 0xff)

def p16(x: int) -> bytes:
	if x >> 16 not in [-1, 0]:
		raise ValueError(f"Value {x} doesn't fit in 16 bits")
	return struct.pack("<H", x & 0xffff)

def p32(x: int) -> bytes:
	if x >> 32 not in [-1, 0]:
		raise ValueError(f"Value {x} doesn't fit in 32 bits")
	return struct.pack("<I", x & 0xffffffff)


def h8(x: int) -> str:
	return bytes_hex(p8(x))

def h16(x: int) -> str:
	return bytes_hex(p16(x))

def h32(x: int) -> str:
	return bytes_hex(p32(x))


def pack_lestring(s: str | bytes) -> bytes:
	if not s:
		return b"\x00"
	if isinstance(s, str):
		s = [ord(c) for c in s]
	return list2bytes(map(lambda c: c | 0x80, s))[:-1] + list2bytes(s[-1:])

def pack_sleb128(x: int) -> bytes:
	# Zero isn't a valid sleb128 value
	assert x != 0
	
	sleb_reversed = []
	
	# Split x into sign and magnitude
	sign = 0
	target = 0
	if x < 0:
		sign = 0x40
		target = -1
	else:
		x -= 1
	
	# Encode each byte to hold the next 7 bits
	while x != target:
		sleb_reversed.append(0x80 | (x & 0x7f))
		x >>= 7
	
	# Do we need an extra byte for the sign?
	if (sleb_reversed[-1] & 0x40) != sign:
		sleb_reversed.append(0x80 | sign)
	else:
		sleb_reversed[-1] |= sign
	
	# Turn off continuation bit in last byte
	sleb_reversed[0] &= ~0x80
	
	# Reverse and pack as string of bytes
	return b"".join(sleb_reversed[::-1])
