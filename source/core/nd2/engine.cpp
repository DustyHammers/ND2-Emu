#include "engine.h"
#include "banks.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numeric>

namespace nd2 {

namespace {
constexpr uint32_t kMainLoop = 0x31B54;          // OS v3.00 event loop entry
constexpr uint32_t kAdcPeriod = 5168;            // OS v3.00 ADC configuration: 17 conversions x 304 cycles
constexpr uint64_t kVirtualBacklog = 2496 * 64;  // virtual-port MIDI queued ahead of the line, in cycles
constexpr double kMinButtonHoldS = 0.030;        // shortest press the panel scan + debounce reliably sees
constexpr double kPanelPublishS = 0.020;
constexpr uint32_t kStateMagic = 0x314D5244;     // "DRM1"
constexpr uint32_t kStateVersion = 1;
int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

// ---------------------------------------------------------------- trigger input FIFO (audio thread only)
struct Engine::TrigFifo : Board::AdcSource {
  static constexpr size_t kCap = 1 << 14;
  std::vector<float> ring[kPads];
  size_t head = 0, tail = 0;                     // frames (all pads move together)
  uint64_t* under = nullptr;
  TrigFifo() { for (auto& r : ring) r.assign(kCap, 0.0f); }
  void reset(size_t prefill) { head = tail = 0; for (size_t i = 0; i < prefill && i < kCap - 1; i++) pushFrame(nullptr); }
  size_t size() const { return head - tail; }
  void pushFrame(const float* v) {
    if (size() >= kCap - 1) return;
    for (int p = 0; p < kPads; p++) ring[p][head % kCap] = v ? v[p] : 0.0f;
    head++;
  }
  void next_block(double* add) override {
    if (!size()) { if (under) ++*under; return; }
    for (int p = 0; p < kPads; p++) add[panel::kAdcPad[p]] += ring[p][tail % kCap];
    tail++;
  }
};

Engine::Engine() {
  for (uint32_t i = 0; i < kButtonRing; i++) buttons_[i].seq.store(i, std::memory_order_relaxed);
  for (int p = 0; p < kPads; p++) { trigGain_[p].store(1.0f); clipHold_[p].store(0); }
  for (auto& a : kitBend_) a.store(0.0f);
  hostBytes_.resize(size_t(1) << 20);
  hostEvents_.reserve(8192);
  snap_.reserve(size_t(4) << 20);
  fifo_ = std::make_unique<TrigFifo>();
  fifo_->under = &trigUnder_;
  idle_ = std::thread([this] { idleLoop(); });
}

Engine::~Engine() {
  idleStop_.store(true);
  if (idle_.joinable()) idle_.join();
  cancel_.store(true);
  if (worker_.joinable()) worker_.join();
}

// ---------------------------------------------------------------- jobs (boot / restore on the worker)
bool Engine::setOs(const OsImage& os, std::string& err) {
  if (!isSupportedOs(os)) { err = "unsupported OS (v3.00 required)"; setMessage(err); return false; }
  os_ = os.image; osHash_ = os.hash;
  startJob({});
  return true;
}

void Engine::setBootFlash(std::vector<uint8_t> flash) { bootFlash_ = std::move(flash); }

void Engine::startJob(std::vector<uint8_t> restore) {
  cancel_.store(true);
  if (worker_.joinable()) worker_.join();
  cancel_.store(false);
  if (!live_) state_.store(State::Booting);
  worker_ = std::thread([this, r = std::move(restore)]() mutable { runJob(std::move(r)); });
}

void Engine::waitForJob() { if (worker_.joinable()) worker_.join(); }

void Engine::runJob(std::vector<uint8_t> restore) {
  auto b = std::make_unique<Board>();
  b->load_app(os_);
  b->audio.reserve(size_t(1) << 16);
  bool restored = false;
  if (!restore.empty()) {
    b->boot();
    StateReader r(restore.data(), restore.size());
    r.get<uint32_t>(); r.get<uint32_t>(); r.get<uint64_t>();          // header, checked by setState
    if (b->loadState(r)) restored = true;
    else { setMessage("project state could not be restored; started fresh"); b = std::make_unique<Board>(); b->load_app(os_); }
  }
  if (!restored) {
    auto& p = b->cpu.prog;
    for (uint32_t pc = 0x40000, i = 0; pc < 0x55800; pc += 2, i += 4)
      p[pc >> 1] = (i + 3 < bootFlash_.size()) ? (bootFlash_[i] | (bootFlash_[i + 1] << 8) | (bootFlash_[i + 2] << 16)) : 0xFFFFFF;
    b->boot();
    b->set_adc(panel::kAdcMasterLevel, std::max(-1.0, std::min(0.999, master_.load() * 2.0 - 1.0)));
    uint64_t limit = b->cpu.cycles + uint64_t(3.0 * FCY);
    while (!cancel_.load() && !b->cpu.halted && b->cpu.pc != kMainLoop && b->cpu.cycles < limit)
      b->cpu.run_until_pc(kMainLoop, uint64_t(0.02 * FCY));
    for (int i = 0; i < 5 && !cancel_.load(); i++) b->cpu.run_cycles(uint64_t(0.02 * FCY));   // the OS loads its program
    if (b->cpu.halted || b->cpu.pc == 0) { setMessage("OS stopped during boot: " + b->cpu.fault); state_.store(State::Stopped); return; }
  }
  if (cancel_.load()) return;
  if (!restored) { b->audio.clear(); b->midi_out.clear(); b->midi_out_cycles.clear(); }
  install(std::move(b));
}

void Engine::install(std::unique_ptr<Board> b) {
  std::unique_ptr<Board> old;
  {
    std::lock_guard<std::mutex> l(mtx_);
    old = std::move(live_);
    live_ = std::move(b);
    retime_ = true;
    masterDirty_.store(true);
    state_.store(State::Running);
  }
}

// ---------------------------------------------------------------- state
std::vector<uint8_t> Engine::getState() {
  std::lock_guard<std::mutex> g(getStateMtx_);
  if (state_.load() != State::Running) return {};
  if (nowMs() - lastAudioMs_.load() < 200) {
    const uint32_t req = snapRequest_.fetch_add(1) + 1;
    for (int i = 0; i < 500; i++) {
      if (snapDone_.load(std::memory_order_acquire) == req) return snap_;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  std::lock_guard<std::mutex> l(mtx_);                // host stopped or offline: save directly
  if (!live_) return {};
  StateWriter w;
  w.put(kStateMagic); w.put(kStateVersion); w.put(osHash_);
  live_->saveState(w);
  return w.bytes;
}

bool Engine::setState(const std::vector<uint8_t>& blob, std::string& err) {
  StateReader r(blob.data(), blob.size());
  if (r.get<uint32_t>() != kStateMagic || r.get<uint32_t>() != kStateVersion) { err = "not a saved state of this emulator"; return false; }
  if (r.get<uint64_t>() != osHash_) { err = "the state was saved with a different OS"; return false; }
  startJob(blob);
  return true;
}

std::vector<uint8_t> Engine::stateOf(const Board& b, uint64_t osHash) {
  StateWriter w;
  w.put(kStateMagic); w.put(kStateVersion); w.put(osHash);
  b.saveState(w);
  return std::move(w.bytes);
}

std::unique_ptr<Board> Engine::boardFromState(const std::vector<uint8_t>& osImage, uint64_t osHash,
                                              const std::vector<uint8_t>& blob, std::string& err) {
  StateReader r(blob.data(), blob.size());
  if (r.get<uint32_t>() != kStateMagic || r.get<uint32_t>() != kStateVersion) { err = "not a saved state of this emulator"; return nullptr; }
  if (r.get<uint64_t>() != osHash) { err = "the state was saved with a different OS"; return nullptr; }
  auto b = std::make_unique<Board>();
  b->load_app(osImage);
  b->boot();
  if (!b->loadState(r)) { err = "the state could not be restored"; return nullptr; }
  return b;
}

void Engine::serveSnapshot(Board& b) {
  const uint32_t req = snapRequest_.load(std::memory_order_acquire);
  if (req == snapDone_.load(std::memory_order_relaxed)) return;
  StateWriter w;
  w.bytes.swap(snap_);
  w.bytes.clear();
  w.put(kStateMagic); w.put(kStateVersion); w.put(osHash_);
  b.saveState(w);
  snap_.swap(w.bytes);
  snapDone_.store(req, std::memory_order_release);
}

uint64_t Engine::cyclesForTest() { std::lock_guard<std::mutex> l(mtx_); return live_ ? live_->cpu.cycles : 0; }

// ---------------------------------------------------------------- panel
void Engine::pressButton(int idx, bool down) {
  const uint32_t v = uint32_t(idx & 0xFF) | (down ? 0x100u : 0u);
  uint32_t pos = btnEnqueue_.load(std::memory_order_relaxed);
  for (;;) {
    ButtonCell& c = buttons_[pos & (kButtonRing - 1)];
    const uint32_t seq = c.seq.load(std::memory_order_acquire);
    const int32_t dif = int32_t(seq - pos);
    if (dif == 0) {
      if (btnEnqueue_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
        c.value = v; c.seq.store(pos + 1, std::memory_order_release); return;
      }
    } else if (dif < 0) return;                       // full: edge dropped
    else pos = btnEnqueue_.load(std::memory_order_relaxed);
  }
}

void Engine::applyPanel(Board& b) {
  // circuit-bent mode: the two bends inside the OS, and DAC Bits (the clock bends: updateClock)
  b.bends.setTimeRate(bendmap::timeRate(kitBend_[int(KitBend::Time)].load(std::memory_order_relaxed)));
  b.bends.setSineBit(bendmap::sineBit(kitBend_[int(KitBend::SineBit)].load(std::memory_order_relaxed)));
  dacBits_ = bendmap::dacBits(kitBend_[int(KitBend::DacBits)].load(std::memory_order_relaxed));
  dacKeep_ = dacBits_ >= 32 ? 0xFFFFFFFFu : ~((1u << (32 - dacBits_)) - 1u);
  uint64_t* downAt = downAt_;
  for (;;) {
    ButtonCell& c = buttons_[btnDequeue_ & (kButtonRing - 1)];
    if (int32_t(c.seq.load(std::memory_order_acquire) - (btnDequeue_ + 1)) < 0) break;
    const uint32_t v = c.value;
    c.seq.store(btnDequeue_ + kButtonRing, std::memory_order_release);
    ++btnDequeue_;
    const int idx = int(v & 0x1F); const bool down = (v & 0x100) != 0;
    uint64_t at = b.cpu.cycles;
    if (down) downAt[idx] = at;
    else at = std::max(at, downAt[idx] + uint64_t(kMinButtonHoldS * FCY));   // a click is held long enough to be seen
    b.button_at(idx, down, at);
  }
  // A burst of detents (a fast turn of the UI encoder) is played as a fast spin, which the OS accelerates as it
  // does on the hardware; single detents keep the slow timing (no acceleration).
  if (int d = encoder_.exchange(0)) b.encoder_turn(d, std::abs(d) >= 3 ? 0.004 : 0.010);
  if (const int p = pendingProgram_.exchange(-1); p >= 0) {
    const uint8_t ch = b.cpu.ram[panel::kRamGlobalChannel];
    if (ch < 16) {
      const uint8_t cc = uint8_t(0xB0 | ch), pc = uint8_t(0xC0 | ch);
      const uint8_t msg[8] = {cc, 0, 0, cc, 32, uint8_t(p / 50), pc, uint8_t(p % 50)};
      b.midi_in_at(msg, sizeof msg, b.cpu.cycles);
    }
  }
  if (masterDirty_.exchange(false))
    b.set_adc(panel::kAdcMasterLevel, std::max(-1.0, std::min(0.999, double(master_.load()) * 2.0 - 1.0)));
}

Engine::PanelView Engine::panel() const {
  PanelView v;
  const uint32_t s = segWord_.load();
  for (int i = 0; i < 4; i++) v.seg[size_t(i)] = uint8_t(s >> (8 * i));
  v.leds = leds_.load();
  return v;
}

void Engine::publishPanel(Board& b) {
  if (b.cpu.cycles - panelSince_ < uint64_t(kPanelPublishS * FCY)) return;
  panelSince_ = b.cpu.cycles;
  const auto p = b.panel(true);
  uint32_t s = 0, l = 0;
  for (int i = 0; i < 4; i++) s |= uint32_t(p.seg[i]) << (8 * i);
  bool levels = false;
  for (int i = 0; i < 32; i++) {
    if (p.led_duty[i] > 0.25) l |= 1u << i;
    const uint8_t lv = uint8_t(std::min(8L, std::lround(p.led_duty[i] * 8.0)));
    if (lv != ledLevel_[i].load(std::memory_order_relaxed)) { ledLevel_[i].store(lv, std::memory_order_relaxed); levels = true; }
  }
  if (levels || s != segWord_.load() || l != leds_.load()) { segWord_.store(s); leds_.store(l); uiSerial_.fetch_add(1); }
  const int cur = b.cpu.ld16(panel::kRamCurrentProgram);
  current_.store(cur < kBanks * kBankPrograms ? cur : -1, std::memory_order_relaxed);
  auto field = [](unsigned v) { return uint64_t(v < 16 ? v + 1 : 0); };   // read only: shown on the MIDI page
  uint64_t ch = field(b.cpu.ram[panel::kRamGlobalChannel]);
  for (int c = 0; c < kPads; c++) ch |= field(b.cpu.ld16(uint16_t(panel::kRamChannelMidi + 2 * c))) << (5 * (c + 1));
  midiChannels_.store(ch, std::memory_order_relaxed);
}

// ---------------------------------------------------------------- MIDI
void Engine::virtualMidiIn(const uint8_t* bytes, size_t n) {
  for (size_t i = 0; i < n; i++) if (vinParser_.feed(bytes[i])) vin_.push(vinParser_.data(), vinParser_.size());
}

void Engine::midiOut(Board& b, int n) {
  const bool toHost = hostOutOn_.load(), toPort = virtualOn_.load();
  for (size_t i = 0; i < b.midi_out.size(); i++) {
    if (!outParser_.feed(b.midi_out[i])) continue;
    const size_t sz = outParser_.size();
    if (toHost && hostUsed_ + sz <= hostBytes_.size() && hostEvents_.size() < hostEvents_.capacity()) {
      const double h = double(b.midi_out_cycles[i] - std::min(b.midi_out_cycles[i], c0_)) / cps_ - double(hostPos_);
      const int off = std::max(0, std::min(n - 1, int(h)));
      std::memcpy(&hostBytes_[hostUsed_], outParser_.data(), sz);
      hostEvents_.push_back({off, uint32_t(hostUsed_), uint32_t(sz)});
      hostUsed_ += sz;
    }
    if (toPort) vout_.push(outParser_.data(), sz);
  }
  b.midi_out.clear(); b.midi_out_cycles.clear();
}

// ---------------------------------------------------------------- idle runner
// Every 10 ms: when enabled and no host block came for 250 ms, run the machine for the real time that passed (at
// most 50 ms per step) with the panel and the virtual port, then discard its audio.
void Engine::idleLoop() {
  constexpr int64_t kQuietMs = 250, kStepMs = 10, kMaxStepMs = 50;
  int64_t last = 0;
  while (!idleStop_.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kStepMs));
    const int64_t now = nowMs();
    if (!idleOn_.load() || now - lastAudioMs_.load() < kQuietMs || state_.load() != State::Running) {
      idling_.store(false);
      last = 0;
      continue;
    }
    std::unique_lock<std::mutex> lk(mtx_, std::try_to_lock);
    if (!lk.owns_lock() || !live_) continue;
    Board& b = *live_;
    if (!idling_.exchange(true)) {               // starting: no host audio state to keep
      b.adc_live = nullptr;
      b.stems_on = false;
      outParser_.reset();
    }
    const int64_t ms = last == 0 ? kStepMs : std::min(kMaxStepMs, std::max<int64_t>(0, now - last));
    last = now;
    applyPanel(b);
    while (vin_.peekSize() && b.midi_line_free() < b.cpu.cycles + kVirtualBacklog)
      vin_.pop([&](const uint8_t* p, size_t sz) { b.midi_in_at(p, sz, b.cpu.cycles); });
    if (!b.cpu.halted)
      b.cpu.run_cycles(uint64_t(double(ms) * 1e-3 * FCY * crystal_ * double(frameCycles_) / double(FRAME_CYCLES)));
    b.audio.clear(); audioRead_ = 0;
    b.stems.clear(); stemRead_ = 0;
    const bool toPort = virtualOn_.load();
    for (uint8_t byte : b.midi_out)
      if (outParser_.feed(byte) && toPort) vout_.push(outParser_.data(), outParser_.size());
    b.midi_out.clear(); b.midi_out_cycles.clear();
    publishPanel(b);
    retime_ = true;                              // the next host block starts afresh
  }
}

// ---------------------------------------------------------------- audio
void Engine::prepare(double hostRate, int maxBlock, bool trigIn, bool stems) {
  std::lock_guard<std::mutex> l(mtx_);
  hostRate_ = hostRate; maxBlock_ = std::max(1, maxBlock); trigOn_ = trigIn; stemsOn_ = stems;
  cpsBase_ = FCY / hostRate;
  const int64_t host = std::llround(hostRate);
  RateConverter::Design od;
  od.inHz = FCY / FRAME_CYCLES; od.outHz = double(host);
  od.inStep = 609375; od.outStep = host * 16;                       // 38085.9375 = 609375 / 16
  { const int64_t g = std::gcd(od.inStep, od.outStep); od.inStep /= g; od.outStep /= g; }
  od.taps = 64; od.beta = 8.0; od.passHz = 0.45 * od.inHz;
  outConv_.setup(stemsOn_ ? 2 + kStemChannels : 2, od);           // stems share the mains' converter (same delay)
  RateConverter::Design td;
  td.inHz = double(host); td.outHz = FCY / kAdcPeriod;
  td.inStep = host * kAdcPeriod; td.outStep = int64_t(FCY);
  { const int64_t g = std::gcd(td.inStep, td.outStep); td.inStep /= g; td.outStep /= g; }
  td.taps = 32; td.beta = 6.0; td.passHz = 6000;
  trigConv_.setup(kPads, td);
  uint64_t d = uint64_t(outConv_.lookahead() + 3) * FRAME_CYCLES;
  if (trigOn_) d += uint64_t(std::ceil(trigConv_.lookahead() * cpsBase_)) + 2 * kAdcPeriod;
  D0_ = double(d);
  latency_ = int(std::ceil(D0_ / cpsBase_));
  applyClockScale();
  const size_t nat = size_t(std::ceil(maxBlock_ * od.inHz / hostRate)) + size_t(outConv_.lookahead()) + 256;
  nativeL_.assign(nat, 0.0f); nativeR_.assign(nat, 0.0f);
  for (int k = 0; k < kStemChannels; k++) {
    nativeS_[k].assign(stemsOn_ ? nat : 0, 0.0f);
    stemScratch_[k].assign(stemsOn_ ? size_t(maxBlock_) : 0, 0.0f);
  }
  for (auto& t : trigTmp_) t.assign(size_t(std::ceil(maxBlock_ * td.outHz / hostRate)) + 256, 0.0f);
  retime_ = true;
}

void Engine::retime(Board& b) {
  outConv_.reset(); trigConv_.reset();
  b.frame_cycles = frameCycles_;
  clockBent_ = false;
  vsActive_ = false; vsPos_ = 1.0;
  b.audio.clear(); audioRead_ = 0;
  b.stems_on = stemsOn_; b.stems.clear(); stemRead_ = 0;
  b.midi_out.clear(); b.midi_out_cycles.clear(); outParser_.reset();
  c0_ = b.cpu.cycles; hostPos_ = 0; panelSince_ = c0_;
  if (trigOn_) {
    const uint64_t a0 = b.adc_next_block_cycle();
    const int64_t pre = std::max<int64_t>(0, std::llround(double(int64_t(c0_ + D_) - int64_t(a0)) / kAdcPeriod));
    fifo_->reset(size_t(pre));
    fifoTarget_ = size_t(pre);
    b.adc_live = fifo_.get();
  } else b.adc_live = nullptr;
}

bool Engine::runFrames(Board& b, size_t frames) {
  while ((b.audio.size() - audioRead_) / 2 < frames) {
    if (b.cpu.halted) return false;
    b.cpu.run_cycles(b.frame_cycles);
  }
  return true;
}

// ---------------------------------------------------------------- circuit-bent clock and output
double Engine::random01() {                        // xorshift32
  rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
  return double(rng_) / 4294967296.0;
}

// DAC Bits: only the top bits of the output word reach the converter (truncated toward zero: no offset at silence).
float Engine::nativeSample(int32_t v) const {
  constexpr float k = 1.0f / 2147483648.0f;
  if (dacBits_ >= 32) return float(v) * k;
  const int64_t m = (v >= 0 ? int64_t(v) : -int64_t(v)) & int64_t(dacKeep_);
  return float(v >= 0 ? m : -m) * k;
}

// The machine's clock against real time: cycles per host sample and the injection delay follow it (so MIDI stays on
// the host's beat and the delay stays the same real time).
void Engine::applyClockScale() {
  const double scale = crystal_ * double(frameCycles_) / double(FRAME_CYCLES);
  cps_ = cpsBase_ * scale;
  D_ = uint64_t(std::llround(D0_ * scale));
}

// CPU Starve, Brownouts (cycles per codec frame) and Crystal, once per block. When the clock changes, the host time
// -> machine cycle mapping is re-anchored at this block's start, so it stays continuous.
void Engine::updateClock(Board& b, int n) {
  uint32_t f = bendmap::starveFrameCycles(kitBend_[int(KitBend::Starve)].load(std::memory_order_relaxed));
  const float brown = kitBend_[int(KitBend::Brownouts)].load(std::memory_order_relaxed);
  if (brown > 0) {
    if (brownLeft_ <= 0 && random01() < bendmap::brownoutsPerSecond(brown) * double(n) / hostRate_)
      brownLeft_ = int64_t(hostRate_ * (bendmap::kBrownoutMinS + (bendmap::kBrownoutMaxS - bendmap::kBrownoutMinS) * random01()));
  } else brownLeft_ = 0;
  if (brownLeft_ > 0) { f = std::min(f, bendmap::kBrownoutFrameCycles); brownLeft_ -= n; }
  const double k = bendmap::crystalRatio(kitBend_[int(KitBend::Crystal)].load(std::memory_order_relaxed));
  if (f == frameCycles_ && k == crystal_) return;
  const uint64_t at = cycleOfHost(double(hostPos_));
  frameCycles_ = f; crystal_ = k;
  b.frame_cycles = f;
  applyClockScale();
  c0_ = at > D_ ? at - D_ : 0;
  hostPos_ = 0;
  clockBent_ = true;
}

// The native frames the output converter needs: as they come, or (Crystal) read at the crystal's ratio.
bool Engine::pushNative(Board& b, int64_t need) {
  if (vsActive_ && crystal_ == 1.0) {              // the crystal is back: carry on from the frame the reading reached
    const size_t at = size_t(vsPos_);
    if (stemsOn_ && b.stems.size() >= stemRead_ + size_t(kStemChannels) * at) stemRead_ += size_t(kStemChannels) * at;
    audioRead_ += 2 * at;
    vsActive_ = false;
  }
  const bool vs = crystal_ != 1.0;
  if (vs && !vsActive_) { vsActive_ = true; vsPos_ = 1.0; }
  const size_t frames = vs ? size_t(std::floor(vsPos_ + double(need - 1) * crystal_)) + 3 : size_t(need);
  if (!runFrames(b, frames)) return false;
  const bool haveStems = stemsOn_ && b.stems.size() >= stemRead_ + size_t(kStemChannels) * frames;
  auto frame = [&](size_t j, int c) { return nativeSample(b.audio[audioRead_ + 2 * j + size_t(c)]); };
  auto stem = [&](size_t j, int s) { return nativeSample(b.stems[stemRead_ + size_t(kStemChannels) * j + size_t(s)]); };
  auto cubic = [](float p0, float p1, float p2, float p3, float t) {      // Catmull-Rom
    return p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
  };
  size_t pushed = 0;
  while (pushed < size_t(need)) {
    const size_t chunk = std::min(size_t(need) - pushed, nativeL_.size());
    const float* in[2 + kStemChannels] = {nativeL_.data(), nativeR_.data()};
    if (!vs) {
      for (size_t i = 0; i < chunk; i++) { nativeL_[i] = frame(i, 0); nativeR_[i] = frame(i, 1); }
      if (stemsOn_) {
        for (int s = 0; s < kStemChannels; s++) {
          float* d = nativeS_[s].data();
          if (haveStems) for (size_t i = 0; i < chunk; i++) d[i] = stem(i, s);
          else std::fill(d, d + chunk, 0.0f);       // taps not installed (OS mismatch): silent stems
          in[2 + s] = d;
        }
        if (haveStems) stemRead_ += size_t(kStemChannels) * chunk;
      }
      audioRead_ += 2 * chunk;
    } else {
      double pos = vsPos_;
      for (size_t i = 0; i < chunk; i++, pos += crystal_) {
        const size_t j = size_t(pos);
        const float t = float(pos - double(j));
        nativeL_[i] = cubic(frame(j - 1, 0), frame(j, 0), frame(j + 1, 0), frame(j + 2, 0), t);
        nativeR_[i] = cubic(frame(j - 1, 1), frame(j, 1), frame(j + 1, 1), frame(j + 2, 1), t);
      }
      if (stemsOn_)
        for (int s = 0; s < kStemChannels; s++) {
          float* d = nativeS_[s].data();
          if (haveStems) {
            double q = vsPos_;
            for (size_t i = 0; i < chunk; i++, q += crystal_) {
              const size_t j = size_t(q);
              d[i] = cubic(stem(j - 1, s), stem(j, s), stem(j + 1, s), stem(j + 2, s), float(q - double(j)));
            }
          } else std::fill(d, d + chunk, 0.0f);
          in[2 + s] = d;
        }
      vsPos_ = pos;
      const size_t drop = size_t(vsPos_) - 1;     // keep the frame before the read position
      audioRead_ += 2 * drop;
      if (haveStems) stemRead_ += size_t(kStemChannels) * drop;
      vsPos_ -= double(drop);
    }
    outConv_.push(in, int(chunk));
    pushed += chunk;
  }
  if (audioRead_ > (size_t(1) << 15)) {
    b.audio.erase(b.audio.begin(), b.audio.begin() + long(audioRead_)); audioRead_ = 0;
    if (stemsOn_) { b.stems.erase(b.stems.begin(), b.stems.begin() + long(std::min(stemRead_, b.stems.size()))); stemRead_ = 0; }
  }
  return true;
}

void Engine::process(float* left, float* right, int n, const MidiEvent* events, int numEvents, const float* const* trig,
                     float* const* stemsOut) {
  lastAudioMs_.store(nowMs());
  hostEvents_.clear(); hostUsed_ = 0;
  auto silenceStems = [&] {
    if (stemsOut) for (int k = 0; k < kStemChannels; k++) if (stemsOut[k]) std::fill(stemsOut[k], stemsOut[k] + std::max(0, n), 0.0f);
  };
  std::unique_lock<std::mutex> lk(mtx_);
  if (!live_ || state_.load() != State::Running || n <= 0) {
    std::fill(left, left + std::max(0, n), 0.0f); std::fill(right, right + std::max(0, n), 0.0f);
    silenceStems();
    return;
  }
  Board& b = *live_;
  if (retime_) { retime(b); retime_ = false; }
  applyPanel(b);
  updateClock(b, n);

  // virtual port first (whole messages, at block start, bounded backlog), then host events sample-accurately
  const uint64_t tBlock = std::max(b.cpu.cycles, cycleOfHost(double(hostPos_)));
  while (vin_.peekSize() && b.midi_line_free() < tBlock + kVirtualBacklog)
    vin_.pop([&](const uint8_t* p, size_t sz) { b.midi_in_at(p, sz, tBlock); });
  for (int i = 0; i < numEvents; i++) {
    uint64_t at = cycleOfHost(double(hostPos_ + events[i].offset));
    if (at < b.cpu.cycles) { at = b.cpu.cycles; ++lateMidi_; }
    b.midi_in_at(events[i].data, size_t(events[i].size), at);
    lastMidiCycle_ = at;
  }

  // trigger inputs: host rate -> ADC scan rate -> FIFO read by the emulated ADC
  if (trigOn_) {
    float gains[kPads];
    for (int p = 0; p < kPads; p++) gains[p] = trigGain_[p].load();
    trigConv_.push(trig, n);
    const int64_t avail = std::min<int64_t>(trigConv_.available(), int64_t(trigTmp_[0].size()));
    float* outs[kPads]; for (int p = 0; p < kPads; p++) outs[p] = trigTmp_[p].data();
    trigConv_.produce(outs, int(avail));
    for (int64_t k = 0; k < avail; k++) {
      float v[kPads];
      for (int p = 0; p < kPads; p++) {
        v[p] = std::max(-1.0f, std::min(1.0f, trigTmp_[p][size_t(k)] * gains[p]));
        if (std::fabs(v[p]) >= 0.999f) clipHold_[p].store(int(0.3 * FCY / kAdcPeriod));
      }
      fifo_->pushFrame(v);
    }
    // a bent clock reads the trigger inputs at another rate: keep their delay from growing
    if (clockBent_ && fifo_->size() > fifoTarget_ + size_t(avail) + 64) fifo_->tail = fifo_->head - (fifoTarget_ + size_t(avail) + 64);
    for (int p = 0; p < kPads; p++) { int h = clipHold_[p].load(); if (h > 0) clipHold_[p].store(std::max(0, h - int(avail))); }
  }

  // run the machine until the converter has the native frames it needs
  const int64_t need = outConv_.inputNeeded(n);
  if (need > 0 && !pushNative(b, need)) {
    state_.store(State::Stopped); setMessage("the OS stopped: " + b.cpu.fault);
    std::fill(left, left + n, 0.0f); std::fill(right, right + n, 0.0f);
    silenceStems();
    return;
  }
  float* out[2 + kStemChannels] = {left, right};
  if (stemsOn_) for (int s = 0; s < kStemChannels; s++) {
    float* dst = stemsOut ? stemsOut[s] : nullptr;
    if (!dst || n > int(stemScratch_[s].size())) { if (int(stemScratch_[s].size()) < n) stemScratch_[s].resize(size_t(n)); dst = stemScratch_[s].data(); }
    out[2 + s] = dst;
  }
  outConv_.produce(out, n);

  midiOut(b, n);
  hostPos_ += n;
  publishPanel(b);
  serveSnapshot(b);
}

}  // namespace nd2
