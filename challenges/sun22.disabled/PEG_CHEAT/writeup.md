This challenge consists of a PEGASUS program that is run directly upon TCP
connections. The PEGASUS program file is provided to players. The program
runs a game that's basically the glass bridge from Squid Game, except the
code contains a bug when checking if the player wins. This bug means that
even by getting to the end across all of the tiles, it will still say that
you died. So even though the correct path across the silicon bridge is
hardcoded as a constant in the binary that can be recovered without much
difficulty, it's useless.

When you first run the game, you are given an opportunity to enter a cheat
code. If you enter the code `UUDDLRLRBA`, aka the Konami Code, you are
prompted for a code address to patch and the byte to write at that address.
So the challenge is how to do a 1-byte patch to either fix the bug by using
the correct condition code on the conditional branch instruction, or just
change that conditional branch to unconditionally branch to the code that
prints the flag. There are certainly other patch locations that could be
used, such as changing some other branch in the program to instead branch
to the win code. Changing the condition code on the `BRR` instruction is
just the most straightforward and natural choice.

My solution script is in [solve](solve).
