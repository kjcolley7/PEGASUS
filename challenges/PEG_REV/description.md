One of the PEGASUS cartridges seems to contain an EAR assembler. Booting it up
presents you with a license check screen, where you need to enter an email
address and password. See if you can generate a valid email & password combo
so you can gain access to an EAR assembler!

=====

Note:

This PEGASUS program is running unmodified on the server and is running using
this exact checker plugin. The checker plugin effectively just connects the EAR
core's port 0 (both read and write) to stdin/stdout, and it also allows the
PEGASUS program to read the flag from port 0xF (F for flag, get it?).

* [LicenseChecker.peg](https://chal.2020.sunshinectf.org/1661c1952305b6f6/LicenseChecker.peg)
* [peg_rev_checker.so](https://chal.2020.sunshinectf.org/1661c1952305b6f6/peg_rev_checker.so)

How to test:

`runpeg --plugin peg_rev_checker.so LicenseChecker.peg [--debug] [--verbose] [--trace]`

How to connect to server:

`nc chal.2020.sunshinectf.org 10001`

Author: kcolley
