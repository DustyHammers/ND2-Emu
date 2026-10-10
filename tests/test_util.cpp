// ROM-free tests: rate converter, MIDI parser and message ring.
#include "util/midi_stream.h"
#include "util/rate_converter.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace nd2;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static RateConverter::Design outDesign(int host) {
  RateConverter::Design d;
  d.inHz = 78e6 / 2048; d.outHz = host; d.inStep = 609375; d.outStep = int64_t(host) * 16;
  d.taps = 64; d.beta = 8.0; d.passHz = 0.45 * d.inHz;
  return d;
}

// native-rate sine through the output converter: compare with the ideal sine at the host rate
static double snrDb(int host, double f, int block) {
  RateConverter rc; rc.setup(2, outDesign(host));
  const double fin = 78e6 / 2048;
  std::vector<float> outL, outR;
  int64_t inPos = 0;
  const int total = host;                          // 1 s
  std::vector<float> l(static_cast<size_t>(block)), r(static_cast<size_t>(block)), il, ir;
  for (int done = 0; done < total; done += block) {
    const int n = std::min(block, total - done);
    const int64_t need = rc.inputNeeded(n);
    il.resize(size_t(need)); ir.resize(size_t(need));
    for (int64_t i = 0; i < need; i++, inPos++) il[size_t(i)] = ir[size_t(i)] = float(0.5 * std::sin(2 * M_PI * f * double(inPos) / fin));
    const float* in[2] = {il.data(), ir.data()};
    rc.push(in, int(need));
    float* out[2] = {l.data(), r.data()};
    rc.produce(out, n);
    outL.insert(outL.end(), l.begin(), l.begin() + n);
  }
  double sig = 0, err = 0;
  for (int k = host / 10; k < total; k++) {                     // skip the start-up
    const double ideal = 0.5 * std::sin(2 * M_PI * f * double(k) / host);
    sig += ideal * ideal; err += (outL[size_t(k)] - ideal) * (outL[size_t(k)] - ideal);
  }
  return 10 * std::log10(sig / std::max(err, 1e-30));
}

int main() {
  // ---- rate converter
  for (int host : {44100, 48000, 96000}) {
    const double s = snrDb(host, 1000.0, 256);
    printf("38085.9375 -> %d Hz, 1 kHz sine: SNR %.1f dB\n", host, s);
    CHECK(s > 70, "SNR %.1f dB at %d Hz", s, host);
    const double s2 = snrDb(host, 12000.0, 256);
    CHECK(s2 > 60, "12 kHz SNR %.1f dB at %d Hz", s2, host);
  }
  CHECK(std::fabs(snrDb(48000, 1000.0, 37) - snrDb(48000, 1000.0, 512)) < 1e-6, "output depends on block size");

  // ---- MIDI parser
  {
    MidiParser p;
    std::vector<std::vector<uint8_t>> got;
    const uint8_t s[] = {0x99, 60, 100, 62, 90, 0xF0, 0x33, 0xF8, 0x7F, 0xF7, 0xC9, 3, 0x40};
    for (uint8_t b : s) if (p.feed(b)) got.emplace_back(p.data(), p.data() + p.size());
    CHECK(got.size() == 6, "%zu messages", got.size());
    if (got.size() == 6) {
      CHECK((got[1] == std::vector<uint8_t>{0x99, 62, 90}), "running status not expanded");
      CHECK((got[2] == std::vector<uint8_t>{0xF8}), "real-time inside SysEx");
      CHECK((got[3] == std::vector<uint8_t>{0xF0, 0x33, 0x7F, 0xF7}), "SysEx not whole");
      CHECK((got[4] == std::vector<uint8_t>{0xC9, 3}), "program change");
      CHECK((got[5] == std::vector<uint8_t>{0xC9, 0x40}), "running status after a program change");
    }
  }
  // ---- message ring
  {
    MessageRing r(64);
    const uint8_t a[] = {1, 2, 3}, big[70] = {};
    CHECK(r.push(a, 3), "push");
    CHECK(!r.push(big, 70), "oversized push accepted");
    for (int i = 0; i < 40; i++) {                                // wrap around many times
      CHECK(r.push(a, 3), "push %d", i);
      std::vector<uint8_t> got;
      r.pop([&](const uint8_t* d, size_t n) { got.assign(d, d + n); });
      CHECK((got == std::vector<uint8_t>{1, 2, 3}), "pop %d", i);
    }
  }
  printf(fails ? "%d failure(s)\n" : "all util tests passed\n", fails);
  return fails ? 1 : 0;
}
