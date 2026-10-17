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
