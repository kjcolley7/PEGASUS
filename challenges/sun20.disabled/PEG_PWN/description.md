**IMPORTANT** Re-download the latest version of [libpegasus_ear.so](https://chal.2020.sunshinectf.org/f678155e6d04f694/libpegasus_ear.so)!

-----

While scanning the internet, you found what appears to be a service running on
a PEGASUS machine. It has a login screen, but it seems nonfunctional. See if
you can break in!

-----

Note:

This PEGASUS program is running unmodified on the server and is running using
this exact checker plugin. The checker plugin connects stdin to port 0 and all
ports to stdout. Reading from port 0xF (F for flag, get it?) will return bytes
from the flag, one at a time.

* [bof.peg](https://chal.2020.sunshinectf.org/91ff9eda4bc59829/bof.peg)
* [peg_pwn_checker.so](https://chal.2020.sunshinectf.org/91ff9eda4bc59829/peg_pwn_checker.so)

How to test:

`runpeg --plugin peg_pwn_checker.so bof.peg [--debug] [--verbose] [--trace]`

How to connect to server:

`nc chal.2020.sunshinectf.org 10004`

Author: kcolley
