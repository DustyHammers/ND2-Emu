# Circuit-bent mode

An emulator extension: the original hardware has no such mode. Cmd-click the panel (Ctrl-click on Windows) to open
the bent panel: six bend pots, one per pad, and an indicator LED. Each pot picks one bend for its pad: Off or one of
eight. Cmd-click again to close the panel (the pots keep their settings). The panel opens by itself when a bend is
turned on, so a bent sound always shows.

| Pot | Bend | What happens inside the emulated chip |
|--|--|--|
| 0 | Off | nothing: the pad plays exactly as normal |
| 1 | Crush | the pad's finished signal keeps 8 bits |
| 2 | Sample & Hold | the pad's finished signal is held for 8 samples |
| 3 | Stuck Bit | one data line (bit 25) of the pad's signal is stuck low |
| 4 | Crackle | a bad joint: now and then a high bit of the pad's signal flips |
| 5 | Underclock | the pad's sound engine runs every other sample: an octave down, with a buzz |
| 6 | Stretch | the pad's envelopes advance one step in three: decays 3x longer, the pitch unchanged |
| 7 | Sine Bend | the pad's sound engine reads the sine table with two address lines swapped |
| 8 | Noise Bend | the pad's noise comes from its own generator with a multiplier bit cleared: metallic |

- Bends act inside the chip, before the mix: nothing is processed after it (no limiter). The channel outputs carry
  each pad's bent sound.
- Some bends only change pads that use what they bend: Sine Bend only sine-based tone models, Noise Bend only pads
  with noise in their sound. Stretch covers the main loudness and noise envelopes; a few tone models (some hi-hats)
  keep their decay elsewhere and are not stretched yet.
- With every pot Off, the output is byte-identical to ND2 without this mode (checked against the previous version's
  render, main output and the six channel outputs).
- A bend on one pad leaves the other pads byte-identical (checked for every bend on every pad, `tests/test_bends.cpp`),
  except Underclock: the time a skipped engine saves lets the chip's interrupts land a little earlier, so tone models
  whose noise depends on that timing (a hi-hat model, for one) get different noise details. Their level and spectrum
  stay within about 1 dB.
- The pots are host parameters **Bend 1** ... **Bend 6** (IDs `bend1` ... `bend6`; Off and the 8 bends), so they can be
  automated. They are saved with the project. The panel's own controls are not parameters.

## Controls

- Turn a pot by dragging up or down (Shift: fine) or with the scroll wheel (one bend a notch). It turns smoothly and
  selects the nearest bend as it goes; on release it settles on that bend.
- The LED is lit while the bent panel is open and flickers while a pot turns.

## Skins

- `<bendpot pad="1".."6">`: a sprite like the MASTER LEVEL knob (RCSS `frames`, `spriteprefix`); Off and bends 1-8 sit at
  frames round(step x (frames - 1) / 8).
- `<bendled>` with `<lamp>` children: the runtime sets the lamps' opacity (1, flickering while a pot turns).
- While the bent panel is open, the document body has class `bent`. The Default skin shows its panel, pots and LED only
  then (`body.bent ...` in `nd2-default.rcss`).

## Where in the OS (v3.00)

All found by tracing; every hook site's program words are checked before anything is installed
(`source/core/nd2/bends.cpp`). Notes and the experiments that led here: the `bend-explore` branch,
`docs/BEND_EXPERIMENTS.md`.

- Render pass, per channel ch (0-5): tone-engine dispatch `goto W0` at 0x1A68C + 0x90ch, returning to +2; the engines
  run as a chain (each leaves W8/W10 where the next one reads). The mix reads the six finished signals (32-bit, high word
  first) at 0x1E8E + 4ch from 0x1A9EE.
- Noise: the generator writes six words 0xDDBA ... 0xDDC4 (0xDDBA is also its state); channel ch's noise filter
  prefetches word ch at 0x1A4D8, 0x1A4DC, 0x1A4E0, 0x1A4E2, 0x1A4E6, 0x1A4E8.
- Control scheduler, once per pass (`inc.w 0x116C` at 0x1ACBC): the main loudness envelope 0xD090 + 8ch and the noise
  envelope 0x1D28 / 0x1D2A + 6ch.
- Sine table 0x39B8 (2048 words, built at boot), read by the tone engines through [W9+W12], [W11+W12] (DSP prefetch) or
  [Wn+Wb]; the CPU remaps those reads while a Sine Bend channel's code runs (`CPU::sine_remap`). The table in RAM is
  never changed, so a saved project never contains a bent table.
- Underclock: the skipped engine's `goto W0` goes to a trampoline (`goto` its return) in unused program memory at
  0x3F00 + 4ch, with the registers the engine left at its last run.

`nd2render -bend t,pad,bend` renders a bend from time t (pad 1-6, bend 0-8).
