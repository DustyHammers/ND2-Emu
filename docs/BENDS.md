# Circuit-bent mode

An emulator extension: the original hardware has no such mode. Cmd-click the panel (Ctrl-click on Windows) to open
the bent panel: six bend pots and an indicator LED. Each pot bends the whole emulated drum by the amount it is turned,
from Off (fully down) to full.

| Pot | Bend | What happens to the emulated hardware | Pot |
|--|--|--|--|
| 1 | CPU Starve | the chip gets fewer CPU cycles per sample (2048 normally; 1750 down to 700): the OS's render falls behind and the converter replays old frames (stutter, buzz); the OS's own timers slow down | smooth |
| 2 | Brownouts | random dips of the supply, 20-100 ms each (CPU Starve at 700 cycles), from about one a second to almost constant | smooth |
| 3 | Time | the OS's control scheduler (envelopes, decays, pitch bends, LFOs) runs on fewer passes: decays up to 32 times longer with the pitch unchanged; fully up it freezes (sounds hold; new hits don't start until it's turned down) | smooth |
| 4 | Sine Stuck Bit | a data line of the sine table's RAM stuck high: bit 8 (subtle) ... bit 15 (harsh). Only sounds that use the sine table change | 9 steps |
| 5 | Crystal | the clock crystal slowed, down to half speed: the whole drum lower and slower together, as with a slower crystal on the real board | smooth |
| 6 | DAC Bits | only the top bits of the output words reach the converter: 12, 10, 9, 8, 7, 6, 5, 4 bits (rounded toward zero: no offset at silence) | 9 steps |

- **Cmd-click** opens the panel. Cmd-click again closes it and turns every bend Off: the drum is back to normal (the
  host sees the parameters go to Off). Cmd-click once more and the pots come back as they were when the panel closed.
  That memory lasts while the plugin is open (it isn't saved with the project).
- The panel opens by itself when a bend is turned on (host automation, a project), so a bent sound always shows.
- Your host's notes stay on the beat whatever the clock does (CPU Starve, Brownouts, Crystal): the plugin follows the
  bent clock. On real hardware a slower crystal would also break MIDI; here it doesn't.
- Bends act on the emulated hardware, not on the finished audio: nothing is processed after the chip (no limiter).
  Time fully up can be about 10 dB louder than normal, because long decays overlap.
- With every pot fully down, the output is byte-identical to ND2 without this mode (main output and the six channel
  outputs, checked against the previous version's render).
- The pots are host parameters **CPU Starve**, **Brownouts**, **Time**, **Sine Stuck Bit**, **Crystal**, **DAC Bits**
  (IDs `starve`, `brownouts`, `time`, `sinebit`, `crystal`, `dacbits`), saved with the project. The panel's own controls
  are not parameters.

## Controls

- Turn a pot by dragging up or down (Shift: fine) or with the scroll wheel. The smooth pots follow the mouse; the stepped
  ones (Sine Stuck Bit, DAC Bits) select the nearest of their nine positions as they turn and settle on it.
- The LED is lit while the bent panel is open and flickers while a pot turns.

## Skins

- `<bendpot pot="1".."6">`: a sprite like the MASTER LEVEL knob (RCSS `frames`, `spriteprefix`); its whole travel is
  frame 0 (Off) to the last frame (full). The stepped pots' nine positions are spread evenly over it.
- `<bendled>` with `<lamp>` children: the runtime sets the lamps' opacity (1, flickering while a pot turns).
- While the bent panel is open, the document body has class `bent`. The Default skin shows its panel, pots and LED only
  then (`body.bent ...` in `nd2-default.rcss`).

## Where in the OS (v3.00) and the emulator

The OS sites were found by tracing; their program words are checked before anything is installed
(`source/core/nd2/bends.cpp`). Notes and the experiments that led here: the `bend-explore` branch,
`docs/BEND_EXPERIMENTS.md`.

- Time: the render loop ends each pass with the control scheduler (`inc.w 0x116C` at 0x1ACBC, then one task group
  chosen by the counter's lowest set bit; every group ends with `goto 0x343E0`, the loop head). A pass to skip gets a
  counter whose increment selects the empty group, and the real counter is put back at the loop head, so a skipped pass
  leaves nothing behind but the time it didn't take. No OS code is changed.
- Sine Stuck Bit: the 2048-word sine table at 0x39B8 (built at boot) is read by the tone engines through [W9+W12],
  [W11+W12] (DSP prefetch) or [Wn+Wb]; while the bend is on, the CPU sets the stuck bit in those reads
  (`CPU::sine_or`). The table in RAM is never changed, so a saved project never contains a bent table.
- CPU Starve, Brownouts: the converter frame period in CPU cycles (`Board::frame_cycles`, normally 2048).
- Crystal: the machine runs at the crystal's speed against real time, and its output is read at that ratio (cubic
  interpolation) before the plugin's sample-rate converter.
- DAC Bits: the 32-bit words the OS sends to the converter, main output and channel outputs alike.
- The plugin's MIDI timing follows the clock: cycles per host sample and the MIDI delay scale with it, re-anchored at a
  block start when it changes.

`nd2render -bend t,bend,amount` renders a bend from time t (bend 1-6 in the order above, amount 0-1).

## Earlier: per-pad bends

The first 0.3.0 test builds had six pots, one per pad, each choosing one of eight bends (Crush, Sample & Hold, Stuck
Bit, Crackle, Underclock, Stretch, Sine Bend, Noise Bend). They were replaced by the whole-kit bends before release;
the code is in the repository's history (commit 719bdcd).
