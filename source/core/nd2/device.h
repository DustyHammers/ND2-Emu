// ND2 device: the unit the plugin/standalone talks to.
// Owns the emulated board; MIDI in -> render audio frames at the native rate (Fcy/2048 = 38085.9375 Hz).
#pragma once
#include "board.h"
#include "rom.h"
#include "panel_map.h"
#include <memory>
#include <algorithm>

namespace nd2 {

constexpr double kNativeRate = FCY / FRAME_CYCLES;   // 38085.9375 Hz
constexpr uint32_t kFlashStart = 0x40000, kFlashEnd = 0x55800;   // program addresses of the preset/settings flash

class Device {
public:
  Device();
  bool setOs(const OsImage& os, std::string& err);
  // flash image: 4 bytes per instruction word (lo, mid, hi, 0), kFlashStart..kFlashEnd; empty = erased
  void setFlash(const std::vector<uint8_t>& img);
  std::vector<uint8_t> getFlash() const;
  void boot();                                // reset and run until the OS main loop is idle

  void midiIn(const uint8_t* data, size_t n) { board().midi_in(std::vector<uint8_t>(data, data + n)); }
  std::vector<uint8_t> takeMidiOut() { auto v = std::move(board().midi_out); board().midi_out.clear(); return v; }

  // Trigger inputs: pad 1..6 (ADC AN12, AN2, AN4, AN6, AN8, AN10). `strength` = piezo peak relative to ADC full scale;
  // with default settings the OS triggers from about 0.25 and output level rises with strength.
  void padHit(int pad, double strength) {
    if (pad >= 1 && pad <= 6) board().piezo_hit(panel::kAdcPad[pad - 1], strength, 400.0, 4.0);
  }
  // MASTER LEVEL knob, 0..1 (the OS reads it through the ADC; audio taper: 0.5 = about -12 dB from full, 0 = off)
  void setMasterLevel(double pos) { board().set_adc(panel::kAdcMasterLevel, std::max(-1.0, std::min(0.999, pos * 2.0 - 1.0))); }
  // Front panel
  void setButton(int idx, bool down) { board().set_button(idx, down); }
  void turnEncoder(int detents) { board().encoder_turn(detents); }
  Board::PanelState panel() { return board().panel(true); }

  // Run the emulation until `frames` stereo frames are produced. Output is the codec's 32-bit word as float (+-1.0).
  void render(float* left, float* right, size_t frames);
  void runSeconds(double s);                  // run without collecting audio (e.g. for SysEx transfers)

  Board& board() { return *b; }
private:
  std::unique_ptr<Board> b;
  std::vector<uint8_t> app;
  size_t audio_read = 0;
};

}  // namespace nd2
