# ND2: rules for working in this repository

ND2 is a free, open-source VST3/AU/standalone plugin that runs a drum synthesizer's original OS (v3.00) on an
emulated dsPIC33E. The OS provides all synth behaviour; our code is the hardware. The owner has no local build
machine: GitHub Actions builds everything and the owner tests in REAPER on an Apple Silicon Mac (macOS 13).
Guide the owner step by step through anything they must do by hand on github.com or on the Mac.

## Rules (from the owner; do not relax them)

1. **No firmware or proprietary data, ever**: no OS image or update file, no decrypted image, no presets or
   factory banks, no SysEx dumps, recordings, renders or trace output. Never write a decrypted OS image to disk.
2. **Never name the original product or its manufacturer** anywhere in this repository or on GitHub (code,
   comments, file names, docs, tests, commit messages, branches, PR titles/descriptions, issues, CI names).
   Neutral terms: "the original drum module", "the original hardware", "the OS", "the OS update file".
3. **No synth parameters added or removed** without the owner's explicit instruction.
4. **Mockup before any UI.** Show a mockup and wait for approval before building or changing user interface.
5. **Stop after each phase**: summarise what changed and say what to test.
6. **Speed work must keep the output byte-identical** (regression render checksum).
7. **Audio first**: changes to sound or timing end with a render to listen to.
8. Do not copy Gearmulator code (GPL-3); using its design as a reference is fine.
9. README stays minimal (description, firmware statement, disclaimer, licence). The owner reviews any change to it.

## Identity (fixed forever: hosts use it to find the plugin in saved projects)

Plugin name "ND2", plugin code "Nd2E", manufacturer "DHammers", manufacturer code "DHmr", bundle ID
com.dhammers.nd2. No copyright holder line anywhere: the project is free and open source under AGPL v3.

## Facts

- Images are allowed only for the built-in default skin (one folder, added in the plugin phase).
- Channel outputs (six stereo pairs) are copies of each channel's share of the main output; the main output is
  never changed by them.
- Build and test notes: `docs/DEVELOPING.md`.
