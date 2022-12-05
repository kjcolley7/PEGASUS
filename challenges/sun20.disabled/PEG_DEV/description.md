**Note that after solving "License Checker", you unlock access to my EAR assembler,
so you might want to solve that one first ;)**

It's time to see if you can create your own PEGASUS file containing EAR code!
For this challenge, no entrypoint is needed, but any entrypoints present will
be invoked before the challenge functions are called. You must implement the
following functions:

```c
void uadd32_write(uint32_t a, uint32_b);
void win(lestring* flag);
```

The `uadd32_write` function takes in two unsigned 32-bit integers as arguments,
broken down into registers as follows:

```c
R2 = LO16(a);
R3 = HI16(a);
R4 = LO16(b);
R5 = HI16(b);
```

Where LO16() means the lower 16 bits and HI16 means the upper 16 bits. For example,
`uadd32_write(0x11223344, 0xABCDEF00)` would be called with the following register
values:

```c
R2 = 0x3344;
R3 = 0x1122;
R4 = 0xEF00;
R5 = 0xABCD;
```

You must implement unsigned 32-bit addition, then write the sum as a hex string,
one byte at a time, to port 1. The following C code implements this:

```c
void uadd32_write(uint32_t a, uint32_t b) {
	fprintf(port_1, "0x%08X\n", a + b);
}
```

Both of these functions must be exported symbols from your PEGASUS file, and they
may be called multiple times over the course of a run. The exact server-side code
used to check submitted PEGASUS programs is provided as the PEG_DEV checker plugin.

-----

PEG_DEV checker plugin:

* [peg_dev_checker.so](https://chal.2020.sunshinectf.org/f21ed9821a3f49b9/peg_dev_checker.so)

How to test:

`runpeg --plugin peg_dev_checker.so <file.peg> [--debug] [--verbose] [--trace]`

How to submit:

`submitpeg --server chal.2020.sunshinectf.org --port 10002 <file>.peg`

Author: kcolley
