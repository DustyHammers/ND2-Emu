#include "midi_stream.h"

namespace nd2 {

int MidiParser::dataBytes(uint8_t s) {
  if (s < 0x80) return -2;
  if (s < 0xF0) { const uint8_t hi = s & 0xF0; return (hi == 0xC0 || hi == 0xD0) ? 1 : 2; }
  switch (s) {
  case 0xF0: return -1;
  case 0xF1: case 0xF3: return 1;
  case 0xF2: return 2;
  default: return 0;   // F4-F7 and real-time F8-FF
  }
}

bool MidiParser::feed(uint8_t b) {
  if (b >= 0xF8) { out_.assign(1, b); return true; }               // real-time: own message, even inside another
  if (inSysex_) {
    if (b == 0xF7) { buf_.push_back(b); inSysex_ = false; out_.swap(buf_); buf_.clear(); return true; }
    if (b >= 0x80) { ++dropped_; inSysex_ = false; buf_.clear(); /* fall through: new status */ }
    else { if (buf_.size() >= max_) { ++dropped_; inSysex_ = false; buf_.clear(); return false; } buf_.push_back(b); return false; }
  }
  if (b >= 0x80) {
    if (!buf_.empty()) ++dropped_;                                // incomplete message dropped
    buf_.clear();
    if (b == 0xF0) { inSysex_ = true; buf_.push_back(b); running_ = 0; return false; }
    if (b == 0xF7 || b == 0xF4 || b == 0xF5) { running_ = 0; return false; }
    const int n = dataBytes(b);
    if (b < 0xF0) running_ = b; else running_ = 0;
    if (n == 0) { out_.assign(1, b); return true; }               // F6
    buf_.push_back(b); need_ = n;
    return false;
  }
  // data byte
  if (buf_.empty()) {
    if (!running_) { ++dropped_; return false; }
    buf_.push_back(running_); need_ = dataBytes(running_);
  }
  buf_.push_back(b);
  if (int(buf_.size()) - 1 >= need_) { out_.swap(buf_); buf_.clear(); return true; }
  return false;
}

void MessageRing::copyIn(size_t pos, const uint8_t* p, size_t n) {
  const size_t cap = buf_.size();
  for (size_t i = 0; i < n; ++i) buf_[(pos + i) % cap] = p[i];
}
void MessageRing::copyOut(size_t pos, uint8_t* p, size_t n) const {
  const size_t cap = buf_.size();
  for (size_t i = 0; i < n; ++i) p[i] = buf_[(pos + i) % cap];
}

bool MessageRing::push(const uint8_t* data, size_t size) {
  const size_t h = head_.load(std::memory_order_relaxed), t = tail_.load(std::memory_order_acquire);
  if (size == 0 || h - t + 4 + size > buf_.size()) return false;
  const uint32_t len = uint32_t(size);
  uint8_t lb[4] = {uint8_t(len), uint8_t(len >> 8), uint8_t(len >> 16), uint8_t(len >> 24)};
  copyIn(h, lb, 4);
  copyIn(h + 4, data, size);
  head_.store(h + 4 + size, std::memory_order_release);
  return true;
}

size_t MessageRing::peekSize() const {
  const size_t t = tail_.load(std::memory_order_relaxed), h = head_.load(std::memory_order_acquire);
  if (h == t) return 0;
  uint8_t lb[4]; copyOut(t, lb, 4);
  return size_t(lb[0]) | size_t(lb[1]) << 8 | size_t(lb[2]) << 16 | size_t(lb[3]) << 24;
}

}  // namespace nd2
