// Circuit-bent mode (source/core/nd2/bends.h). Mappings and settings always; with the real OS (ND2_OS=/path/to/os.cab,
// optional ND2_FLASH=flash image) also: every bend changes the sound, Sine Stuck Bit and DAC Bits switched off leave no
// trace, and with the clock bends (CPU Starve, Brownouts, Crystal) the hits still land on the host's beat.
#include "engine.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

using namespace nd2;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static OsImage g_os;
static std::vector<uint8_t> g_flash;
constexpr double kRate = 48000;
constexpr int kBlock = 256, kTotal = int(2.2 * kRate);
constexpr double kFirstHit = 0.2, kHitEvery = 0.25;

struct Setting { int bend; float amount; double from, until; };   // amount from `from` to `until` (seconds), else 0

// Bank 1 program 5 (sine-bank tone models, so Sine Stuck Bit has something to bend), every pad hit on its default MIDI
// note every 0.25 s from 0.2 s.
static std::vector<float> render(std::initializer_list<Setting> settings, int* latency = nullptr) {
  std::vector<float> out;
  Engine e;
  e.setBootFlash(g_flash);
  std::string err;
  if (!e.setOs(g_os, err)) { printf("setOs: %s\n", err.c_str()); return out; }
  e.waitForJob();
  e.prepare(kRate, kBlock, false, true);
  if (latency) *latency = e.latencySamples();
  static const uint8_t notes[Engine::kPads] = {60, 62, 64, 65, 67, 69};
  std::vector<float> L(kBlock), R(kBlock), S[Engine::kStemChannels];
  float* sp[Engine::kStemChannels];
  for (int s = 0; s < Engine::kStemChannels; s++) { S[s].resize(kBlock); sp[s] = S[s].data(); }
  std::vector<uint8_t> msg(3 * Engine::kPads);
  for (int done = 0; done < kTotal; done += kBlock) {
    const int n = std::min(kBlock, kTotal - done);
    const double t = done / kRate;
    for (const auto& s : settings) e.setKitBend(s.bend, t >= s.from && t < s.until ? s.amount : 0.0f);
    std::vector<Engine::MidiEvent> ev;
    static const uint8_t program[8] = {0xB9, 0, 0, 0xB9, 32, 0, 0xC9, 4};
    if (done == 0) { ev.push_back({0, program, 3}); ev.push_back({0, program + 3, 3}); ev.push_back({0, program + 6, 2}); }
    for (int64_t h = int64_t(kFirstHit * kRate); h < kTotal; h += int64_t(kHitEvery * kRate))
      if (h >= done && h < done + n)
        for (int k = 0; k < Engine::kPads; k++) {
          msg[3 * k] = 0x99; msg[3 * k + 1] = notes[k]; msg[3 * k + 2] = 110;
          ev.push_back({int(h - done), &msg[3 * k], 3});
        }
    e.process(L.data(), R.data(), n, ev.data(), int(ev.size()), nullptr, sp);
    out.insert(out.end(), L.begin(), L.begin() + n);
  }
  return out;
}

static double rmsDb(const std::vector<float>& x) {
  double s = 0; for (float v : x) s += double(v) * v;
  return 10 * std::log10(s / double(std::max<size_t>(1, x.size())) + 1e-20);
}
static double diffDb(const std::vector<float>& a, const std::vector<float>& b) {
  double s = 0; const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; i++) s += double(a[i] - b[i]) * double(a[i] - b[i]);
  return 10 * std::log10(s / double(std::max<size_t>(1, n)) + 1e-20);
}
// Onset of each hit: the first sample above -30 dBFS in the 0.2 s after it was sent (host time, latency removed).
static std::vector<double> onsets(const std::vector<float>& x, int latency) {
  std::vector<double> t;
  for (double h = kFirstHit; h + 0.2 < double(x.size()) / kRate; h += kHitEvery) {
    const size_t a = size_t(h * kRate) + size_t(latency), b = a + size_t(0.2 * kRate);
    double at = -1;
    for (size_t i = a; i < b && i < x.size(); i++) if (std::fabs(x[i]) > 0.0316f) { at = double(i - size_t(latency)) / kRate; break; }
    t.push_back(at);
  }
  return t;
}

int main() {
  {   // mappings and settings (no OS needed)
    CHECK(bendmap::starveFrameCycles(0) == 2048 && bendmap::starveFrameCycles(1) == 700, "starve range");
    CHECK(bendmap::timeRate(0) == 1.0 && bendmap::timeRate(1) == 0.0 && std::fabs(bendmap::timeRate(0.4) - 0.25) < 1e-9, "time range");
    CHECK(bendmap::sineBit(0) == -1 && bendmap::sineBit(1.0 / 8) == 8 && bendmap::sineBit(1) == 15, "sine bit steps");
    CHECK(bendmap::dacBits(0) == 32 && bendmap::dacBits(1.0 / 8) == 12 && bendmap::dacBits(1) == 4, "dac bit steps");
    CHECK(bendmap::crystalRatio(0) == 1.0 && bendmap::crystalRatio(1) == 0.5, "crystal range");
    CHECK(bendmap::brownoutsPerSecond(0) == 0.0 && bendmap::brownoutsPerSecond(1) > 10, "brownout range");
    Engine e;
    e.setKitBend(0, 2.0f); CHECK(e.kitBend(0) == 1.0f, "amount above 1 is 1 (%g)", double(e.kitBend(0)));
    e.setKitBend(2, -1.0f); CHECK(e.kitBend(2) == 0.0f, "negative amount is Off");
    e.setKitBend(5, NAN); CHECK(e.kitBend(5) == 0.0f, "NaN is Off");
    e.setKitBend(6, 0.5f); CHECK(e.kitBend(6) == 0.0f, "bend 7 does not exist");
  }
  const char* osPath = getenv("ND2_OS");
  if (!osPath) { printf("%s\n", fails ? "bend tests FAILED" : "bend tests passed (ND2_OS not set: OS checks skipped)"); return fails ? 1 : 0; }
  std::string err;
  if (!loadOsUpdateFile(osPath, g_os, err)) { printf("FAIL: %s\n", err.c_str()); return 1; }
  if (const char* f = getenv("ND2_FLASH")) {
    std::ifstream in(f, std::ios::binary);
    g_flash.assign(std::istreambuf_iterator<char>(in), {});
  }

  int latency = 0;
  const std::vector<float> base = render({}, &latency);
  CHECK(!base.empty() && rmsDb(base) > -60, "the unbent render sounds (%.1f dB)", rmsDb(base));
  CHECK(render({{0, 0.0f, 0, 9}, {4, 0.0f, 0, 9}}) == base, "amounts at 0 change nothing");
  // bends switched off before the first hit (0.2 s) that leave nothing behind
  CHECK(render({{int(KitBend::SineBit), 1.0f, 0, 0.1}}) == base, "Sine Stuck Bit switched off leaves no trace");
  CHECK(render({{int(KitBend::DacBits), 1.0f, 0, 0.1}}) == base, "DAC Bits switched off leaves no trace");

  static const char* names[kKitBends] = {"CPU Starve", "Brownouts", "Time", "Sine Stuck Bit", "Crystal", "DAC Bits"};
  const auto baseOn = onsets(base, latency);
  for (int b = 0; b < kKitBends; b++)
    for (float a : {0.5f, 1.0f}) {
      const auto r = render({{b, a, 0, 9}});
      const double d = diffDb(r, base);
      printf("%-15s %.1f: %6.1f dB rms, difference %6.1f dB\n", names[b], double(a), rmsDb(r), d);
      CHECK(r.size() == base.size() && r != base && d > -70, "%s at %.1f changes the sound (%.1f dB)", names[b], double(a), d);
      if (b == int(KitBend::Starve) || b == int(KitBend::Brownouts) || b == int(KitBend::Crystal)) {
        const auto on = onsets(r, latency);
        double worst = 0; int found = 0;
        for (size_t i = 0; i < on.size() && i < baseOn.size(); i++)
          if (on[i] >= 0 && baseOn[i] >= 0) { worst = std::max(worst, std::fabs(on[i] - baseOn[i])); found++; }
        printf("                hits on the beat: %d of %zu, worst %.1f ms\n", found, baseOn.size(), worst * 1e3);
        CHECK(found >= int(baseOn.size()) - 1 && worst < 0.012, "%s at %.1f: hits off the beat (worst %.1f ms, %d found)",
              names[b], double(a), worst * 1e3, found);
      }
    }
  printf("%s\n", fails ? "bend tests FAILED" : "bend tests passed");
  return fails ? 1 : 0;
}
