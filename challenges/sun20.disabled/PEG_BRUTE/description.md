**Note that after solving "License Checker", you unlock access to my EAR assembler,
so you might want to solve that one first ;)**

The previous owner of this machine seems to have set a password on it, which is
blocking access to some functionality. Create a PEGASUS cartridge (program) that
can bruteforce this passcode!

-----

Technical notes:

Password characters may be entered by writing a byte to port 1. When the byte
'\n' (0x0a) is written to this port, the password is submitted. After submitting
a password, if you read a byte from port 1, the byte's value will be the number
of characters until the first invalid character. For example, if the password
was "hello" and you submitted "hero\n", then the byte you read from port 1 will
be 0x02.

Writing bytes to port 0 will send them back to your client. So basically stdout
is connected between the client and server but not stdin.

-----

PEG_BRUTE checker plugin:

* [peg_brute_checker.so](https://chal.2020.sunshinectf.org/78b77a31862ce212/peg_brute_checker.so)

How to test:

`runpeg --plugin peg_brute_checker.so <file.peg> [--debug] [--verbose] [--trace]`

How to submit:

`submitpeg --server chal.2020.sunshinectf.org --port 10003 <file>.peg`

Author: kcolley
