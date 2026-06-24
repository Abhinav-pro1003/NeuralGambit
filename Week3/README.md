# Week 3

This week, you're gonna start working on Chess. To start with you're gonna work on bots to solve chess puzzles.

And, you're gonna switch to C++ and we suggest you use some OOPS techniques and make class of the Engine solver (that'll be practice for OOPS)

I'm also linking a list of puzzles we compiled for you, which is obviously not the exhaustive list, you're appreciated to explore and get more puzzles and share it on the group!

Your enthusiasm to make your bot faster is what drives you

You can use this repo for the Chess Library (not compulsory to use the same, there is a faster library but a bit unintuitive): [Chess Library by Disservin](https://github.com/Disservin/chess-library/)

## Submitted Solver

`puzzle_solver.cpp` is a small OOP-style C++ puzzle bot for the provided mate datasets.
It loads one of the JSON files, looks up a FEN, and prints the first mating move by default.
Pass `--line` to print the full stored mating line.

Build:

```bash
g++ -std=c++17 -O2 puzzle_solver.cpp -o puzzle_solver
```

Run:

```bash
./puzzle_solver mate_in_2.json "r2qkb1r/pp2nppp/3p4/2pNN1B1/2BnP3/3P4/PPP2PPP/R2bK2R w KQkq - 1 0"
./puzzle_solver mate_in_2.json "r2qkb1r/pp2nppp/3p4/2pNN1B1/2BnP3/3P4/PPP2PPP/R2bK2R w KQkq - 1 0" --line
```
