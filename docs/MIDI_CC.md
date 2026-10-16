# MIDI control changes

The OS responds to MIDI control changes (CC) for every sound setting on the panel, as the original hardware does,
and sends the same CCs when a setting is turned on the panel. ND2 passes MIDI from the host to the OS unchanged and,
since 0.2.1, the OS's MIDI out back to the host. So a host can record panel moves as CC automation and play them back,
or you can draw CC envelopes yourself (in Ableton Live: a MIDI clip's envelopes, "MIDI Ctrl").

This chart was measured in the emulator (OS v3.00, factory settings): each CC sent to the OS, then the setting read
on the display; and each setting turned on the panel, then the OS's MIDI out read.

## Channels

| Sent on | Changes |
|--|--|
| MIDI channels 1-6 | drum channels 1-6 (each drum channel's own MIDI channel; factory setting 1-6) |
| the global MIDI channel (factory setting 10) | the drum channel selected on the panel (CHANNEL SELECT) |

The OS sends its CCs on the global MIDI channel, for the selected drum channel. Pad hits go out as notes (global
channel, the pad's note), program changes from the panel as bank select (CC 0, CC 32) and program change.

## Settings

CC values 0-127 are spread over each setting's range (for a 0-50 setting, 64 is 25). Settings with a list of types
or modes step through the list in the panel's order.

Upper row (with LOWER PARAM held: the red label):

| Button | Setting | CC | Range on the display |
|--|--|--|--|
| 1 | FILTER | 14 | 0 to 50 |
|   | FILT TYPE | 15 | filter types (LP12, LP24, BP12, ... HP) |
| 2 | DYN FILTER | 16 | -50 to 50 |
|   | FILT RES | 17 | 0 to 20 |
| 3 | ATK/RATE | 18 | 0 to 50 |
|   | ENV MODE | 19 | envelope modes (AD, LFO ..., CLP ...) |
| 4 | DECAY (noise) | 21 | 0 to 50 |
|   | TYPE/DYN (noise decay) | 22 | Off, then up to 50 |
| 5 | DISTORT | 23 | 0 to 50 |
|   | TYPE (distortion) | 24 | distortion types |
| 6 | EQ FREQ | 25 | 50 Hz to 12.0 kHz |
|   | GAIN (EQ) | 26 | -24 to 24 |
| 7 | REPEAT | 27 | 0, then repeat rates |
|   | AMOUNT (repeat) | 28 | 0 to 20 |
| 8 | LEVEL | 7 | 0 to 50 |
|   | PAN | 10 | left 20 to right 20 (64: centre) |

Lower row (ROW SELECT; with LOWER PARAM held: the red label):

| Button | Setting | CC | Range on the display |
|--|--|--|--|
| 1 | SPECTRA | 30 | 0 to 99 |
|   | WAVE | 46 | tone models |
| 2 | DYN TIMB | 53 | 0 to 50 |
|   | TIMBRE | 52 | 0 to 50 |
| 3 | HI DECAY | 47 | 0 to 50 |
|   | PUNCH | 48 | Off, then punch types |
| 4 | DECAY (tone) | 50 | 0 to 50 |
|   | TYPE/DYN (tone decay) | 51 | Off, then up to 50 |
| 5 | PITCH | 31 | 0 to 127 (semitones); see below |
| 6 | BEND | 54 | -50 to 50 |
|   | TIME (bend) | 55 | 0 to 50 |
| 7 | CLICK LEVEL | 56 | 0 to 50 |
|   | TYPE (click) | 57 | click types |
| 8 | MIX (tone / noise) | 58 | tone 20 / noise 0 to tone 0 / noise 20 (64: both 20) |

PITCH: CC 31 is the pitch in semitones (60 shows as 60.0). For half semitones send CC 63 first, then CC 31: CC 63 of
64 or more adds half a semitone (CC 31 alone always gives a whole semitone). The OS sends both when PITCH is turned on
the panel. SCL PRE (PITCH's red label) has no CC of its own here.

## Limits

MIDI reaches the OS at the original hardware's speed (31,250 bits per second): one CC takes about 1 ms, so about
1,000 CCs per second in all. Very dense CC automation (fast LFOs on several settings at once) fills the line, and the
notes behind it arrive late, exactly as on the hardware. Settings move in the steps shown on the display.
