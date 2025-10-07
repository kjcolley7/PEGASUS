PegasusEar
-----

Custom CPU architecture and binary file format, initially created for SunshineCTF 2020.


## Useful Documentation

* [EAR Extended Architecture Reference](docs/EAR_EAR_v3.md)
* [EAR Assembler Manual](docs/EARASM.md)
* [PEGASUS File Format Reference](docs/PEGASUS.md)


## Project Structure

* [docs](docs): Contains Markdown-formatted documentation
* [libear](libear): Core EAR emulator. Product: `libear.so`
* [libeardbg](libeardbg): EAR debugger core and REPL. Product: `libeardbg.so`
* [runpeg](runpeg): Command line program for running a PEGASUS file with a variety of options. Product: `runpeg`
* [earasm](earasm): EAR assembler and PEGASUS linker
* [vscode-extension](vscode-extension): VSCode extension adding syntax highlighting to EAR assembly files (`*.ear`)
* [bootrom](bootrom): Source code of the EAR CPU's bootrom. Product: `boot.rom`
* [challenges](challenges): Source code for the various CTF challenges that have been built on top of the PEGASUS platform
* [client](client.disabled): Command line program to upload a PEGASUS file to a server and print the result. Product: `submitpeg`
* [server](server.disabled): Library for receiving a PEGASUS file from a client, saving it, and running it. Product: `libpegasus_server.so`
* [pegsession](pegsession): Infrastructure for hosting challenges with both the debugger and challenge I/O accessible on different ports
* [common](common): Defines macros and other common support functionality used by multiple subprojects
* [libraries](libraries): Contains library submodules and the build definitions for them
* [mkexeloadable](mkexeloadable): Tool used to patch Linux executables to make them loadable using `dlopen()` on newer versions of Ubuntu
* [tests](tests): Contains a `pytest` suite for testing the assembler, and assorted assembly files for testing the loader/emulator


## Building & Deploying

Builds using PwnableHarness's `pwnmake` utility:

```bash
git clone --recursive https://github.com/kjcolley7/PEGASUS.git && cd PEGASUS
pwnmake <build-targets>
```

* `pwnmake`: Compile all binaries
* `pwnmake docker-build`: Build Docker images for all server-based challenges
* `pwnmake docker-start`: Start Docker containers for all server-based challenges
* `pwnmake publish`: Publish all build artifacts that should be distributed to players to the `publish` folder
* `pwnmake check`: Run the test suite for assembler and emulator
* `pwnmake solve`: Run solution scripts for all challenges, using `localhost` for server challenges
* `pwnmake solve SERVER=<address>`: Run solution scripts for all challenges using the provided IP/hostname as the target server
