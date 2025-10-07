This challenge is a fairly simple program which reads one byte at a time from
both stdin and the flag port (0xF) and then compares them. Players are given
access to the PEGASUS debugger, which only allows non-invasive debugging. This
means that players can't simply jump to some win function or modify code or
data in the program.

The expected solution is to add a breakpoint after the flag byte has been read
into R8 but before the next byte is read from stdin. Each time the breakpoint
is hit, the value from R8 decides what byte should be sent to the program's
stdin port. This reveals the flag one character at a time.

My solution script is in [solve](solve).
