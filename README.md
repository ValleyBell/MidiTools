# MIDI Tools

various tools related to patching/modifying MIDI files

## MIDI Event Sorter

This tool allows you to sort MIDI events that occour at the same tick.  
This can be useful when you have MIDIs where e.g. the Note On event occours before instrument is set, resulting in a wrong instrument.

The tool sorts MIDI events in the following order:

- Note Off events (from all channels)
- parameter change events, grouped by channel 1..16
    - Bank Select MSB, LSB
    - Patch Change
    - Control Changes
    - Polyphonic Aftertouch
    - Channel Aftertouch
    - Pitch Bend
- Note On events (from all channels)

SysEx messages, Meta Events, Channel Mode Messages (CC 120..127) and RPNs/NRPNs will not be moved.

By default, the original order of the Note events and Control Changes inside their "block" is kept.  
Using the `-e` parameter, a bitmask can be set to enable sorting those in ascending order.

Example for `-e 0x01`:

- Control Changes in the order 7, 91, 11, 10 will be sorted to 7, 10, 11, 91.

## Midi Splitter

This tool allows you to split MIDI tracks up based on certain criteria.

Currently you can:

- split chords: puts overlapping notes (e.g. chords) that occour on one channel on separate tracks, so that each track is monophonic per channel
- split by instrument: puts each instrument on a separate track, useful when editing e.g. xm2mid conversions
- split by channel: same as converting Format 0 to Format 1
- split by volume: make a separate track for each note velocity
- split by key: make a separate track for note pitch

I originally wrote this in 2012, but with an older version of my MIDI library.  


## MIDI Volume Converter

With this tool you can convert MIDI volume values to different scales.
This is what I wrote the tool for initially.

But you can also modify the volume of the MIDI,
which is what I use this tool for most of the time these days.

Converting volume scales is useful for e.g. fixing the velocities in MIDIs converted from PSX seq files.  
Supported scales are:

- `GM` - General MIDI scale (127 = 0 db, 90 = -6 db, 64 = -12 db, 45 = -18 db)
- `Lin` - linear scale (127 = 0 db, 64 = -6 db, 32 = -12 db, 16 = -18 db)
- `FM` - Yamaha FM scale (-0.75 db steps: 127 = 0 db, 119 = -6 db, 111 = -12 db)
- `PSG2` - SN76489 PSG scale (-3 db per 8 steps: 127..120 = 0 db, 119..112 = -2 db, 111..104 = -4 db)
- `PSG3` - AY8910 PSG scale (-2 db per 8 steps: 127..120 = 0 db, 119..112 = -3 db, 111..104 = -6 db)
- `WinFM` - scale used by Windows OPL3 FM MIDI driver

You can convert freely between the various scales.


# Libraries

## MidiLib.cpp/hpp
This is a MIDI library that allows you to read in MID files, modify them and write them back.
It is relatively low-level, but that's what I prefer.

One notable feature is, that it keeps track of the "running staus" of the original data.
This means you can write the files back with minimal changes to the byte stream.


# Complilation notes

Project files for VC++ 6 and VC2010 are included.

If you want to compile them with GCC, you need to link with *MidiLib.cpp* and the Math library `m`.

```
g++ -I. MidiLib.cpp <tool.cpp> -lm -o <tool>
```
