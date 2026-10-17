// Circuit-bent mode (an emulator extension, not in the original hardware): per drum channel, one of eight bends of
// the emulated hardware, chosen by the owner by ear (docs/BENDS.md). With every channel at Off the machine runs
// exactly as without this file (the hooks change nothing then). Each bend changes only its own channel, byte for byte,
// except Underclock: the CPU time a skipped engine saves lets the chip's interrupts land a little earlier, and tone
// models whose noise depends on that timing (e.g. a hi-hat model) then get different noise details (same level and
// spectrum within about 1 dB; measured, see docs/BENDS.md).
//
//   1 Crush       the channel's finished signal keeps 8 bits (truncated toward zero)
//   2 Hold        the channel's finished signal held for 8 samples (sample-and-hold)
//   3 Stuck       bit 25 of the channel's finished signal held low (toward zero: no offset at silence)
//   4 Crackle     a random high bit (26-31) of the channel's signal flipped, 0.15 % of samples
//   5 Underclock  the channel's tone engine runs 1 sample in 2
//   6 Stretch     the channel's envelopes advance 1 step in 3 (decays 3x longer, pitch unchanged)
//   7 Sine        the channel's tone engine reads the 2048-word sine table with address lines 2 and 9 swapped
//   8 Noise       the channel's noise comes from its own generator with multiplier bit 0 cleared (metallic)
//
// Where in the OS (v3.00), all found by tracing and checked by the program words before any hook is installed:
//   - render pass: noise generator words 0xDDBA + 2ch, prefetched by the noise filters at 0x1A4D8 ... 0x1A4E8;
//     tone-engine dispatch `goto W0` at 0x1A68C + 0x90ch returning to +2; the mix reads the six finished signals
//     (32-bit, high word first) at 0x1E8E + 4ch from 0x1A9EE;
//   - control scheduler (once per pass, `inc.w 0x116C` at 0x1ACBC): the main loudness envelope 0xD090 + 8ch and the
//     noise envelope 0x1D28 / 0x1D2A + 6ch;
//   - sine table 0x39B8 (built at boot), read by the tone engines as [W9+W12], [W11+W12] (DSP prefetch) or
//     [Wb+Wn] (register offset); the CPU remaps those reads while a Sine channel's code runs (CPU::sine_remap).
// Settings are applied by the audio thread between blocks. Nothing here is part of the saved machine state: the
// hooks keep their counters and generators outside it, and the sine table in RAM is never changed.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace dspic { class CPU; }

namespace nd2 {

class Board;

enum class Bend : uint8_t { Off = 0, Crush, Hold, Stuck, Crackle, Underclock, Stretch, Sine, Noise };
constexpr int kBendCount = 9;                    // Off + 8
constexpr int kBendChannels = 6;

class Bends {
public:
  // Installs the hooks (from Board::boot). False if the OS's code isn't the expected one (then nothing is installed
  // and set() has no effect).
  bool install(Board& b);
  bool installed() const { return installed_; }
  // Audio thread, between blocks. channel 0-5, bend 0-8 (out of range = Off).
  void set(int channel, int bend);
  Bend get(int channel) const { return channel >= 0 && channel < kBendChannels ? mode_[size_t(channel)] : Bend::Off; }
  bool any() const { return any_; }

private:
  static void hookNoiseA(dspic::CPU& c, void* ctx);
  static void hookNoiseB(dspic::CPU& c, void* ctx);
  static void hookDispatch(dspic::CPU& c, void* ctx);
  static void hookReturn(dspic::CPU& c, void* ctx);
  static void hookMix(dspic::CPU& c, void* ctx);
  static void hookScheduler(dspic::CPU& c, void* ctx);
  void onNoiseA(dspic::CPU& c);
  void onNoiseB(dspic::CPU& c);
  void onDispatch(dspic::CPU& c, int ch);
  void onReturn(dspic::CPU& c, int ch);
  void onMix(dspic::CPU& c);
  void onScheduler(dspic::CPU& c);
  uint32_t random();
  uint16_t nextNoise(int ch);
  struct DispatchCtx { Bends* bends; int ch; };
  std::array<DispatchCtx, kBendChannels> dispatchCtx_{};
  std::array<Bend, kBendChannels> mode_{};
  bool any_ = false, installed_ = false;
  // per-channel runtime (reset when a channel's bend changes)
  std::array<int32_t, kBendChannels> held_{};
  std::array<uint32_t, kBendChannels> holdCount_{}, underCount_{}, stretchCount_{}, noiseX_{};
  std::array<std::array<uint16_t, 3>, kBendChannels> envKeep_{};
  std::array<bool, kBendChannels> envValid_{};
  std::array<uint16_t, 5> noiseOrig_{};
  // the registers each tone engine leaves for the next one (W1-W14), as at its last return
  std::array<std::array<uint16_t, 14>, kBendChannels> engineOut_{};
  std::array<bool, kBendChannels> engineOutValid_{};
  uint32_t rng_ = 0x2545F491u;
};

}  // namespace nd2
