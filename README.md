# MIDI Tools
various tools related to patching/modifying MIDI files

## Midi Splitter
This tool allows you to split MIDI tracks up based on certain criteria.

Currently you can:

- split by note: puts overlapping notes (e.g. chords) on separate tracks, so that each track is monophonic
- split by instrument: puts each instrument on a separate track, useful when editing e.g. xm2mid conversions

I originally wrote this in 2012, but with an older version of my MIDI library.  


## MIDI Volume Converter
With this tool you can convert MIDI volume values to different scales.
This is what I wrote the tool for initially.

But you can also modify the volume of the MIDI,
which is what I use this tool for most of the time these days.

Converting volume scales is useful for e.g. fixing the velocities in MIDIs converted from PSX seq files.  
Supported scales are:

- `MIDI` - General MIDI scale (127 = 0 db, 90 = -6 db, 64 = -12 db, 45 = -18 db)
- `Lin` - linear scale (127 = 0 db, 64 = -6 db, 32 = -12 db, 16 = -18 db)
- `FM` - Yamaha FM scale (-0.75 db steps: 127 = 0 db, 119 = -6 db, 111 = -12 db)
- `PSG2` - SN76489 PSG scale (-3 db per 8 steps: 127..120 = 0 db, 119..112 = -2 db, 111..104 = -4 db)
- `PSG3` - AY8910 PSG scale (-2 db per 8 steps: 127..120 = 0 db, 119..112 = -3 db, 111..104 = -6 db)
- `WinFM` - scale used by Windows OPL3 FM MIDI driver

You can convert freely between the various scales.
The only exception is that you can not convert *to* WinFM.


# Libraries

## MidiLib.cpp/hpp
This is a MIDI library that allows you to read in MID files, modify them and write them back.
It is relatively low-level, but that's what I prefer.

One notable feature is, that it keeps track of the "running staus" of the original data.
This means you can write the files back with without changing the whole structure.


# Complilation notes

Project files for VC++ 6 and VC2010 are included.

If you want to compile them with GCC, you need to link with *MidiLib.cpp* and the Math library `m`.

```
g++ MidiLib.cpp <tool.cpp> -lm -o <tool>
```
