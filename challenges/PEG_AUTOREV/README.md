# Deployment Notes

This challenge starts with a `socat` listener that runs the [serve.py](serve.py)
script. This script builds a random grid in the style of the NoPixel casino
generator hack puzzle. That grid is guaranteed to have at least one correct path
through it. The logic for generating the grid is in [grid.py](grid.py).

> Note: I probably should have generated the grid in a way such that it only
> has **exactly** one correct path, to make bruteforce solutions less viable.

This grid is converted into a PEGASUS program in [asm_grid.py](asm_grid.py). It
works by emitting a chunk of code for each cell in the grid. These code chunks
are identical, except a few values are filled in. The code of each block comes
from the template at [block_template.ear](block_template.ear). Each code block
reads in one character from stdin. If the character is a '0', it jumps to the
block to the right by the cell's value number of squares. If it's a '1', it
jumps down that number of squares. If it's a newline, it skips the newline and
jumps back to the beginning of the code block. Only when the program gets to
the bottom-right grid cell does it count as success. Success is marked by
writing a value (101) to port 0xE (for exit code). Any time that the program
would jump outside the bounds of the grid, it simply halts, marking failure.

Everything is tied together back in [serve.py](serve.py). After the assembly
code is generated, it is passed to the assembler as an in-memory string. This
generates a PEGASUS program, which is stored as an in-memory byte-string. Then,
the PEGASUS program's bytes are written to a file descriptor created with
`memfd_create(2)`. This file descriptor is created using the `os.memfd_create()`
Python function, which is only available as of Python 3.8+. For this reason, the
Dockerfile uses Ubuntu 20.04 as the base image, as that's the earliest version
of Ubuntu to have Python 3.8+ and still be supported. Once the PEGASUS program
is written to that in-memory file descriptor, `serve.py` goes on to execute it
using `runpeg` with the `peg_autorev_checker.so` plugin. The PEGASUS program
is specified as `/proc/self/fd/<peg_fd>`, where `<peg_fd>` is the file
descriptor provided by `memfd_create`. All of this is done to avoid using any
writeable files on the filesystem, such as temporary files. It just seemed like
it would be more reliable this way and less prone to failure. Also, avoiding
files on disk allows the container to run fully read-only.

When the `runpeg` process exits, `serve.py` inspects the return status code.
If it matches the special value of 101, then it prints the next chunk of the
flag. Otherwise, it prints a failure message and exits. The player must solve
5 increasingly difficult levels to get all 5 chunks of the flag. Also, each
randomly-generated level is generated based on a seed. This means that in case
a bug in the level generation code is discovered, I could easily reproduce the
buggy case by using the seed (which is shown to players just in case). It was
never actually needed during the live CTF, however. One final note is that
each level has its own time limit (of 30 seconds), and there is an overal time
limit for the entire connection (which gives a bit of extra time beyond 30
seconds per level as some leeway).
