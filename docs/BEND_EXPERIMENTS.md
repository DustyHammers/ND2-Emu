# Circuit-bent experiments (scratch, not part of the plugin)

`nd2emu` options added on this branch to try "circuit bending" the emulated hardware. Nothing here is used by the
plugin; with no option given, the output is unchanged.

| Option | Bend |
|--|--|
| `-bframe N` | CPU cycles per codec frame (normal 2048). Below about 1700 the render can't keep up and the 16-frame output ring replays: stutter and buzz. Note times are CPU time, so the groove slows unless the note times are scaled by N/2048. |
| `-bnosat` | accumulator and store saturation off: overflows wrap |
| `-bint` | fractional/integer multiply mode swapped (silences this kit: not useful) |
| `-bsine swap\|stuck\|crush\|gray` | the 2048-point sine table at RAM 0x39B8 after boot: address lines 2 and 9 swapped, data bit 12 stuck high, 3 bits, Gray code. Only the sine-bank tone models hear it. |
| `-bram addr,val` / `-bfill addr,count,val` | RAM pokes after boot. Noise generator constants: 0x1DFA (multiplier low word, 0x8435), 0xDDB4 (multiplier high word, 0x0BB3). Decay 1/x table: 0xAD62 (fill 0x0004: nothing decays; 0x7FFF: everything chokes). |
| `-bpatch addr,word` | program word patch in memory (re-decoded). Mix shift: 0x1AA1E and 0x1AA58 `sftac ACCA,#-3` = 0xC8007D; 0xC8007B is +12 dB into the clipper. |
| `-bdac bits` | keep only the top bits of the codec words |
| `-bfb gain,padmask` | feedback: the output added to the trigger inputs (pads by bit mask) |
| `-bvox ch,mode,amount` | per voice, on the channel's signal at 0x1E8E + 4(ch-1) (high word first) before the mix: `crush` bits, `hold` samples, `wrap` gain, `fold` gain, `flip` bit |
| `-bskip ch,N` | per voice underclock: the channel's tone engine runs 1 sample in N (dispatch `goto W0` at 0x1A68C + 0x90(ch-1), skipped through trampolines at 0x3F00 + 4(ch-1)); 1 in 2 is an octave down plus a buzz near Nyquist |

## Time and glitch bends

| Option | Bend |
|--|--|
| `-bstretch N` | time stretch: the OS's control scheduler (`inc.w 0x116C` at 0x1ACBC, then one rate group per sample by the counter's lowest set bit; every task ends with `goto 0x343E0`) runs on 1 sample in N. Envelopes, decays and pitch bends get N times longer; pitch is unchanged (measured: decay 0.19 / 0.37 / 0.75 s at x1 / x2 / x4, 193 Hz throughout). Done with a trampoline at 0x3F40 (`bra z` set by a hook, the two replaced instructions, gotos back). |
| `-bfreeze t0,t1` | the scheduler stops between t0 and t1 (CPU seconds): sounds stop evolving and hold, then carry on |
| `-bjitter p` | the scheduler is skipped at random with a slowly wandering probability up to p (subtle) |
| `-bbrown p` | brownouts: random bursts (20-100 ms) of CPU starve (700 cycles per frame) |
| `-bvox ch,crackle,p` | per voice: a random high bit flipped with probability p per sample |

## Random roll

`-broll seed,t_roll,t_reset`: at t_roll a random set of bends (each of 10 kinds with probability 0.3, at least one;
random amounts; per-voice bends on a random number of random voices), all undone at t_reset (originals saved first:
sine table, noise constants, decay table, mix words; flags and hooks switched off).

Fuzz, 240 seeds on bank 1 program 5 (roll at 1.5 s, reset at 5.6 s, run to 10 s): no OS crash or hang, every run
back to normal after the reset (last 2 s within 1.0 dB of the unbent render, 0.25 s windows; median 0.3 dB). Level
is the hazard: while bent the median was -13.5 dB RMS against -24.6 dB normal, 92 runs above -10 dB RMS and 154
reaching full scale (hot mix bus, wrap, feedback, jammed decay, stretch). A level guard is needed.

### Pool v2 (owner: no constant roar, no excessive noise, no feedback; no limiter)

No feedback; the decay table only chokes (0x1000-0x7FFF, never the jam that drones); one bend per voice; per-voice
wrap and fold gains 1.5-3; crackle at most 0.003; stretch x2-x3; mix bus +6 dB only, and only alone (not with
stretch, wrap or clipping off). Nothing is processed after the chip.

Fuzz, 240 seeds: no crash or hang; back to normal after every reset (worst 1.0 dB). Bent median RMS -25.2 dB against
-24.2 dB normal; loudest roll -16.0 dB (+8 dB); level between hits at most -20.7 dB (normal -34 dB), so no roar;
11 rolls more than 6 dB louder than normal.

## Per-voice stretch

The scheduler's per-channel handler calls (Fs/16 group: `goto W0` at 0x192FC + 4ch, returning to +2; Fs/256 group:
0x1A2AA + 4ch) are not where the audible decays are: skipping them changes nothing audible.

What works (`BVMAP=<any> BVENV=docs/bend_maps/stretch_env_words.txt nd2emu ... -bvstretch ch,N`): each channel's
envelope words, held back on N-1 of N passes (at the T5 loop head 0x343E0) when they only took a small step
(|delta| <= 0x400), so note starts (big jumps) always go through:
- 0xD090 + 8ch: the main loudness envelope (steps down every 2 passes, Fs/4 group). Alone, it gives exactly the
  whole-drum stretch on pads 1, 4, 5 of bank 1 program 5 (decay 1.19 -> 2.89, 0.19 -> 0.75, 0.35 -> 1.39 s at x4).
- 0x1D28 + 6ch and 0x1D2A + 6ch (the Fs/2 group's envelope, 32-bit): adds pad 6 (0.29 -> 1.23 s; whole drum 1.15) and
  part of pad 2 (0.12 -> 0.33 s; whole drum 0.46).
- Pad 3 of that program (hi-hat model) is not stretched yet: its decay is in its tone model's own state.
- A pad whose stretch is off is byte-identical to normal while another pad is stretched (all six checked).
Map tools: `BMAP=t0,t1,file` (bytes the scheduler changes), `BCHG=t0,t1,file` (words changed by anything),
`BLOG=a,b,...` (log words per pass), `-bsched` (goto sites), `BDUMP=pc` (one task in full).
Still to do for a real feature: the tone-model-specific decay words, for every tone model in every channel.

## Per-voice sine and noise; the ten proposed knob bends

- `-bvnoise ch,mode`: channel ch's noise from its own bent generator (mode 1: multiplier bit 0 cleared, metallic;
  2: multiplier 5, buzzy). The generator writes six words 0xDDBA..0xDDC4 (0xDDBA is also its state, high word; each
  next word = low word ^ the previous one); channel k's noise filter prefetches word k at 0x1A4D8, 0x1A4DC, 0x1A4E0,
  0x1A4E2, 0x1A4E6, 0x1A4E8. Hook 0x1A4D8 bends words 0-4; hook 0x1A4DC restores the generator state, recomputes word 5
  from the unbent word 4 and bends word 5. Checked: bending the other five never changes a pad.
- `-bvsine ch`: a bent copy (address lines 2 and 9 swapped) of the 2048-word table at 0x39B8 is swapped in at the
  channel's engine dispatch (0x1A68C + 0x90ch) and the normal table back at its return (+2), at the next channel's
  dispatch, and at the mix (0x1A9EE) (one engine doesn't return through +2). Checked: bending the other five never
  changes a pad. One hook per address: these share addresses with the underclock hooks (combine them in one hook).
- `-bvox ch,stuck0,bit`: a data line stuck low (truncation toward zero, so no DC at silence; `flip` makes DC).
- Crush on the channel signal needs about 8 kept bits (5 is near silence: the channel signals are small).
- Reel settings (all six pads): crush 8, hold 8, wrap 2.5, fold 3, stuck0 25, crackle 0.002, underclock 1 in 2,
  stretch x3, sine swap, noise mode 1.
