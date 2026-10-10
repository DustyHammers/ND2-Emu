// Binary state streams for saving and restoring the machine (plugin project state).
// Plain little-endian copies of fixed-size values (macOS arm64 and Linux x86-64 are both little-endian).
// Same design as the owner's other emulator project: tagged sections, so readers skip what they don't know.
#pragma once
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace nd2 {

class StateWriter {
public:
  std::vector<uint8_t> bytes;

  template <typename T> void put(const T& v) {
    static_assert(std::is_trivially_copyable<T>::value, "plain values only");
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    bytes.insert(bytes.end(), p, p + sizeof(T));
  }
  void raw(const void* p, size_t n) { const auto* b = static_cast<const uint8_t*>(p); bytes.insert(bytes.end(), b, b + n); }
  template <typename T> void vec(const std::vector<T>& v) { put(uint64_t(v.size())); if (!v.empty()) raw(v.data(), v.size() * sizeof(T)); }
  void str(const std::string& s) { put(uint64_t(s.size())); raw(s.data(), s.size()); }
  template <typename A, typename B> void deq(const std::deque<std::pair<A, B>>& d) {
    put(uint64_t(d.size()));
    for (const auto& x : d) { put(x.first); put(x.second); }
  }
  // tagged section: tag, then the length of what `body` writes
  template <typename F> void section(const char (&tag)[5], F body) {
    raw(tag, 4);
    const size_t lenAt = bytes.size();
    put(uint64_t(0));
    body(*this);
    const uint64_t len = bytes.size() - lenAt - 8;
    std::memcpy(&bytes[lenAt], &len, 8);
  }
};

class StateReader {
public:
  StateReader() = default;
  StateReader(const uint8_t* data, size_t size) : p_(data), end_(data + size) {}
  bool ok() const { return ok_; }
  bool atEnd() const { return p_ >= end_; }
  void fail() { ok_ = false; p_ = end_; }

  template <typename T> void get(T& v) {
    static_assert(std::is_trivially_copyable<T>::value, "plain values only");
    if (size_t(end_ - p_) < sizeof(T)) { fail(); return; }
    std::memcpy(&v, p_, sizeof(T)); p_ += sizeof(T);
  }
  template <typename T> T get() { T v{}; get(v); return v; }
  void raw(void* dst, size_t n) { if (size_t(end_ - p_) < n) { fail(); return; } std::memcpy(dst, p_, n); p_ += n; }
  template <typename T> void vec(std::vector<T>& v, size_t maxCount = size_t(1) << 26) {
    const uint64_t n = get<uint64_t>();
    if (!ok_ || n > maxCount || n * sizeof(T) > size_t(end_ - p_)) { fail(); return; }
    v.resize(size_t(n));
    if (n) raw(v.data(), size_t(n) * sizeof(T));
  }
  void str(std::string& s) {
    const uint64_t n = get<uint64_t>();
    if (!ok_ || n > size_t(end_ - p_)) { fail(); return; }
    s.assign(reinterpret_cast<const char*>(p_), size_t(n)); p_ += n;
  }
  template <typename A, typename B> void deq(std::deque<std::pair<A, B>>& d) {
    const uint64_t n = get<uint64_t>();
    if (!ok_ || n * (sizeof(A) + sizeof(B)) > size_t(end_ - p_)) { fail(); return; }
    d.clear();
    for (uint64_t i = 0; i < n && ok_; ++i) { A a = get<A>(); B b = get<B>(); d.emplace_back(a, b); }
  }
  bool nextSection(std::string& tag, StateReader& body) {
    if (atEnd() || !ok_) return false;
    char t[4]; raw(t, 4);
    const uint64_t len = get<uint64_t>();
    if (!ok_ || len > size_t(end_ - p_)) { fail(); return false; }
    tag.assign(t, 4);
    body = StateReader(p_, size_t(len));
    p_ += len;
    return true;
  }

private:
  const uint8_t* p_ = nullptr;
  const uint8_t* end_ = nullptr;
  bool ok_ = true;
};

}  // namespace nd2
