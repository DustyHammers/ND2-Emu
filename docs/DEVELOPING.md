# Developing ND2

Notes for building and testing. The OS update file is never part of this repository.

## What's emulated

| Part | Status |
|---|---|
| dsPIC33E core: full instruction set used by the OS, 40-bit accumulators, MAC prefetch/write-back, saturation, convergent rounding, REPEAT/DIVF, DISI, EDS/PSV paging | done, verified against the Ghidra listing (86 292 instructions) |
| Interrupt controller, timers 1–7, UART2 (MIDI in/out), DCI + DMA1 audio out, flash self-programming | done |
| Audio: Fs = Fcy/2048 = 38 085.9375 Hz, 32-bit stereo codec words | done |
| Presets: SysEx ProgDump load + Get Bank round trip | done, 200/200 factory programs byte-identical |
| ADC1 scan (17 inputs, 15.1 kHz) + DMA0 ping-pong → trigger front-end; pads 1–6 = AN12, AN2, AN4, AN6, AN8, AN10 | done (piezo hits modelled as damped sines) |
| Front panel: 4-digit 7-segment display + 29 LEDs (8-column multiplex, PWM brightness), 15 buttons, encoder, MASTER LEVEL pot (read by the OS on AN25) | done; map in `source/core/nd2/panel_map.h` |

Busy-wait loops in the OS are fast-forwarded cycle-exactly (output is bit-identical with and without it).

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build            # set ND2_OS=/path/to/os.cab to include the device test
```

On macOS the defaults are `arm64` and deployment target 13.0.

## OS update file

Any of these work (the loader finds `os.cab` inside the file and verifies it is v3.00):

- `os.cab` from the Mac updater app (`Contents/Resources/os.cab`)
- the Windows updater `.exe` (unzipped)

## Command-line renderer

```sh
build/nd2emu -os os.cab -flash flash.bin -t 5 -o out.wav \
  -bank 1.3,10,1 -pgm 1.31,10,1 -note 1.5,10,60,110
```

| Option | Meaning |
|---|---|
| `-os file` | OS update (or `ND2_OS` env) |
| `-flash file` / `-saveflash file` | load / save the preset flash image (0x40000–0x557FF, 4 bytes per word) |
| `-syx t file.syx` | send SysEx one message at a time from time t, waiting for each reply |
| `-note t,ch,note,vel` | note on (default kit: ch 10, pads 1–6 = notes 60 62 64 65 67 69) |
| `-bank t,ch,bank` | bank select (CC0 = 0, CC32 = bank − 1) |
| `-pgm t,ch,prog` | program change (1–50) |
| `-midi "t,hex …"` | raw MIDI bytes |
| `-pad t,pad,strength` | piezo hit on trigger input 1–6 (strength 0–1 of ADC full scale) |
| `-press t,idx[,dur]` / `-enc t,detents` | press panel button idx (row·8 + bit) / turn the encoder |
| `-panel t` | print the display and lit LEDs at time t |
| `-master 0..1` | MASTER LEVEL knob position (default 1 = full) |
| `-o file.wav` `-from t` | write audio (native rate) starting at time t |
| `-midiout file` | save everything the drum sent |
| `-bp`, `-tr`, `-dump`, `-pcprof`, `-check`, `-stats` | debugging |

Times are seconds from power-on; the OS is ready about 1.2 s after power-on.

## Engine renderer (what the plugin runs)

`nd2render` drives the same `Engine` the plugin uses: host sample rate, host-sized blocks, sample-accurate MIDI at a
constant latency, and the six trigger inputs as host audio. Output is latency-compensated 24-bit stereo and is
identical whatever the block size.

```sh
build/nd2render -os os.cab -flash flash.bin -trig pads.wav -o out.wav -bank 0.1,10,1 -pgm 0.2,10,1 -note 1.0,10,69,100
```

| Option | Meaning |
|---|---|
| `-trig file.wav` | six-channel trigger audio at the render rate (channel k → pad k+1; 16/24/32-bit or float) |
| `-gain pad,g` | input gain for one trigger input |
| `-rate hz` `-block n` | host rate (default 48000) and block size (default 256) |
| `-note`, `-pgm`, `-bank`, `-midi`, `-press`, `-enc`, `-master` | as for `nd2emu`, times from the start of the render |
| `-loadstate f` / `-savestate f` | restore / save the whole machine (plugin state format) |
| `-keeplatency` | do not remove the reported latency from the output |
| `-stems prefix` | also write `prefix_ch1.wav` … `prefix_ch6.wav`: each channel's exact share of the main output (level, pan and master included); the six add up to the main output |

## Tools

- `tools/nd2_sysex.py`: `.nd2_bank` / `.nd2p` ↔ SysEx (ProgDump protocol), Get Bank queries, parser
- `tools/dis_check.cpp`, `tools/cmp_dis.py`: decoder check against a Ghidra listing

## Layout

```
source/core/dspic   CPU: decoder, disassembler, interpreter
source/core/nd2     ND2 board (peripherals), OS loader, Device API, Engine (plugin core)
source/core/util    rate converter, MIDI parser/ring, state reader/writer
source/cli          nd2emu command-line renderer/debugger, nd2render engine renderer
source/plugin       VST3/AU/standalone (to come)
tests               core and util self-tests (no firmware needed) + device and engine tests (need the OS)
```
