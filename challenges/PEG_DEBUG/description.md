You manage to connect to the JTAG interface of the console! However, the
debug interface provided over JTAG is partially locked down and only
provides non-intrusive debugging capabilities. Maybe that will be enough
to bypass the password protection on the device?

-----

### Note:

There are intentionally no files delivered as part of this challenge. You
need to use the interactive debugger to solve it.

You can connect to this challenge over SSH for an enhanced interactive
debugging experience with tab-completion, arrow keys, suggestions, etc.
The debugger is also available over a standard TCP socket, which may be
easier to use from a script. After connecting to the debugger (either SSH
or plain TCP), you then connect to another port from a second terminal to
access the program's I/O. That is, you'll have one terminal window connected
to the debugger, and another connected to the program's stdin/stdout.

### 1A. Connect to debugger (enhanced SSH version)

```
ssh -o StrictHostKeyChecking=no -p 22702 peg-debug@sunshinectf.games
```

### 1B. Connect to debugger (plain TCP version)

```
nc sunshinectf.games 22701
```

### 2. Connect to program I/O (in another terminal)

```
nc sunshinectf.games 22700
```

Then, paste the PEGASUS debug session ID that the debugger connection printed.

Author: kcolley
