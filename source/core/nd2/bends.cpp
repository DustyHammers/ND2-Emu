#include "bends.h"
#include "board.h"
#include <cstdlib>
#include <initializer_list>

namespace nd2 {

namespace {
// OS v3.00 code at the hook sites (checked before anything is installed)
constexpr uint32_t kDispatch = 0x1A68C, kDispatchStep = 0x90;          // goto W0 (tone engine of channel ch)
constexpr uint32_t kGotoW0 = 0x010400, kReturnWord = 0x2DE020, kReturnStep = 0xA0;   // mov.w #0xde02+..,W0 after it
constexpr uint32_t kNoiseA = 0x1A4D8, kNoiseB = 0x1A4DC, kMix = 0x1A9EE, kScheduler = 0x1ACBC;
constexpr uint32_t kTrampolines = 0x3F00;                             // unused program memory (the boot loader's area)
constexpr uint16_t kNoiseWords = 0xDDBA, kSignals = 0x1E8E;
constexpr uint16_t kSine = 0x39B8;
constexpr uint32_t kCrackleThreshold = 6442451u;                      // 0.15 % of 2^32

// Sine bend: address lines 2 and 9 of the word index swapped (an involution).
struct SinePerm {
  uint16_t map[2048];
  SinePerm() {
    for (int i = 0; i < 2048; i++) {
      const int b2 = (i >> 2) & 1, b9 = (i >> 9) & 1;
      map[i] = uint16_t((i & ~0x204) | (b9 << 2) | (b2 << 9));
    }
  }
};
const SinePerm kSinePerm;

bool wordsAt(const dspic::CPU& c, uint32_t pc, std::initializer_list<uint32_t> words) {
  for (uint32_t w : words) { if (c.progword(pc) != w) return false; pc += 2; }
  return true;
}
int32_t towardZero(int32_t x, uint32_t clearMask) {
  if (x >= 0) return int32_t(uint32_t(x) & ~clearMask);
  return -int32_t(uint32_t(-int64_t(x)) & ~clearMask);
}
}  // namespace

bool Bends::install(Board& b) {
  if (installed_) return true;                            // hooks stay installed across a second boot of the board
  auto& c = b.cpu;
  for (int k = 0; k < kBendChannels; k++) {
    if (!wordsAt(c, kDispatch + kDispatchStep * k, {kGotoW0, kReturnWord + kReturnStep * k})) return false;
    if (!wordsAt(c, kTrampolines + 4 * k, {0xFFFFFF, 0xFFFFFF})) return false;
  }
  if (!wordsAt(c, kNoiseA - 2, {0x682D1A, 0xC39EE6, 0x682D1A, 0xC51EE7})) return false;
  if (!wordsAt(c, kMix, {0xC7184A}) || !wordsAt(c, kScheduler, {0xEC316C, 0xAF116C})) return false;
  for (int k = 0; k < kBendChannels; k++) {                // trampoline k: goto the return of channel k's dispatch
    const uint32_t a = kTrampolines + 4 * k, ret = kDispatch + kDispatchStep * k + 2;
    c.prog[a >> 1] = 0x040000 | (ret & 0xFFFE);
    c.prog[(a >> 1) + 1] = (ret >> 16) & 0x7F;
    c.invalidate(a); c.invalidate(a + 2);
    dispatchCtx_[size_t(k)] = DispatchCtx{this, k};
    c.add_hook(kDispatch + kDispatchStep * k, hookDispatch, &dispatchCtx_[size_t(k)]);
    c.add_hook(kDispatch + kDispatchStep * k + 2, hookReturn, &dispatchCtx_[size_t(k)]);
  }
  c.add_hook(kNoiseA, hookNoiseA, this);
  c.add_hook(kNoiseB, hookNoiseB, this);
  c.add_hook(kMix, hookMix, this);
  c.add_hook(kScheduler, hookScheduler, this);
  installed_ = true;
  return true;
}

void Bends::set(int channel, int bend) {
  if (channel < 0 || channel >= kBendChannels) return;
  const Bend m = bend > 0 && bend < kBendCount ? Bend(bend) : Bend::Off;
  const size_t k = size_t(channel);
  if (!installed_ || mode_[k] == m) return;
  mode_[k] = m;
  held_[k] = 0; holdCount_[k] = 0; underCount_[k] = 0; stretchCount_[k] = 0; envValid_[k] = false;
  noiseX_[k] = uint32_t(channel + 1);
  any_ = false;
  for (Bend x : mode_) any_ = any_ || x != Bend::Off;
}

uint32_t Bends::random() {                                // xorshift32
  rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
  return rng_;
}

uint16_t Bends::nextNoise(int ch) {                       // the OS's generator with multiplier bit 0 cleared
  uint32_t& x = noiseX_[size_t(ch)];
  x = x * 0x0BB38434u + 0x3619636Bu;
  return uint16_t(x >> 16);
}

void Bends::hookNoiseA(dspic::CPU& c, void* ctx) { static_cast<Bends*>(ctx)->onNoiseA(c); }
void Bends::hookNoiseB(dspic::CPU& c, void* ctx) { static_cast<Bends*>(ctx)->onNoiseB(c); }
void Bends::hookDispatch(dspic::CPU& c, void* ctx) { auto* d = static_cast<DispatchCtx*>(ctx); d->bends->onDispatch(c, d->ch); }
void Bends::hookReturn(dspic::CPU& c, void* ctx) { auto* d = static_cast<DispatchCtx*>(ctx); d->bends->onReturn(c, d->ch); }
void Bends::hookMix(dspic::CPU& c, void* ctx) { static_cast<Bends*>(ctx)->onMix(c); }
void Bends::hookScheduler(dspic::CPU& c, void* ctx) { static_cast<Bends*>(ctx)->onScheduler(c); }

// Before channel 0's noise prefetch: noise words 0-4 are written (word 0 is also the generator's state, high word).
void Bends::onNoiseA(dspic::CPU& c) {
  if (!any_) return;
  for (int k = 0; k < 5; k++) {
    noiseOrig_[size_t(k)] = c.ld16(kNoiseWords + 2 * k);
    if (mode_[size_t(k)] == Bend::Noise) c.st16(kNoiseWords + 2 * k, nextNoise(k));
  }
}

// After channel 0's prefetch and the last xor (word 5 = generator low word ^ word 4): the generator state back,
// word 5 from the unbent word 4, then channel 5's own noise.
void Bends::onNoiseB(dspic::CPU& c) {
  if (!any_) return;
  if (mode_[0] == Bend::Noise) c.st16(kNoiseWords, noiseOrig_[0]);
  if (mode_[4] == Bend::Noise) c.st16(kNoiseWords + 10, uint16_t(c.W(0) ^ noiseOrig_[4]));
  if (mode_[5] == Bend::Noise) c.st16(kNoiseWords + 10, nextNoise(5));
}

// Channel ch's tone engine is dispatched: the sine reads of its code are remapped (until the next channel's
// dispatch or the mix); an underclocked engine is skipped every other sample (goto W0 -> trampoline -> return).
void Bends::onDispatch(dspic::CPU& c, int ch) {
  if (!any_) { c.sine_remap = nullptr; return; }
  const Bend m = mode_[size_t(ch)];
  c.sine_remap = m == Bend::Sine ? kSinePerm.map : nullptr;
  if (m == Bend::Underclock && (underCount_[size_t(ch)]++ & 1) && engineOutValid_[size_t(ch)]) {
    // The engines run as a chain: each leaves W8/W10 (and W12/W13) where the next one starts reading. A skipped
    // engine leaves the registers as it did at its last return, so the next engines read where they should. (The
    // time it saves still lets the chip's interrupts land a little earlier: see bends.h.)
    const auto& o = engineOut_[size_t(ch)];
    for (int r = 1; r <= 14; r++) c.setW(r, o[size_t(r - 1)]);
    c.setW(0, uint16_t(kTrampolines + 4 * ch));
  }
}

// Channel ch's tone engine has returned: remember the registers it leaves for the next engine.
void Bends::onReturn(dspic::CPU& c, int ch) {
  if (!any_) { engineOutValid_[size_t(ch)] = false; return; }
  auto& o = engineOut_[size_t(ch)];
  for (int r = 1; r <= 14; r++) o[size_t(r - 1)] = c.W(r);
  engineOutValid_[size_t(ch)] = true;
}

// The mix is about to read the six finished signals (32-bit, high word first): the signal bends.
void Bends::onMix(dspic::CPU& c) {
  c.sine_remap = nullptr;
  if (!any_) return;
  for (int k = 0; k < kBendChannels; k++) {
    const Bend m = mode_[size_t(k)];
    if (m != Bend::Crush && m != Bend::Hold && m != Bend::Stuck && m != Bend::Crackle) continue;
    const uint16_t a = uint16_t(kSignals + 4 * k);
    int32_t x = int32_t((uint32_t(c.ld16(a)) << 16) | c.ld16(a + 2));
    switch (m) {
    case Bend::Crush: x = towardZero(x, (1u << 24) - 1); break;                       // keep 8 bits
    case Bend::Hold: if (holdCount_[size_t(k)]++ % 8 == 0) held_[size_t(k)] = x; x = held_[size_t(k)]; break;
    case Bend::Stuck: x = towardZero(x, 1u << 25); break;
    case Bend::Crackle: if (random() < kCrackleThreshold) x ^= int32_t(1u << (26 + random() % 6)); break;
    default: break;
    }
    c.st16(a, uint16_t(uint32_t(x) >> 16));
    c.st16(a + 2, uint16_t(x));
  }
}

// Once per pass, before the control scheduler: a stretched channel's envelope words are held back on 2 passes of 3
// when they only took a small step (a note start is a big jump and always goes through).
void Bends::onScheduler(dspic::CPU& c) {
  if (!any_) return;
  for (int k = 0; k < kBendChannels; k++) {
    if (mode_[size_t(k)] != Bend::Stretch) continue;
    const uint16_t words[3] = {uint16_t(0xD090 + 8 * k), uint16_t(0x1D28 + 6 * k), uint16_t(0x1D2A + 6 * k)};
    const bool skip = stretchCount_[size_t(k)]++ % 3 != 0;
    auto& keep = envKeep_[size_t(k)];
    for (int i = 0; i < 3; i++) {
      uint16_t cur = c.ld16(words[i]);
      if (skip && envValid_[size_t(k)]) {
        const int d = int16_t(uint16_t(cur - keep[size_t(i)]));
        if (d != 0 && std::abs(d) <= 0x400) { c.st16(words[i], keep[size_t(i)]); cur = keep[size_t(i)]; }
      }
      keep[size_t(i)] = cur;
    }
    envValid_[size_t(k)] = true;
  }
}

}  // namespace nd2
