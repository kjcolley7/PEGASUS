After entering the access code, you're now presented with the satellite terminal's login prompt. We don't know the correct login details, can you find a way around it?

* [LoginScreen.peg](https://sunshinectf.games/b7df841c9e55/LoginScreen.peg)
* [runpeg](https://sunshinectf.games/e589a450affa/runpeg) (updated)

-----

**Note**: There is a new build of `runpeg` available which now has a `--flag-port-file` option. Create a dummy `flag.txt` file and then run it like this:

```
runpeg LoginScreen.peg --flag-port-file flag.txt
```

This sets up the file whose contents are read when the EAR code runs the `RDB` instruction to read from port 0xF ('f' for flag, get it?). The way this works is that each time you execute `RDB <reg>, (0xF)`, the next flag byte is read into `<reg>`. When there are no more flag bytes left to read, `CF` will be set.
