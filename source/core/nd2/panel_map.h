// ND2 front panel: button and LED indices as wired on the hardware (found by probing OS v3.00).
// Button index = row * 8 + bit (Board::set_button); LED index = (column - 4) * 8 + bit (Board::PanelState::led_duty).
#pragma once

namespace nd2::panel {

// ---- buttons (white label = function, red label = shifted/lower function on the hardware)
enum Button : int {
  BtnRowSelect    = 0,   // ROW SELECT          (Shift: SOLO EDIT)
  BtnProgram      = 1,   // PROGRAM             (Shift: STORE)
  BtnChannelPrev  = 2,   // CHANNEL SELECT left (Shift: MUTE GROUP, hold to clear)
  BtnChannelNext  = 3,   // CHANNEL SELECT right(Shift: EDIT GROUP, hold to clear)
  BtnParam1       = 8,   // Filter / Spectra          (Shift: COPY)
  BtnParam2       = 9,   // Dyn Filter / Dyn Timb     (Shift: PASTE)
  BtnParam3       = 10,  // Atk/Rate / Hi Decay       (Shift: PANIC)
  BtnParam4       = 11,  // Noise Decay / Tone Decay  (Shift: SYSTEM)
  BtnParam5       = 16,  // Distort / Pitch           (Shift: MIDI)
  BtnParam6       = 17,  // EQ Freq / Bend            (Shift: TRIG TYPE)
  BtnParam7       = 18,  // Repeat / Click Level      (Shift: INP THRES)
  BtnParam8       = 19,  // Level / Mix Tone-Noise    (Shift: INP SENS)
  BtnShift        = 24,  // SHIFT / EXIT
  BtnTrig         = 25,  // TRIG                (Shift: TRIG LEVEL)
  BtnLowerParam   = 26,  // LOWER PARAM         (Shift: REPEAT TEMPO)
};
constexpr int kParamButtons[8] = {BtnParam1, BtnParam2, BtnParam3, BtnParam4, BtnParam5, BtnParam6, BtnParam7, BtnParam8};

// ---- LEDs
// parameter LEDs, left to right: upper row (Noise: Filter, Dyn Filter, Atk/Rate, Decay | Channel: Distort, EQ Freq, Repeat, Level)
constexpr int kLedUpperRow[8] = {21, 0, 2, 8, 10, 16, 18, 24};
// lower row (Tone: Spectra, Dyn Timb, Hi Decay, Decay, Pitch, Bend | Click: Level | Mix: Tone/Noise)
constexpr int kLedLowerRow[8] = {29, 1, 3, 9, 11, 17, 19, 25};
// input channels 1-6
constexpr int kLedPeakFocus[6] = {4, 12, 20, 27, 26, 28};
constexpr int kLedTrigMG[6]    = {6, 14, 15, 23, 22, 30};
constexpr int kLedSoloEdit = 7;
// LEDs 5, 13, 31: not seen lit (probably unconnected)

// ---- analog
constexpr int kAdcMasterLevel = 25;   // MASTER LEVEL pot: digital volume read by the OS (-1 = off, 0 = 12 o'clock, +1 = full)
constexpr int kAdcPad[6] = {12, 2, 4, 6, 8, 10};

// ---- OS v3.00 RAM
constexpr unsigned kRamCurrentProgram = 0x70BC;   // u16: the loaded program, bank * 50 + program
constexpr unsigned kRamGlobalChannel = 0x49D8;    // u8: the global MIDI channel, 0-15 (program changes are received on it)
constexpr unsigned kRamChannelMidi = 0x49DA;      // u16 x 6: each drum channel's MIDI channel, 0-15 (factory 0-5)

}  // namespace nd2::panel
