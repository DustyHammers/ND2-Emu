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
| `-bend t,bend,amount` | circuit-bent mode from time t: bend 1-6 (CPU Starve, Brownouts, Time, Sine Stuck Bit, Crystal, DAC Bits), amount 0 (Off) - 1 (`docs/BENDS.md`) |
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
plugin              VST3/AU/standalone (JUCE): processor, editor, OS and bank file handling
plugin/skin         skin runtime without JUCE (RmlUi + Lua + FreeType) and its tests
skins/default       the built-in default skin (the only images in the repository)
tools/skin          sources of the default skin's art
tests               core and util self-tests (no firmware needed) + device, engine and bank tests (need the OS)
```

## Plugin

```sh
scripts/fetch_deps.sh                      # JUCE, RmlUi, Lua, FreeType at the commits in scripts/deps.env
cmake -S . -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Release -DND2_BUILD_PLUGIN=ON -DND2_BUILD_TESTS=OFF -DND2_BUILD_CLI=OFF
cmake --build build/plugin
build/plugin/plugin/plugin_test [--editor [screenshot dir]]
```

`plugin_test` checks bus layouts, silence and state without the OS. With the OS in `~/Documents/ND2/os/` (or
`ND2_OS_SOURCE=` any update file, located as the plugin does) and `ND2_BANKS=` the factory bank files, it also
builds the factory memory, plays MIDI and a piezo hit, checks that the channel outputs add up to the main output,
and round-trips the state; with the factory memory also the Banks window's work (bank 1 saved as a file, loaded
into bank 6, the banks checked, the names in the project state). `--editor` opens the panel, clicks a button and the
encoder, opens the bent panel (Cmd-click), turns pot 1, lets automation turn pots 3 and 6, closes and reopens the panel
(all Off, then the pots back), and goes through the ND2 window's tabs (right-click, last tab, closing by the dimmed area
and the round button). It also checks the six bend parameters, their texts, Cmd-click's memory and that the amounts are
saved with the project.

Plugin data lives in `~/Documents/ND2/` (`os/`, `cache/`, `skins/`, `Library/`); global settings in
`~/Library/Application Support/ND2/ND2.settings`.

## Memory checks

CI runs the core and skin tests with sanitizers (see CI below). Locally, with the OS, the same build also covers the
engine, device and bank tests, and the plugin test (window, skin, Banks window) can be built the same way; as of 0.1.8
both run clean (no leaks, no invalid accesses, no undefined behaviour; the only report is 640 bytes inside libX11).
An engine soak (8 minutes of notes, pad hits, program changes, panel input, a state snapshot every 10 s and a
restore every minute) levels off at about 31 MB after the first minutes and stays there.

## Banks window

Bank files are the original manager's `.nd2_bank` (a stored zip of 50 `Program NNN.nd2p`). Every bank job works on a
copy of the running machine (`Engine::getState`) restored in an offline machine (`source/core/nd2/bankio.*`,
`BankMachine`), driven only through the OS's program dump messages as the original manager drives the hardware:

- read: QryPerf_File (ProgDump msg 7, header `0x40|bank, program`) → DtaPerf_File (msg 8, 212 bytes), or AckPerf_File
  (msg 9) code 01 when the slot is empty;
- write: DtaPerf_File (msg 8) → AckPerf_File code 00 (one at a time, each acknowledged);
- a load hands the offline machine's state back to the running one (`Engine::setState`); if the program playing is in
  the loaded bank, the offline machine selects it again first (bank select + program change) so the new sound plays.

Programs missing from a file leave the bank's other slots as they were (the OS has no message that empties a slot).
The plugin side is `plugin/Source/BankFiles.*` (files, library folder, bank names), `BankService.*` (jobs, one at a
time per instance on a worker thread) and `BanksPanel.*` (the BANKS tab of the ND2 window). `tests/test_banks.cpp`
covers the offline machine with the OS and the factory bank files (`ND2_OS`, `ND2_BANKS`).

## The ND2 window

`ND2Window.*`: the one menu system (approved mockup), opened by a right-click on the panel on the tab used last (the
first time BANKS). Tabs: SYSTEM (version, the OS update file, the factory banks, the data folder; the RmlUi debugger),
BANKS (`BanksPanel`), SKIN (skins, Reload skin, Show skins folder, zoom) and MIDI (MIDI out to the host, saved with
the project in processor state version 3; the OS's MIDI channels, read from RAM 0x49D8 and 0x49DA by
`Engine::publishPanel`, shown only; the CC chart of `docs/MIDI_CC.md`). Placed as in the owner's other plugin: laid
out at 1500 x 900 dp, drawn 1300 dp of the skin wide, centred over a dimmed backdrop with a shadow (`ui::placeCentred`,
`ui::drawBackdrop`). The round button, Esc or a click on the dimmed area closes it (a question in the footer is
cancelled first); the bottom-right corner stays with the resize grip. Built from the shared window parts in
`plugin/Source/ui/` (frame, tabs, footer, buttons, switch, segmented, rows), which come from the owner's other plugin
with ND2's red highlight and the Banks window's font. Skin actions: "banks" opens the BANKS tab.

## Circuit-bent mode

Six bends of the whole kit, each an amount 0 (Off) ... 1. `source/core/nd2/bends.*`: the amount mappings
(`bendmap`) and the two bends inside the OS (Time: CPU hooks on the control scheduler; Sine Stuck Bit: `CPU::sine_or`),
installed by `Board::boot`. `Board::frame_cycles` (CPU Starve, Brownouts). `Engine`: `setKitBend` (lock-free, applied
at the next block), `updateClock` (the bent clock: cycles per host sample and the MIDI delay follow it, re-anchored at
a block start), `pushNative` (Crystal: the machine's frames read at the crystal's ratio, cubic; DAC Bits). Plugin: six
parameters (`ND2Processor`, processor state version 5), Cmd-click's memory (`ND2Processor::setBends`), the skin elements
`<bendpot pot>` (smooth or stepped) / `<bendled>` and the body class `bent` (`plugin/skin/src/skin_elements.*`,
`skin_view.cpp`), Cmd-click in `RmlSkinComponent`. `tests/test_bends.cpp` (with `ND2_OS`, optionally `ND2_FLASH`)
checks that every bend changes the sound, that Sine Stuck Bit and DAC Bits leave no trace once off, and that the hits
stay on the host's beat with the clock bends. Details and the OS addresses: `docs/BENDS.md`.

## CI

`.github/workflows/ci.yml`: `scripts/check_no_data.py` over every commit (firmware and data file types, known
firmware checksums, images outside `skins/default/`, the original names in files and commit messages), core and
skin tests on Linux (twice: normally, and built with AddressSanitizer, LeakSanitizer and UndefinedBehaviorSanitizer,
where any leak, invalid memory access or undefined behaviour fails the build), and on macOS the core tests, the plugin build, `plugin_test`, ad-hoc signing,
`scripts/check_plugin_identity.py`, `auval` and the downloadable zip (with `plugin/packaging/INSTALL.txt`).

Windows (`windows-plugin`): VST3 + Standalone for x64, built with clang-cl (Visual Studio's ClangCL toolset, so the
CPU uses the same computed-goto dispatch as on macOS; MSVC would fall back to the switch), without LTO (the Microsoft
linker cannot link clang's LTO objects). The job runs the core tests, `plugin_test` (no editor window: the runner has
no OpenGL 3), the identity check and pluginval without GUI tests, and uploads `ND2-<version>-Windows-<commit>.zip`
(with `plugin/packaging/INSTALL-Windows.txt`). The default skin is copied into the VST3 bundle's
`Contents/Resources/nd2-default` and next to the standalone `ND2.exe`.

Release (`release`, after all other jobs): on main, a version without a release yet is released with both zips as
`ND2-macOS.zip` and `ND2-Windows.zip`, with the README's notes for that version; on pull requests the job only
checks that both zips arrive.
