// Circuit-bent mode (source/core/nd2/bends.h). Setting checks always; with the real OS (ND2_OS=/path/to/os.cab,
// optional ND2_FLASH=flash image) also: Off leaves no trace, and each bend on one pad leaves the other five pads'
// channel outputs byte-identical (Underclock: within 1.5 dB, see bends.h).
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

struct Render { std::vector<float> main; std::vector<float> pad[Engine::kPads]; };   // pad k: L+R of its channel output

// Every pad hit on its default MIDI note every 0.25 s from 0.2 s; `bends[k]` from the first block (pad k), and
// optionally one pad bent only until 0.1 s (before any note), to see that Off leaves nothing behind.
static Render render(const int (&bends)[Engine::kPads], int earlyPad = -1, int earlyBend = 0) {
  Render r;
  Engine e;
  e.setBootFlash(g_flash);
  std::string err;
  if (!e.setOs(g_os, err)) { printf("setOs: %s\n", err.c_str()); return r; }
  e.waitForJob();
  e.prepare(kRate, kBlock, false, true);
  for (int k = 0; k < Engine::kPads; k++) e.setBend(k, bends[k]);
  if (earlyPad >= 0) e.setBend(earlyPad, earlyBend);
  static const uint8_t notes[Engine::kPads] = {60, 62, 64, 65, 67, 69};
  std::vector<float> L(kBlock), R(kBlock), S[Engine::kStemChannels];
  float* sp[Engine::kStemChannels];
  for (int s = 0; s < Engine::kStemChannels; s++) { S[s].resize(kBlock); sp[s] = S[s].data(); }
  std::vector<uint8_t> msg(3 * Engine::kPads);
  for (int done = 0; done < kTotal; done += kBlock) {
    const int n = std::min(kBlock, kTotal - done);
    if (earlyPad >= 0 && done >= int(0.1 * kRate)) e.setBend(earlyPad, bends[earlyPad]);
    std::vector<Engine::MidiEvent> ev;
    for (int64_t t = int64_t(0.2 * kRate); t < kTotal; t += int64_t(0.25 * kRate))
      if (t >= done && t < done + n)
        for (int k = 0; k < Engine::kPads; k++) {
          msg[3 * k] = 0x99; msg[3 * k + 1] = notes[k]; msg[3 * k + 2] = 110;
          ev.push_back({int(t - done), &msg[3 * k], 3});
        }
    e.process(L.data(), R.data(), n, ev.data(), int(ev.size()), nullptr, sp);
    r.main.insert(r.main.end(), L.begin(), L.begin() + n);
    for (int k = 0; k < Engine::kPads; k++)
      for (int i = 0; i < n; i++) r.pad[k].push_back(S[2 * k][size_t(i)] + S[2 * k + 1][size_t(i)]);
  }
  return r;
}

static double rmsDb(const std::vector<float>& x) {
  double s = 0; for (float v : x) s += double(v) * v;
  return 10 * std::log10(s / double(std::max<size_t>(1, x.size())) + 1e-20);
}

int main() {
  {   // settings (no OS needed)
    Engine e;
    e.setBend(0, 9); CHECK(e.bend(0) == 0, "bend out of range is Off (%d)", e.bend(0));
    e.setBend(2, 8); CHECK(e.bend(2) == 8, "bend 8 kept (%d)", e.bend(2));
    e.setBend(5, -1); CHECK(e.bend(5) == 0, "negative bend is Off (%d)", e.bend(5));
    e.setBend(6, 3); CHECK(e.bend(6) == 0, "pad 7 does not exist");
  }
  const char* osPath = getenv("ND2_OS");
  if (!osPath) { printf("%s\n", fails ? "bend tests FAILED" : "bend tests passed (ND2_OS not set: OS checks skipped)"); return fails ? 1 : 0; }
  std::string err;
  if (!loadOsUpdateFile(osPath, g_os, err)) { printf("FAIL: %s\n", err.c_str()); return 1; }
  if (const char* f = getenv("ND2_FLASH")) {
    std::ifstream in(f, std::ios::binary);
    g_flash.assign(std::istreambuf_iterator<char>(in), {});
  }

  const int off[Engine::kPads] = {0, 0, 0, 0, 0, 0};
  const Render base = render(off);
  CHECK(!base.main.empty() && rmsDb(base.main) > -60, "the unbent render sounds (%.1f dB)", rmsDb(base.main));
  const Render early = render(off, 0, int(Bend::Crush));
  CHECK(early.main == base.main, "a bend switched off before any note leaves no trace");

  static const char* names[kBendCount] = {"Off", "Crush", "Hold", "Stuck", "Crackle", "Underclock", "Stretch", "Sine", "Noise"};
  for (int b = 1; b < kBendCount; b++) {
    int changedOwn = 0;
    for (int p = 0; p < Engine::kPads; p++) {
      int bends[Engine::kPads] = {0, 0, 0, 0, 0, 0};
      bends[p] = b;
      const Render r = render(bends);
      if (r.pad[p] != base.pad[p]) changedOwn++;
      for (int q = 0; q < Engine::kPads; q++) {
        if (q == p) continue;
        if (Bend(b) == Bend::Underclock) {
          const double d = std::fabs(rmsDb(r.pad[q]) - rmsDb(base.pad[q]));
          CHECK(d < 1.5, "%s on pad %d: pad %d's level moved %.2f dB", names[b], p + 1, q + 1, d);
        } else {
          CHECK(r.pad[q] == base.pad[q], "%s on pad %d changed pad %d", names[b], p + 1, q + 1);
        }
      }
    }
    printf("%-10s changes %d of 6 pads on their own (the rest don't use what it bends in this program)\n", names[b], changedOwn);
    if (Bend(b) <= Bend::Underclock) CHECK(changedOwn == Engine::kPads, "%s changes every pad (%d)", names[b], changedOwn);
  }
  printf("%s\n", fails ? "bend tests FAILED" : "bend tests passed");
  return fails ? 1 : 0;
}
