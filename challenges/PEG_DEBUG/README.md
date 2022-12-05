# Deployment Notes

This challenge involves 3 services:

* `pegsession` on port 27000
* `peg-debug` on port 27001
* `peg-debug-ssh` on port 27002

The `pegsession` service can actually work for any number of distinct PEGASUS
challenges that involve access to the debugger. The `peg-debug` and
`peg-debug-ssh` Docker containers depend on the `pegsession` container to
start, as they mount a shared volume from the `pegsession` container that holds
UNIX domain sockets that are used for the program's I/O.
