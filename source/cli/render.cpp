// nd2render: offline render through the Engine, exactly as the plugin runs it (host rate, host blocks,
// sample-accurate MIDI, six trigger audio inputs, latency compensated).
//   nd2render -os <os.cab|Update.exe> [-flash flash.bin] -o out.wav [-t seconds] [-rate 48000] [-block 256]
//             [-trig six_channel.wav] [-gain pad,g] [-note t,ch,note,vel] [-pgm t,ch,prog] [-bank t,ch,bank]
//             [-midi "t,hex hex .."] [-press t,idx[,dur]] [-enc t,detents] [-master v]
//             [-loadstate in.state] [-savestate out.state] [-keeplatency] [-stems prefix]
// Times are in seconds from the start of the render. The trigger WAV must be at the render rate; channel k feeds pad k+1.
// -stems writes prefix_ch1.wav .. prefix_ch6.wav: each channel's exact share of the main output (they add up to it).
#include "engine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace nd2;

namespace {

bool readFile(const char* fn, std::vector<uint8_t>& out) {
  FILE* f = fopen(fn, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END); out.resize(size_t(ftell(f))); fseek(f, 0, SEEK_SET);
  const bool ok = fread(out.data(), 1, out.size(), f) == out.size();
  fclose(f);
  return ok;
}

// PCM 16/24/32 or float32 (plain or extensible) -> per-channel float
bool readWav(const char* fn, std::vector<std::vector<float>>& ch, double& rate, std::string& err) {
  std::vector<uint8_t> d;
  if (!readFile(fn, d) || d.size() < 12 || memcmp(d.data(), "RIFF", 4) || memcmp(d.data() + 8, "WAVE", 4)) { err = "not a WAV file"; return false; }
  auto u16 = [&](size_t o) { return uint32_t(d[o] | d[o + 1] << 8); };
  auto u32 = [&](size_t o) { return uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24); };
  int fmt = 0, nch = 0, bits = 0;
  for (size_t o = 12; o + 8 <= d.size();) {
    const uint32_t len = u32(o + 4);
    const size_t body = o + 8;
    if (body + len > d.size()) { err = "truncated WAV"; return false; }
    if (!memcmp(&d[o], "fmt ", 4) && len >= 16) {
      fmt = int(u16(body)); nch = int(u16(body + 2)); rate = u32(body + 4); bits = int(u16(body + 14));
      if (fmt == 0xFFFE && len >= 26) fmt = int(u16(body + 24));
    } else if (!memcmp(&d[o], "data", 4)) {
      if (!nch || !bits) { err = "data before fmt"; return false; }
      const size_t bps = size_t(bits / 8), frames = len / (bps * size_t(nch));
      ch.assign(size_t(nch), std::vector<float>(frames));
      for (size_t i = 0; i < frames; i++) for (int c = 0; c < nch; c++) {
        const size_t p = body + (i * size_t(nch) + size_t(c)) * bps;
        float v;
        if (fmt == 3 && bits == 32) { uint32_t x = u32(p); memcpy(&v, &x, 4); }
        else if (fmt == 1 && bits == 16) v = float(int16_t(u16(p))) / 32768.0f;
        else if (fmt == 1 && bits == 24) v = float(int32_t(u32(p - 1) & 0xFFFFFF00u) / 256) / 8388608.0f;
        else if (fmt == 1 && bits == 32) v = float(int32_t(u32(p))) / 2147483648.0f;
        else { err = "unsupported WAV format"; return false; }
        ch[size_t(c)][i] = v;
      }
      return true;
    }
    o = body + len + (len & 1);
  }
  err = "no data chunk";
  return false;
}

void writeWav24(const char* fn, const std::vector<float>& l, const std::vector<float>& r, int rate) {
  FILE* f = fopen(fn, "wb");
  if (!f) { fprintf(stderr, "cannot write %s\n", fn); exit(1); }
  const uint32_t data = uint32_t(l.size() * 6);
  auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); u32(36 + data); fwrite("WAVEfmt ", 1, 8, f); u32(16); u16(1); u16(2);
  u32(uint32_t(rate)); u32(uint32_t(rate) * 6); u16(6); u16(24); fwrite("data", 1, 4, f); u32(data);
  for (size_t i = 0; i < l.size(); i++) for (float v : {l[i], r[i]}) {
    const int32_t s = int32_t(std::lround(std::max(-1.0f, std::min(1.0f, v)) * 8388607.0f));
    const uint8_t b[3] = {uint8_t(s), uint8_t(s >> 8), uint8_t(s >> 16)};
    fwrite(b, 1, 3, f);
  }
  fclose(f);
}

struct Ev { int64_t at; int kind; std::vector<uint8_t> bytes; int a = 0, b = 0; };   // kind 0 MIDI, 1 press, 2 release, 3 encoder

}  // namespace

int main(int argc, char** argv) {
  const char *osPath = nullptr, *flashPath = nullptr, *outPath = nullptr, *trigPath = nullptr, *loadPath = nullptr, *savePath = nullptr, *stemPrefix = nullptr;
  double secs = -1, rate = 48000, master = 1.0;
  int block = 256;
  bool keepLatency = false;
  float gain[Engine::kPads] = {1, 1, 1, 1, 1, 1};
  struct Raw { double t; int kind; std::vector<uint8_t> bytes; int a, b; };
  std::vector<Raw> raw;
  for (int i = 1; i < argc; i++) {
    auto arg = [&](const char* s) { return !strcmp(argv[i], s) && i + 1 < argc; };
    if (arg("-os")) osPath = argv[++i];
    else if (arg("-flash")) flashPath = argv[++i];
    else if (arg("-o")) outPath = argv[++i];
    else if (arg("-t")) secs = atof(argv[++i]);
    else if (arg("-rate")) rate = atof(argv[++i]);
    else if (arg("-block")) block = std::max(1, atoi(argv[++i]));
    else if (arg("-trig")) trigPath = argv[++i];
    else if (arg("-master")) master = atof(argv[++i]);
    else if (arg("-gain")) { int p; float g; if (sscanf(argv[++i], "%d,%f", &p, &g) == 2 && p >= 1 && p <= 6) gain[p - 1] = g; }
    else if (arg("-loadstate")) loadPath = argv[++i];
    else if (arg("-savestate")) savePath = argv[++i];
    else if (!strcmp(argv[i], "-keeplatency")) keepLatency = true;
    else if (arg("-stems")) stemPrefix = argv[++i];
    else if (arg("-note")) { double t; int c, n, v; sscanf(argv[++i], "%lf,%d,%d,%d", &t, &c, &n, &v);
      raw.push_back({t, 0, {uint8_t(0x90 | ((c - 1) & 15)), uint8_t(n & 127), uint8_t(v & 127)}, 0, 0}); }
    else if (arg("-pgm")) { double t; int c, p; sscanf(argv[++i], "%lf,%d,%d", &t, &c, &p);
      raw.push_back({t, 0, {uint8_t(0xC0 | ((c - 1) & 15)), uint8_t((p - 1) & 127)}, 0, 0}); }
    else if (arg("-bank")) { double t; int c, bk; sscanf(argv[++i], "%lf,%d,%d", &t, &c, &bk);
      raw.push_back({t, 0, {uint8_t(0xB0 | ((c - 1) & 15)), 0, 0}, 0, 0});
      raw.push_back({t, 0, {uint8_t(0xB0 | ((c - 1) & 15)), 32, uint8_t((bk - 1) & 127)}, 0, 0}); }
    else if (arg("-midi")) { const char* s = argv[++i]; double t = atof(s); std::vector<uint8_t> by;
      for (const char* p = strchr(s, ','); p && *p;) { while (*p == ',' || *p == ' ') p++; if (!*p) break; char* e; by.push_back(uint8_t(strtoul(p, &e, 16))); p = e; }
      raw.push_back({t, 0, by, 0, 0}); }
    else if (arg("-press")) { double t, dur = 0.1; int idx; if (sscanf(argv[++i], "%lf,%d,%lf", &t, &idx, &dur) < 2) continue;
      raw.push_back({t, 1, {}, idx, 0}); raw.push_back({t + dur, 2, {}, idx, 0}); }
    else if (arg("-enc")) { double t; int det; sscanf(argv[++i], "%lf,%d", &t, &det); raw.push_back({t, 3, {}, det, 0}); }
    else { fprintf(stderr, "unknown or incomplete option %s\n", argv[i]); return 1; }
  }
  if (!osPath || !outPath) { fprintf(stderr, "usage: nd2render -os <os.cab|Update.exe> -o out.wav [options] (see source)\n"); return 1; }

  std::string err;
  OsImage os;
  if (!loadOsUpdateFile(osPath, os, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  std::vector<std::vector<float>> trig;
  if (trigPath) {
    double tr = 0;
    if (!readWav(trigPath, trig, tr, err)) { fprintf(stderr, "%s: %s\n", trigPath, err.c_str()); return 1; }
    if (std::fabs(tr - rate) > 0.5) { fprintf(stderr, "%s is %.0f Hz; render rate is %.0f Hz (use -rate)\n", trigPath, tr, rate); return 1; }
    if (trig.size() > size_t(Engine::kPads)) trig.resize(size_t(Engine::kPads));
    if (secs < 0) secs = double(trig[0].size()) / rate;
  }
  if (secs < 0) { for (auto& e : raw) secs = std::max(secs, e.t + 2.0); if (secs < 0) secs = 2.0; }

  Engine eng;
  if (flashPath) { std::vector<uint8_t> fl; if (!readFile(flashPath, fl)) { fprintf(stderr, "cannot read %s\n", flashPath); return 1; } eng.setBootFlash(std::move(fl)); }
  if (!eng.setOs(os, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  eng.waitForJob();
  if (loadPath) {
    std::vector<uint8_t> st;
    if (!readFile(loadPath, st) || !eng.setState(st, err)) { fprintf(stderr, "load state: %s\n", err.empty() ? "cannot read" : err.c_str()); return 1; }
    eng.waitForJob();
  }
  if (eng.state() != Engine::State::Running) { fprintf(stderr, "machine not running: %s\n", eng.message().c_str()); return 1; }
  for (int p = 0; p < Engine::kPads; p++) eng.setTrigGain(p, gain[p]);
  eng.setMasterLevel(float(master));
  eng.prepare(rate, block, trigPath != nullptr, stemPrefix != nullptr);
  const int lat = keepLatency ? 0 : eng.latencySamples();

  std::vector<Ev> evs;
  for (auto& r : raw) evs.push_back({int64_t(std::llround(r.t * rate)), r.kind, r.bytes, r.a, r.b});
  std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.at < b.at; });

  const int64_t total = int64_t(std::llround(secs * rate)) + lat;
  const size_t nb = static_cast<size_t>(block);
  std::vector<float> outL, outR, L(nb), R(nb);
  constexpr int NS = Engine::kStemChannels;
  std::vector<std::vector<float>> stemOut(NS), stemBuf(NS, std::vector<float>(nb));
  std::vector<std::vector<float>> tb(static_cast<size_t>(Engine::kPads), std::vector<float>(nb));
  std::vector<Engine::MidiEvent> mev;
  size_t ei = 0;
  for (int64_t pos = 0; pos < total; pos += block) {
    const int n = int(std::min<int64_t>(block, total - pos));
    mev.clear();
    for (; ei < evs.size() && evs[ei].at < pos + n; ei++) {
      Ev& e = evs[ei];
      if (e.kind == 0) mev.push_back({int(std::max<int64_t>(0, e.at - pos)), e.bytes.data(), int(e.bytes.size())});
      else if (e.kind == 1 || e.kind == 2) eng.pressButton(e.a, e.kind == 1);      // panel input: block resolution, like a UI
      else eng.turnEncoder(e.a);
    }
    const float* tp[Engine::kPads] = {};
    if (trigPath) for (int c = 0; c < Engine::kPads; c++) {
      if (size_t(c) >= trig.size()) continue;
      for (int i = 0; i < n; i++) { const int64_t k = pos + i; tb[size_t(c)][size_t(i)] = k < int64_t(trig[size_t(c)].size()) ? trig[size_t(c)][size_t(k)] : 0.0f; }
      tp[c] = tb[size_t(c)].data();
    }
    float* sp[NS]; for (int k = 0; k < NS; k++) sp[k] = stemBuf[size_t(k)].data();
    eng.process(L.data(), R.data(), n, mev.data(), int(mev.size()), trigPath ? tp : nullptr, stemPrefix ? sp : nullptr);
    outL.insert(outL.end(), L.begin(), L.begin() + n);
    outR.insert(outR.end(), R.begin(), R.begin() + n);
    if (stemPrefix) for (int k = 0; k < NS; k++) stemOut[size_t(k)].insert(stemOut[size_t(k)].end(), stemBuf[size_t(k)].begin(), stemBuf[size_t(k)].begin() + n);
  }
  outL.erase(outL.begin(), outL.begin() + lat); outR.erase(outR.begin(), outR.begin() + lat);
  writeWav24(outPath, outL, outR, int(std::lround(rate)));
  if (stemPrefix) {
    for (auto& v : stemOut) v.erase(v.begin(), v.begin() + lat);
    double maxErr = 0, peakMain = 0;
    for (size_t i = 0; i < outL.size(); i++) {
      double sl = 0, sr = 0;
      for (int c = 0; c < 6; c++) { sl += stemOut[size_t(2 * c)][i]; sr += stemOut[size_t(2 * c + 1)][i]; }
      maxErr = std::max({maxErr, std::fabs(sl - outL[i]), std::fabs(sr - outR[i])});
      peakMain = std::max({peakMain, double(std::fabs(outL[i])), double(std::fabs(outR[i]))});
    }
    for (int c = 0; c < 6; c++) {
      const std::string fn = std::string(stemPrefix) + "_ch" + std::to_string(c + 1) + ".wav";
      writeWav24(fn.c_str(), stemOut[size_t(2 * c)], stemOut[size_t(2 * c + 1)], int(std::lround(rate)));
      float pk = 0; for (int s = 0; s < 2; s++) for (float v : stemOut[size_t(2 * c + s)]) pk = std::max(pk, std::fabs(v));
      printf("wrote %s (peak %.3f)\n", fn.c_str(), pk);
    }
    printf("channel outputs vs main output: largest difference of the sum %.2e (%.1f dB below the main peak)\n",
           maxErr, maxErr > 0 ? 20 * std::log10(peakMain / maxErr) : 999.0);
  }
  float pk = 0; for (size_t i = 0; i < outL.size(); i++) pk = std::max({pk, std::fabs(outL[i]), std::fabs(outR[i])});
  printf("wrote %s: %.2f s at %.0f Hz, block %d, latency %d samples (%s), peak %.3f, late MIDI %llu, trigger underruns %llu\n",
         outPath, double(outL.size()) / rate, rate, block, eng.latencySamples(), keepLatency ? "kept" : "compensated", pk,
         (unsigned long long)eng.lateMidi(), (unsigned long long)eng.trigUnderruns());
  if (savePath) {
    auto st = eng.getState();
    FILE* f = fopen(savePath, "wb");
    if (!f || fwrite(st.data(), 1, st.size(), f) != st.size()) { fprintf(stderr, "cannot write %s\n", savePath); return 1; }
    fclose(f);
    printf("saved state %s (%zu bytes)\n", savePath, st.size());
  }
  return 0;
}
