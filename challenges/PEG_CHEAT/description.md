>Silicon Bridge is the classic thrilling game of chance! All you need to do is cross the bridge that's made up of two rows of silicon tiles. But be careful! At each step, one of the two silicon tiles will not support your weight! Can you make it to the end?

That's what the description on this game cartridge says, but Silicon Bridge seems impossible. Maybe there's some way to cheat?

=====

Run locally:
```
echo 'sun{fake_local_flag}' > flag.txt
runpeg --plugin peg_cheat_checker.so SiliconBridge.peg [--debug] [--verbose] [--trace]
```

Run on the challenge server:
```
nc sunshinectf.games 22704
```

Pegasus program:
* [SiliconBridge.peg](https://sunshinectf.games/21478ecf6429/SiliconBridge.peg)

Checker module for runpeg, just allows accessing a `flag.txt` file by reading from port 0xF (for Flag, get it?):
* [peg_cheat_checker.so](https://sunshinectf.games/21478ecf6429/peg_cheat_checker.so)

Author: kcolley
