# Euclid RPE

A **pure C99 CLAP plugin** that fires MIDI notes on Euclidean rhythms.
It is a small clone of the Euclidean engine in HY-RPE2: you give it notes
and a step/trigger count, and it spreads the hits as evenly as possible.

Drop it on a MIDI / instrument track **immediately before a drum machine
or sampler**. The plugin itself is silent — it only emits note-ons.

## What you set

Each of the 8 tracks has:

| Param | Meaning |
| --- | --- |
| Note | MIDI note to trigger (GM drums: 36 kick, 38 snare, 42 closed hat…) |
| Steps | Length of the cycle (1–32) |
| Triggers | How many hits inside that cycle (0–steps) |
| Rotate | Shift the pattern later in the cycle |
| Velocity / Channel / Mute | as labelled |

Global clock: **Rate** (1/4, 1/8, 1/16, 1/32), **Swing**, **Gate %**.

## Build

Needs a C99 compiler and git.

CLAP is included as a git submodule:

```bash
git clone --recurse-submodules https://github.com/your/repo.git
# or after cloning:
git submodule update --init --recursive
```

Headers are at `third_party/clap/include`.

```bash
make
make test
make install    # copies EuclidRPE.clap to ~/.clap
```

Then scan for plugins in Bitwig, Reaper, Ardour, or any CLAP host.
