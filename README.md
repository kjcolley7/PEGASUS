PegasusEar
-----

Custom CPU architecture and binary file format, initially created for SunshineCTF 2020.


## Useful Documentation

* [EAR Extended Architecture Reference](docs/EAR_EAR.md)
* [PEGASUS File Format Reference](docs/PEGASUS.md)


## Project Structure

* [challenges](challenges): Source code for the various CTF challenges that have been built on top of the PEGASUS platform.
* [client](client): Command line program to upload a PEGASUS file to a server and print the result. Product: `submitpeg`
* [common](common): Defines macros and other common support functionality used by multiple subprojects.
* [docs](docs): Contains Markdown-formatted documentation for the EAR architecture and PEGASUS file format.
* [earasm](earasm): EAR assembler and PEGASUS linker.
* [kjc_argparse](kjc_argparse): Command line argument parsing library.
* [mkexeloadable](mkexeloadable): Tool used to patch Linux executables to make them loadable using `dlopen()` on newer versions of Ubuntu.
* [pegasus_ear](pegasus_ear): Core EAR emulator, debugger, and PEGASUS loader. Product: `libpegasus_ear.so`
* [runear](runear): Simplified command line program for running a raw binary file as EAR code. Product: `runear`
* [runpeg](runpeg): Command line program for running a PEGASUS file with a variety of options. Product: `runpeg`
* [server](server): Library for receiving a PEGASUS file from a client, saving it, and running it. Product: `libpegasus_server.so`
* [tests](tests): Contains a `pytest` suite for testing the assembler, and assorted assembly files for testing the loader/emulator.


## Building & Deploying

Must be built under Linux. Only tested on Ubuntu 18.04 and 20.04.

```bash
git clone https://github.com/C0deH4cker/PwnableHarness.git && cd PwnableHarness
git clone --recursive https://github.com/kjcolley7/PEGASUS.git && cd PEGASUS
```

* `make`: Compile all binaries
* `make docker-build`: Build Docker images for all server-based challenges
* `make docker-start`: Start Docker containers for all server-based challenges
* `make publish`: Publish all build artifacts that should be distributed to players to the `publish` folder/symlink
* `make check`: Run the test suite for assembler and emulator
* `make solve`: Run solution scripts for all challenges, using `localhost` for server challenges
* `make solve SERVER=<address>`: Run solution scripts for all challenges using the provided IP/hostname as the target server
