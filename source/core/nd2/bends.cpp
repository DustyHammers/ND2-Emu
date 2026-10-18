#include "bends.h"
#include "board.h"
#include <initializer_list>

namespace nd2 {

namespace {
// OS v3.00 code at the hook sites (checked before anything is installed)
constexpr uint32_t kScheduler = 0x1ACBC;   // inc.w 0x116C; then btsc.b 0x116C,#n / goto <group n> for n = 0 ... 7
constexpr uint32_t kLoopHead = 0x343E0;    // the render loop's head: every scheduler group ends with goto here
constexpr uint16_t kCounter = 0x116C;
// Counter value whose increment selects a group with no tasks: 3 + 1 = 4, lowest set bit 2 (`goto 0x343E0`).
constexpr uint16_t kIdleCounter = 3;

bool wordsAt(const dspic::CPU& c, uint32_t pc, std::initializer_list<uint32_t> words) {
  for (uint32_t w : words) { if (c.progword(pc) != w) return false; pc += 2; }
  return true;
}
}  // namespace

bool Bends::install(Board& b) {
  if (installed_) return true;                     // hooks stay installed across a second boot of the board
  auto& c = b.cpu;
  if (!wordsAt(c, kScheduler, {0xEC316C, 0xAF116C, 0x049104, 0x000001, 0xAF316C, 0x04927A, 0x000001, 0xAF516C,
                               0x0443E0})) return false;
  if (!wordsAt(c, kLoopHead, {0x8058A1})) return false;
  cpu_ = &c;
  c.add_hook(kScheduler, hookScheduler, this);
  c.add_hook(kLoopHead, hookLoopHead, this);
  installed_ = true;
  return true;
}

void Bends::setTimeRate(double rate) {
  rate = std::max(0.0, std::min(1.0, rate));
  if (!installed_ || rate == rate_) return;
  if (rate_ >= 1.0) phase_ = 0.0;                  // starting: the first pass is skipped
  rate_ = rate;
}

void Bends::setSineBit(int bit) {
  if (bit < -1 || bit > 15) bit = -1;
  if (!installed_ || bit == sineBit_) return;
  sineBit_ = bit;
  cpu_->sine_or = bit < 0 ? 0 : uint16_t(1u << bit);
}

// Time: before the scheduler's `inc.w 0x116C`. A pass to skip gets a counter whose increment selects the empty group,
// so no task runs and the scheduler goes straight back to the loop head, where the real counter is put back: the
// skipped pass leaves no trace but the time it didn't take.
void Bends::hookScheduler(dspic::CPU& c, void* ctx) {
  auto* s = static_cast<Bends*>(ctx);
  if (s->restore_) { c.st16(kCounter, s->counter_); s->restore_ = false; }
  if (s->rate_ >= 1.0) return;
  s->phase_ += s->rate_;
  if (s->phase_ >= 1.0) { s->phase_ -= 1.0; return; }
  s->counter_ = c.ld16(kCounter);
  c.st16(kCounter, kIdleCounter);
  s->restore_ = true;
}

void Bends::hookLoopHead(dspic::CPU& c, void* ctx) {
  auto* s = static_cast<Bends*>(ctx);
  if (s->restore_) { c.st16(kCounter, s->counter_); s->restore_ = false; }
}

}  // namespace nd2
