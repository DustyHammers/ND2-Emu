// MIDI byte streams as whole messages (same design as the owner's other emulator project):
// MidiParser turns a byte stream into complete messages (running status expanded, SysEx kept whole, real-time bytes
// as their own one-byte messages); MessageRing is a single-producer single-consumer ring of whole messages.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace nd2 {

class MidiParser {
public:
  explicit MidiParser(size_t maxMessage = size_t(1) << 20) : max_(maxMessage) { buf_.reserve(256); }
  bool feed(uint8_t b);                          // true when a message is complete: data()/size() until the next feed
  const uint8_t* data() const { return out_.data(); }
  size_t size() const { return out_.size(); }
  void reset() { buf_.clear(); running_ = 0; need_ = 0; inSysex_ = false; }
  uint64_t dropped() const { return dropped_; }
  static int dataBytes(uint8_t status);          // -1 SysEx, -2 not a status byte
private:
  size_t max_;
  std::vector<uint8_t> buf_, out_;
  uint8_t running_ = 0;
  int need_ = 0;
  bool inSysex_ = false;
  uint64_t dropped_ = 0;
};

class MessageRing {
public:
  explicit MessageRing(size_t capacityBytes) : buf_(capacityBytes) { tmp_.reserve(capacityBytes); }
  bool push(const uint8_t* data, size_t size);   // producer; false (nothing written) if it does not fit
  size_t peekSize() const;                       // consumer; 0 if empty
  template <typename F> void pop(F&& f) {        // consumer: f(const uint8_t*, size_t)
    const size_t n = peekSize();
    if (!n) return;
    tmp_.resize(n);
    size_t t = tail_.load(std::memory_order_relaxed);
    copyOut(t + 4, tmp_.data(), n);
    f(tmp_.data(), n);
    tail_.store(t + 4 + n, std::memory_order_release);
  }
  size_t usedBytes() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire); }
  void clear() { tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release); }
private:
  std::vector<uint8_t> buf_, tmp_;
  std::atomic<size_t> head_{0}, tail_{0};
  void copyIn(size_t pos, const uint8_t* p, size_t n);
  void copyOut(size_t pos, uint8_t* p, size_t n) const;
};

}  // namespace nd2
