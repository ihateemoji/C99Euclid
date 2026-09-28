# C99Euclid [WIP]

Minimal C99 CLAP Euclidean sequencer with a lightweight X11 GUI

![screenshot](imgs/C99Euclid.jpeg?raw=true)

## Disclaimer

This project is very much WIP; feel free to test it and let me know if any issues come up.

## Build

All you need is:
- C99 compiler (`gcc` or `clang`)
- X11 development headers (`libx11-dev` on Debian/Ubuntu)
  
CLAP is included as a git submodule, so all you need is to clone this repository:
```bash
git clone --recurse-submodules https://github.com/ihateemoji/C99Euclid
```
and then simply build the code with 
```bash
make            
```
which will produce a `C99Euclid.clap` binary. You could install this binary manually or let make copy it for you into `~/.clap/C99Euclid.clap` via 
```bash
make install
```

Then rescan plugins in any CLAP host (Bitwig, Reaper, Ardour, …).

PS: If you are like me and forget `--recurse-submodules` 9/10 times, you can just cd into the cloned directory and
```bash
git submodule update --init --recursive
```

## Theming

Because of how this project is built, it is very easy to tweak the GUI colours before compiling.
See [THEMING.md](THEMING.md) for more info on how to do this and examples of themes.
Also, if you make something cool, feel free to open a pull request to add your themes to [THEMING.md](THEMING.md).

## Disclaimer

I have very minimal experience with GUI programming, so I started this project as an opportunity to gain some.
Any improvements/contributions from anyone more experienced than me will be greatly appreciated!
