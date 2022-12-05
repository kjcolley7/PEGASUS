This challenge is exactly the same as PEG_AUTOREV, except I made each grid cell
use randomly selected characters instead of just '0' and '1' as the values to go
right and down. I figured this would make it players couldn't just bruteforce
it through `runpeg` with different binary strings as inputs. However, I didn't
consider that players could still do the same bruteforce attack if they ran the
program through `runpeg` with `--trace` and looked at the immediate value from
the `CMP` instructions that execute in each block. So sadly, this attempt at
preventing the brute-force attack did not work as intended.

I think I could have made the bruteforce attack infeasible within the time limit
for each level if the grids generated always had EXACTLY one valid path. For the
biggest grids, this would have required bruteforcing up to `2**n` possibilities,
where `n` is the length of the solution input string. Another idea could be some
sort of proof-of-work code that must be executed. Care would need to be take to
ensure that a player couldn't simply patch the program to jump over this proof
of work code and still do the brute-force attack. Perhaps the randomly generated
program could be packed in some way, and the program would work by running an
unpacker function at first before jumping to the unpacked code. There are many
different ways that the brute-force attack could be prevented, but it requires
careful consideration to be sure the countermeasures can't be bypassed with a
little work.

Sadly, I did not have time during the CTF to think up and implement a proper fix
for this unintended solution.
