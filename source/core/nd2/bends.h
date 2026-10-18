// Circuit-bent mode (an emulator extension, not in the original hardware): six bends of the whole emulated drum, each
// with an amount from 0 (Off) to 1, chosen by the owner by ear (docs/BENDS.md). With every amount at 0 the machine runs
// exactly as without this file: the hooks change nothing and the clocks keep their normal values.
//
//   CPU Starve      the chip gets fewer CPU cycles per codec frame (normal 2048): below about 1700 the OS's render
//                   falls behind and the codec replays old frames (stutter, buzz); the OS's own timers slow down
//   Brownouts       random dips (20-100 ms) of the supply: CPU Starve at 700 cycles for the dip
//   Time            the OS's control scheduler (envelopes, decays, pitch bends, LFOs) runs on fewer passes: decays
//                   last longer with the pitch unchanged, up to a freeze at the top of the pot
//   Sine Stuck Bit  a data line of the 2048-word sine table's RAM stuck high (stepped: bit 8 ... 15)
//   Crystal         the 16 MHz crystal slowed (down to -50 %): pitch and every timing of the drum go down together
//   DAC Bits        the converter's data lines: only the top bits of the output words reach it (stepped: 12 ... 4 bits,
//                   truncated toward zero, so no offset at silence)
//
// Where: Time and Sine Stuck Bit act inside the OS v3.00 (this class; the program words at the hook sites are checked
// before anything is installed). CPU Starve and Brownouts set Board::frame_cycles; Crystal and DAC Bits are in the
// Engine (the machine's clock against real time, and the output words). Nothing here is part of the saved machine
// state; the plugin saves the amounts as its parameters.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dspic { class CPU; }

namespace nd2 {

class Board;

enum class KitBend : int { Starve = 0, Brownouts, Time, SineBit, Crystal, DacBits };
constexpr int kKitBends = 6;
constexpr int kBendSteps = 9;                      // the stepped bends: Off + 8 steps

namespace bendmap {
inline bool stepped(int bend) { return bend == int(KitBend::SineBit) || bend == int(KitBend::DacBits); }
inline double clamp01(double a) { return a > 0 ? std::min(a, 1.0) : 0.0; }
inline int step(double a) { return int(std::lround(clamp01(a) * (kBendSteps - 1))); }
constexpr uint32_t kNormalFrameCycles = 2048;
constexpr uint32_t kBrownoutFrameCycles = 700;
constexpr double kBrownoutMinS = 0.020, kBrownoutMaxS = 0.100;
// CPU Starve: cycles per codec frame, 1750 (just above where it starts to break up) ... 700
inline uint32_t starveFrameCycles(double a) {
  return a > 0 ? uint32_t(std::lround(1750.0 - 1050.0 * clamp01(a))) : kNormalFrameCycles; }
// Brownouts: dips per second, 0.7 ... 15.4
inline double brownoutsPerSecond(double a) { return a > 0 ? 0.7 * std::pow(22.0, clamp01(a)) : 0.0; }
// Time: the share of the scheduler's passes that run, 1 (normal) ... 1/32 ... 0 (freeze at 1)
inline double timeRate(double a) { return a <= 0 ? 1.0 : a >= 1 ? 0.0 : std::pow(2.0, -5.0 * a); }
// Sine Stuck Bit: -1 (Off), 8 ... 15
inline int sineBit(double a) { const int s = step(a); return s ? 7 + s : -1; }
// Crystal: the clock against its normal speed, 1 ... 0.5
inline double crystalRatio(double a) { return a > 0 ? 1.0 - 0.5 * clamp01(a) : 1.0; }
// DAC Bits: the bits of the 32-bit output words that reach the converter, 32 (Off), 12, 10, 9, 8, 7, 6, 5, 4
inline int dacBits(double a) { static const int bits[kBendSteps] = {32, 12, 10, 9, 8, 7, 6, 5, 4}; return bits[step(a)]; }
}  // namespace bendmap

// The two bends inside the OS.
class Bends {
public:
  // Installs the hooks (from Board::boot). False if the OS's code isn't the expected one (then nothing is installed
  // and the setters have no effect).
  bool install(Board& b);
  bool installed() const { return installed_; }
  // Audio thread, between blocks.
  void setTimeRate(double rate);                   // 1 = normal, 0 = freeze (see bendmap::timeRate)
  void setSineBit(int bit);                        // -1 = Off, 0-15
  double timeRate() const { return rate_; }
  int sineBit() const { return sineBit_; }

private:
  static void hookScheduler(dspic::CPU& c, void* ctx);
  static void hookLoopHead(dspic::CPU& c, void* ctx);
  dspic::CPU* cpu_ = nullptr;
  bool installed_ = false;
  double rate_ = 1.0, phase_ = 0.0;
  int sineBit_ = -1;
  bool restore_ = false;                           // a skipped pass: the scheduler's counter to put back
  uint16_t counter_ = 0;
};

}  // namespace nd2
