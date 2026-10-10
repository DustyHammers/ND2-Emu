// Engine tests with the real OS (ND2_OS=/path/to/os.cab; optional ND2_FLASH=flash image). Skipped without it.
#include "engine.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>

using namespace nd2;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static OsImage g_os;
static std::vector<uint8_t> g_flash;

static std::unique_ptr<Engine> makeEngine(double rate, int block, bool trig) {
  auto e = std::make_unique<Engine>();
  e->setBootFlash(g_flash);
  std::string err;
  if (!e->setOs(g_os, err)) { printf("setOs: %s\n", err.c_str()); return nullptr; }
  e->waitForJob();
  e->prepare(rate, block, trig);
  return e;
}

// runs `total` samples in blocks; `midiAt` = global sample of one note-on (or -1); `trigAt` = global sample of a pad-1 piezo hit
static std::vector<float> run(Engine& e, int total, int block, int64_t midiAt, int64_t trigAt, double rate, int64_t start = 0) {
  std::vector<float> out; out.reserve(size_t(total));
  std::vector<float> L(static_cast<size_t>(block)), R(static_cast<size_t>(block)), T(static_cast<size_t>(block));
  const uint8_t note[3] = {0x99, 60, 110};
  for (int done = 0; done < total; done += block) {
    const int n = std::min(block, total - done);
    const int64_t g0 = start + done;
    Engine::MidiEvent ev{0, note, 3}; int nev = 0;
    if (midiAt >= g0 && midiAt < g0 + n) { ev.offset = int(midiAt - g0); nev = 1; }
    const float* trig[Engine::kPads] = {};
    if (trigAt >= 0) {
      for (int i = 0; i < n; i++) {
        const double t = double(g0 + i - trigAt) / rate;
        T[size_t(i)] = t < 0 ? 0.0f : float(0.6 * std::sin(2 * M_PI * 400 * t) * std::exp(-t / 0.004));
      }
      trig[0] = T.data();
    }
    e.process(L.data(), R.data(), n, &ev, nev, trigAt >= 0 ? trig : nullptr);
    out.insert(out.end(), L.begin(), L.begin() + n);
  }
  return out;
}

static int64_t onset(const std::vector<float>& x, int64_t from) {
  for (size_t i = size_t(std::max<int64_t>(0, from)); i < x.size(); i++) if (std::fabs(x[i]) > 1e-4f) return int64_t(i);
  return -1;
}

int main() {
  const char* osPath = getenv("ND2_OS");
  if (!osPath) { printf("ND2_OS not set: engine test skipped\n"); return 0; }
  std::string err;
  if (!loadOsUpdateFile(osPath, g_os, err)) { printf("FAIL: %s\n", err.c_str()); return 1; }
  if (const char* fp = getenv("ND2_FLASH")) {
    if (FILE* f = fopen(fp, "rb")) { fseek(f, 0, SEEK_END); g_flash.resize(size_t(ftell(f))); fseek(f, 0, SEEK_SET); if (fread(g_flash.data(), 1, g_flash.size(), f) != g_flash.size()) g_flash.clear(); fclose(f); }
  }

  // ---- latency
  for (double rate : {44100.0, 48000.0, 96000.0}) for (bool trig : {false, true}) {
    Engine e; e.prepare(rate, 256, trig);
    const double ms = 1000.0 * e.latencySamples() / rate;
    printf("latency at %.0f Hz%s: %d samples (%.2f ms)\n", rate, trig ? " with trigger inputs" : "", e.latencySamples(), ms);
    CHECK(ms > 0.3 && ms < 5.0, "latency %.2f ms", ms);
  }

  const double rate = 48000; const int block = 256;
  // ---- sample-accurate MIDI: moving the note by 37 samples moves the sound by 37 samples
  {
    auto a = makeEngine(rate, block, false), b = makeEngine(rate, block, false);
    if (!a || !b) return 1;
    const int64_t S = 24000 + 100;
    auto xa = run(*a, 48000, block, S, -1, rate), xb = run(*b, 48000, block, S + 37, -1, rate);
    const int64_t oa = onset(xa, S), ob = onset(xb, S);
    // the event reaches the emulated MIDI input exactly 37 samples later (in machine cycles, relative to each origin)
    const double cA = double(a->lastMidiCycleForTest() - a->blockOriginForTest()), cB = double(b->lastMidiCycleForTest() - b->blockOriginForTest());
    printf("MIDI note at %lld -> sound at %lld (+%.2f ms incl. latency %d); moved by 37 -> input +%.2f samples, sound +%lld\n",
           (long long)S, (long long)oa, 1000.0 * (oa - S) / rate, a->latencySamples(), (cB - cA) * rate / 78e6, (long long)(ob - oa));
    CHECK(std::fabs((cB - cA) * rate / 78e6 - 37) < 0.01, "input moved by %.3f samples", (cB - cA) * rate / 78e6);
    // the OS handles notes on its 1250 Hz event tick, so the sound may move by up to one tick more or less
    CHECK(oa > S && std::llabs((ob - oa) - 37) <= int64_t(rate / 1250) + 2, "onsets %lld %lld", (long long)oa, (long long)ob);
    CHECK(a->lateMidi() == 0, "%llu late MIDI events", (unsigned long long)a->lateMidi());
  }
  // ---- trigger input: a piezo hit on pad 1 plays, aligned like MIDI; moving it moves the sound
  {
    auto a = makeEngine(rate, block, true), b = makeEngine(rate, block, true);
    const int64_t S = 24000 + 100;
    auto xa = run(*a, 48000, block, -1, S, rate), xb = run(*b, 48000, block, -1, S + 240, rate);
    const int64_t oa = onset(xa, S), ob = onset(xb, S);
    printf("trigger hit at %lld -> sound at %lld (+%.2f ms incl. latency %d); moved by 240 -> %lld; underruns %llu\n",
           (long long)S, (long long)oa, 1000.0 * (oa - S) / rate, a->latencySamples(), (long long)(ob - oa),
           (unsigned long long)a->trigUnderruns());
    CHECK(oa > S && oa - S < int64_t(0.015 * rate), "trigger onset %lld", (long long)oa);
    CHECK(std::llabs((ob - oa) - 240) <= 24, "trigger shift %lld", (long long)(ob - oa));     // ADC scan + OS detection jitter
    CHECK(a->trigUnderruns() == 0, "underruns");
  }
  // ---- state: restore gives the same machine and, from there, the same audio
  {
    auto a = makeEngine(rate, block, true);
    run(*a, 24000, block, 1000, 12000, rate);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));     // host "stopped": direct save
    auto blob = a->getState();
    CHECK(blob.size() > 100000, "state %zu bytes", blob.size());
    Engine b; b.setBootFlash(g_flash);
    if (!b.setOs(g_os, err)) return 1;
    b.waitForJob();
    CHECK(b.setState(blob, err), "setState: %s", err.c_str());
    b.waitForJob();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    auto blob2 = b.getState();
    CHECK(blob2 == blob, "restored machine differs (%zu vs %zu bytes)", blob2.size(), blob.size());
    if (blob2 != blob) if (const char* d = getenv("ND2_DUMP")) {
      for (int k = 0; k < 2; k++) { std::string fn = std::string(d) + (k ? "/b.bin" : "/a.bin"); if (FILE* f = fopen(fn.c_str(), "wb")) { auto& v = k ? blob2 : blob; fwrite(v.data(), 1, v.size(), f); fclose(f); } }
    }
    a->prepare(rate, block, true); b.prepare(rate, block, true);    // both retime from the same machine state
    auto xa = run(*a, 24000, block, 6000, 12000, rate, 0), xb = run(b, 24000, block, 6000, 12000, rate, 0);
    CHECK(xa == xb, "audio after restore differs");
    double pk = 0; for (float v : xa) pk = std::max(pk, double(std::fabs(v)));
    CHECK(pk > 0.01, "silent after restore (peak %.4f)", pk);
  }
  // ---- channel outputs: six stereo pairs that add up to the main output; each pad only in its own pair
  {
    auto a = makeEngine(rate, block, false);
    a->prepare(rate, block, false, true);
    const int N = 48000; const int notes[6] = {60, 62, 64, 65, 67, 69};
    std::vector<float> L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
    std::vector<std::vector<float>> sb(Engine::kStemChannels, std::vector<float>(static_cast<size_t>(block)));
    double maxErr = 0, peak = 0, e[6] = {}, leak = 0;
    for (int done = 0; done < N; done += block) {
      const int n = std::min(block, N - done);
      uint8_t msg[3] = {0x99, 0, 110}; Engine::MidiEvent ev{0, msg, 3}; int nev = 0;
      const int pad = (done + n > 2000 && done <= 2000) ? 0 : -1;       // pad 1 only, at sample 2000
      if (pad == 0) { msg[1] = uint8_t(notes[0]); ev.offset = 2000 - done; nev = 1; }
      float* sp[Engine::kStemChannels]; for (int k = 0; k < Engine::kStemChannels; k++) sp[k] = sb[size_t(k)].data();
      a->process(L.data(), R.data(), n, &ev, nev, nullptr, sp);
      for (int i = 0; i < n; i++) {
        double sl = 0, sr = 0;
        for (int c = 0; c < 6; c++) { sl += sb[size_t(2 * c)][size_t(i)]; sr += sb[size_t(2 * c + 1)][size_t(i)]; e[c] += sb[size_t(2 * c)][size_t(i)] * sb[size_t(2 * c)][size_t(i)]; }
        maxErr = std::max({maxErr, std::fabs(sl - L[size_t(i)]), std::fabs(sr - R[size_t(i)])});
        peak = std::max(peak, double(std::fabs(L[size_t(i)])));
      }
    }
    for (int c = 1; c < 6; c++) leak = std::max(leak, e[c]);
    printf("channel outputs: sum vs main output max difference %.2e (main peak %.3f); pad 1 energy in ch1 %.3g, largest elsewhere %.3g\n", maxErr, peak, e[0], leak);
    CHECK(peak > 0.01, "silent");
    CHECK(maxErr < 1e-5, "channel outputs do not add up to the main output (%.2e)", maxErr);
    CHECK(e[0] > 0 && leak == 0, "pad 1 leaked into other channel outputs");
  }
  // ---- panel: encoder and buttons from another thread reach the OS
  {
    auto a = makeEngine(rate, block, false);
    run(*a, 4800, block, -1, -1, rate);
    const auto p0 = a->panel();
    std::thread ui([&] { a->turnEncoder(1); });
    ui.join();
    run(*a, 24000, block, -1, -1, rate);
    const auto p1 = a->panel();
    CHECK(p0.seg != p1.seg, "encoder did not change the display");
    std::thread ui2([&] { a->pressButton(panel::BtnParam1, true); a->pressButton(panel::BtnParam1, false); });
    ui2.join();
    run(*a, 24000, block, -1, -1, rate);
    const auto p2 = a->panel();
    CHECK(p2.leds & (1u << panel::kLedUpperRow[0]), "a click on parameter 1 did not light its LED (leds %08x)", p2.leds);
  }
  printf(fails ? "%d failure(s)\n" : "all engine tests passed\n", fails);
  return fails ? 1 : 0;
}
