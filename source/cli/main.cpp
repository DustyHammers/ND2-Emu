// nd2emu: ND2 emulator (original OS v3.00), command-line renderer and debugger
//
//   nd2emu -os <os.cab|Update.exe> [-flash flash.bin] [-saveflash out.bin] -t <seconds> -o out.wav
//          [-note t,ch,note,vel] [-pgm t,ch,prog] [-bank t,ch,bank] [-midi "t,hex hex .."]
//          [-hit t,an,amp[,freq,decay_ms]] [-pad t,pad,strength] [-press t,idx[,dur]] [-enc t,detents] [-panel t] [-syx t file.syx] [-midiout file] [-from seconds] [-check ram_init.bin]
//          [-master 0..1] [-adc an,v] [-bp hexpc] [-tr lo hi t] [-dump addr,len] [-pcprof file] [-stats]
#include "device.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

using namespace nd2;
namespace nd2 { uint64_t ff_stat_calls(); uint64_t ff_stat_skipped(); }

static std::vector<uint8_t> readfile(const char* fn) {
  std::vector<uint8_t> b; FILE* f = fopen(fn, "rb"); if (!f) return b;
  fseek(f, 0, SEEK_END); b.resize(ftell(f)); fseek(f, 0, SEEK_SET);
  if (fread(b.data(), 1, b.size(), f) != b.size()) b.clear();
  fclose(f); return b;
}
static void writefile(const char* fn, const void* p, size_t n) { FILE* f = fopen(fn, "wb"); if (f) { fwrite(p, 1, n, f); fclose(f); } }

static void write_wav(const char* fn, const std::vector<int32_t>& s, double fs, int shift) {
  FILE* f = fopen(fn, "wb"); if (!f) return;
  uint32_t n = (uint32_t)(s.size() / 2), data = n * 4;
  auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); }; auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); u32(36 + data); fwrite("WAVEfmt ", 1, 8, f); u32(16); u16(1); u16(2);
  u32((uint32_t)lround(fs)); u32((uint32_t)lround(fs) * 4); u16(4); u16(16); fwrite("data", 1, 4, f); u32(data);
  for (auto v : s) { int64_t x = (int64_t)v >> shift; x = std::max<int64_t>(-32768, std::min<int64_t>(32767, x)); u16((uint16_t)(int16_t)x); }
  fclose(f);
}

static uint32_t brng = 12345;
struct BVox { int ch = 1; std::string mode; double amt = 0; int32_t hold = 0; int count = 0; };
static std::vector<BVox> bvox;
static int bskip[6] = {0, 0, 0, 0, 0, 0}; static int bskip_n[6] = {0, 0, 0, 0, 0, 0};
static void vox_hook(dspic::CPU& c, void*) {
  for (auto& v : bvox) {
    const uint32_t a = 0x1E8E + 4 * (v.ch - 1);
    int32_t x = (int32_t)(((uint32_t)c.ld16(a) << 16) | c.ld16(a + 2));
    if (v.mode == "crush") { int bits = (int)v.amt; uint32_t keep = ~((1u << (32 - bits)) - 1); x = x >= 0 ? (int32_t)((uint32_t)x & keep) : -(int32_t)((uint32_t)(-(int64_t)x) & keep); }
    else if (v.mode == "hold") { if (v.count++ % (int)v.amt == 0) v.hold = x; x = v.hold; }
    else if (v.mode == "wrap") { x = (int32_t)(uint32_t)((int64_t)x * (int64_t)v.amt); }
    else if (v.mode == "fold") { double y = x / 2147483648.0 * v.amt; while (y > 1 || y < -1) y = y > 1 ? 2 - y : -2 - y; x = (int32_t)(y * 2147483647.0); }
    else if (v.mode == "flip") { x ^= (int32_t)(1u << (int)v.amt); }
    else if (v.mode == "crackle") { brng = brng * 1664525u + 1013904223u; if ((brng >> 8) / 16777216.0 < v.amt) { brng = brng * 1664525u + 1013904223u; x ^= (int32_t)(1u << (26 + (brng >> 29))); } }
    c.st16(a, (uint16_t)((uint32_t)x >> 16)); c.st16(a + 2, (uint16_t)x);
  }
}
static void skip_hook(dspic::CPU& c, void* ctx) {
  const int ch = (int)(intptr_t)ctx;
  if (bskip[ch] > 1 && (bskip_n[ch]++ % bskip[ch]) != 0) c.st16(0, (uint16_t)(0x3F00 + 4 * ch));   // skip the engine this sample
}
// time stretch / freeze: the control scheduler (inc 0x116C at 0x1ACBC) is skipped on some samples
static int bstretch = 1; static double bjitter = 0; static std::vector<std::pair<double, double>> bfreeze;
static uint64_t bsched_n = 0;
static uint32_t bgroups = 0xFFFFFFFF; static uint64_t bgrp_n[32] = {};
static void sched_hook(dspic::CPU& c, void*) {
  const double t = c.cycles / nd2::FCY;
  const uint16_t nxt = (uint16_t)(c.ld16(0x116C) + 1); const int grp = nxt ? __builtin_ctz(nxt) : 16;
  bool skip = bstretch > 1 && ((bgroups >> grp) & 1) && (bgrp_n[grp]++ % (uint64_t)bstretch) != 0;
  for (auto& w : bfreeze) if (t >= w.first && t < w.second) skip = true;
  if (bjitter > 0) { static double p = bjitter * 0.5; brng = brng * 1664525u + 1013904223u; p += ((brng >> 8) / 16777216.0 - 0.5) * 0.004; p = std::max(0.0, std::min(bjitter, p));
    brng = brng * 1664525u + 1013904223u; if ((brng >> 8) / 16777216.0 < p) skip = true; }
  bsched_n++;
  c.bend_setz(skip);
}
// brownouts: CPU starve in short random bursts; crackle: sparse bit hits per voice
static bool bsched_an = false, bvs_any = false;
// per-voice stretch: the scheduler's per-channel handler calls (Fs/16 group at 0x192FC + 4ch, Fs/256 group at
// 0x1A2AA + 4ch) are skipped on N-1 of N calls for the stretched channels, through trampolines at 0x3F80 + 4i
static int bvstretch[6] = {1, 1, 1, 1, 1, 1}; static uint64_t bvs_diff[6] = {}, bvs_calls = 0; static uint64_t bvs_n[12] = {};
struct VSite { uint32_t pc; int ch; int idx; };
static VSite vsites[12];
static void vstretch_hook(dspic::CPU& c, void* ctx) {
  const VSite* v = static_cast<const VSite*>(ctx);
  const int n = bvstretch[v->ch];
  if (n > 1 && (bvs_n[v->idx]++ % (uint64_t)n) != 0) c.st16(0, (uint16_t)(0x3F80 + 4 * v->idx));
}
static double bbrown = 0; static uint64_t bbrown_until = 0;
// random roll: a random set of bends; undone at broll_reset (all bends are reversible)
static uint32_t broll_seed = 0; static double broll_at = 1.5, broll_reset = 0; static std::string broll_desc;
struct Saved { uint16_t sine[2048], lcg[4], decay[256]; uint32_t mix[2]; };
static Saved bsaved;
static uint32_t rr() { brng = brng * 1664525u + 1013904223u; return brng >> 8; }
static double ru() { return rr() / 16777216.0; }
struct FeedBack : nd2::Board::AdcSource {
  nd2::Board* b; double g; unsigned mask;
  void next_block(double* add) override {
    if (b->audio.size() < 2) return;
    const double x = b->audio[b->audio.size() - 2] / 2147483648.0 * g;
    static const int an[6] = {12, 2, 4, 6, 8, 10};
    for (int p = 0; p < 6; p++) if (mask & (1u << p)) add[an[p]] += x;
  }
};
int main(int argc, char** argv) {
  const char* os_path = getenv("ND2_OS");
  double secs = 1.0, wav_from = 0, syx_t = 0, tr_t = 0;
  const char *wav = nullptr, *flash_in = nullptr, *flash_out = nullptr, *midiout_path = nullptr, *raminit = nullptr, *pcprof = nullptr;
  bool stats = false;
  bool bint = false; double bfb = 0; unsigned bfbmask = 0x3F;
  int bframe = 0, bdac = 0; bool bnosat = false, bfind = false; const char* bsine = nullptr;
  std::vector<std::pair<uint32_t, uint32_t>> bpatch, bram;
  std::vector<std::pair<double, std::vector<uint8_t>>> midi;
  std::vector<uint8_t> syx;
  std::vector<std::pair<int, uint16_t>> ports;
  std::vector<std::pair<int, double>> adcs; double master = 1.0;
  struct UiEv { double t; int kind, a, b; }; std::vector<UiEv> uis; size_t ui = 0;
  struct HitEv { double t; int an; double amp, f, dec; }; std::vector<HitEv> hits; size_t hi = 0;
  std::vector<uint32_t> bps; std::vector<std::pair<uint32_t, uint32_t>> dumps;
  uint32_t tr_lo = 0, tr_hi = 0; int tr_n = 0;
  for (int i = 1; i < argc; i++) {
    auto arg = [&](const char* s) { return !strcmp(argv[i], s) && i + 1 < argc; };
    if (arg("-os")) os_path = argv[++i];
    else if (arg("-t")) secs = atof(argv[++i]);
    else if (arg("-o")) wav = argv[++i];
    else if (arg("-from")) wav_from = atof(argv[++i]);
    else if (arg("-flash")) flash_in = argv[++i];
    else if (arg("-saveflash")) flash_out = argv[++i];
    else if (arg("-midiout")) midiout_path = argv[++i];
    else if (arg("-check")) raminit = argv[++i];
    else if (arg("-pcprof")) pcprof = argv[++i];
    else if (!strcmp(argv[i], "-stats")) stats = true;
    else if (arg("-bframe")) bframe = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-bnosat")) bnosat = true;
    else if (arg("-bsine")) bsine = argv[++i];
    else if (arg("-bdac")) bdac = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-bfind")) bfind = true;
    else if (!strcmp(argv[i], "-bint")) bint = true;
    else if (arg("-bstretch")) bstretch = atoi(argv[++i]);
    else if (arg("-bgroups")) bgroups = strtoul(argv[++i], nullptr, 16);
    else if (arg("-bjitter")) bjitter = atof(argv[++i]);
    else if (arg("-bfreeze")) { double a, b; sscanf(argv[++i], "%lf,%lf", &a, &b); bfreeze.push_back({a, b}); }
    else if (arg("-bbrown")) bbrown = atof(argv[++i]);
    else if (!strcmp(argv[i], "-bsched")) bsched_an = true;
    else if (arg("-bvstretch")) { int ch, n; sscanf(argv[++i], "%d,%d", &ch, &n); bvstretch[ch - 1] = n; bvs_any = true; }
    else if (arg("-broll")) { sscanf(argv[++i], "%u,%lf,%lf", &broll_seed, &broll_at, &broll_reset); }
    else if (arg("-bfb")) { sscanf(argv[++i], "%lf,%x", &bfb, &bfbmask); }
    else if (arg("-bfill")) { unsigned a, n, v; sscanf(argv[++i], "%x,%u,%x", &a, &n, &v); for (unsigned k = 0; k < n; k++) bram.push_back({a + 2 * k, v}); }
    else if (arg("-bvox")) { BVox v; char m[16]; sscanf(argv[++i], "%d,%15[^,],%lf", &v.ch, m, &v.amt); v.mode = m; bvox.push_back(v); }
    else if (arg("-bskip")) { int ch, n; sscanf(argv[++i], "%d,%d", &ch, &n); bskip[ch - 1] = n; }
    else if (arg("-bram")) { unsigned a, v; sscanf(argv[++i], "%x,%x", &a, &v); bram.push_back({a, v}); }
    else if (arg("-bpatch")) { unsigned a, v; sscanf(argv[++i], "%x,%x", &a, &v); bpatch.push_back({a, v}); }
    else if (arg("-tr")) { tr_lo = strtoul(argv[++i], nullptr, 16); tr_hi = strtoul(argv[++i], nullptr, 16); tr_t = atof(argv[++i]); }
    else if (arg("-dump")) { uint32_t a, n; sscanf(argv[++i], "%x,%x", &a, &n); dumps.push_back({a, n}); }
    else if (arg("-bp")) bps.push_back(strtoul(argv[++i], nullptr, 16));
    else if (arg("-syx")) { syx_t = atof(argv[++i]); syx = readfile(argv[++i]); }
    else if (arg("-note")) { double t; int ch, nn, vel; sscanf(argv[++i], "%lf,%d,%d,%d", &t, &ch, &nn, &vel);
      midi.push_back({t, {(uint8_t)(0x90 | ((ch - 1) & 15)), (uint8_t)nn, (uint8_t)vel}}); }
    else if (arg("-pgm")) { double t; int ch, p; sscanf(argv[++i], "%lf,%d,%d", &t, &ch, &p);
      midi.push_back({t, {(uint8_t)(0xC0 | ((ch - 1) & 15)), (uint8_t)(p - 1)}}); }
    else if (arg("-bank")) { double t; int ch, bk; sscanf(argv[++i], "%lf,%d,%d", &t, &ch, &bk);   // CC0 = 0, CC32 = bank-1
      midi.push_back({t, {(uint8_t)(0xB0 | ((ch - 1) & 15)), 0, 0, 32, (uint8_t)(bk - 1)}}); }
    else if (arg("-master")) master = atof(argv[++i]);
    else if (arg("-adc")) { int an; double v; sscanf(argv[++i], "%d,%lf", &an, &v); adcs.push_back({an, v}); }
    else if (arg("-pad")) { double t, s; int p; sscanf(argv[++i], "%lf,%d,%lf", &t, &p, &s);
      static const int an[6] = {12, 2, 4, 6, 8, 10}; if (p >= 1 && p <= 6) hits.push_back({t, an[p - 1], s, 400, 4}); }
    else if (arg("-hit")) { double t, amp, f = 400, dec = 4; int an; sscanf(argv[++i], "%lf,%d,%lf,%lf,%lf", &t, &an, &amp, &f, &dec);
      hits.push_back({t, an, amp, f, dec}); }
    else if (arg("-port")) { char p; unsigned v; sscanf(argv[++i], "%c,%x", &p, &v); ports.push_back({p - 'A', (uint16_t)v}); }
    else if (arg("-press")) { double t, dur = 0.1; int idx; sscanf(argv[++i], "%lf,%d,%lf", &t, &idx, &dur);
      uis.push_back({t, 0, idx, 1}); uis.push_back({t + dur, 0, idx, 0}); }
    else if (arg("-enc")) { double t; int n; sscanf(argv[++i], "%lf,%d", &t, &n); uis.push_back({t, 1, n, 0}); }
    else if (arg("-panel")) { double t; sscanf(argv[++i], "%lf", &t); uis.push_back({t - 0.05, 2, 0, 0}); uis.push_back({t, 3, 0, 0}); }
    else if (arg("-midi")) { double t; char buf[512]; sscanf(argv[++i], "%lf,%511[^\n]", &t, buf);
      std::vector<uint8_t> v; for (char* p = strtok(buf, " "); p; p = strtok(nullptr, " ")) v.push_back((uint8_t)strtoul(p, nullptr, 16));
      midi.push_back({t, v}); }
    else { fprintf(stderr, "unknown or incomplete option %s\n", argv[i]); return 2; }
  }
  if (!os_path) { fprintf(stderr, "need -os <OS update file: os.cab or the updater> (or ND2_OS env)\n"); return 2; }

  OsImage os; std::string err;
  if (!loadOsUpdateFile(os_path, os, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  Device dev;
  if (!dev.setOs(os, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  if (flash_in) {
    auto f = readfile(flash_in);
    if (!f.empty()) { dev.setFlash(f); printf("loaded flash image %s\n", flash_in); } else printf("no flash image at %s (erased)\n", flash_in);
  }
  Board& B = dev.board(); auto& C = B.cpu;
  dev.setMasterLevel(master);

  if (raminit) {   // boot check: crt0 must reproduce the .dinit RAM image
    B.load_app(os.image); B.boot();
    while (!C.halted && C.pc != 0x333F6 && C.instret < 50000000) C.step();
    auto ri = readfile(raminit); int diff = 0;
    if (ri.size() >= 0xE000) for (uint32_t a = 0x1000; a < 0xE000; a++) diff += C.ram[a] != ri[a];
    printf("main reached at %llu instructions; RAM vs .dinit image: %d bytes differ (live stack only expected)\n", (unsigned long long)C.instret, diff);
  }
  for (auto& a : adcs) B.set_adc(a.first, a.second);
  for (auto& p : ports) if (p.first >= 1 && p.first <= 6) B.port_in[p.first] = p.second;   // B..G
  auto tb = std::chrono::steady_clock::now();
  dev.boot();
  printf("booted to main loop at %.3f s emulated, cycle %llu, ram hash %016llx (%.2f s wall)%s\n", C.cycles / FCY, (unsigned long long)C.cycles, [&]{ uint64_t h=1469598103934665603ULL; for (int i=0x60;i<0x10000;i++){ h^=C.ram[i]; h*=1099511628211ULL;} return (unsigned long long)h; }(),
         std::chrono::duration<double>(std::chrono::steady_clock::now() - tb).count(), C.halted ? " HALTED" : "");

  // BEND experiments (scratch only)
  if (bfind) for (size_t i = 0; i < C.prog.size(); i++) { uint32_t w = C.prog[i];
    if ((w & 0xF00000) == 0x200000) { uint32_t lit = (w >> 4) & 0xFFFF;
      if (lit == 0x8435 || lit == 0x0BB3 || lit == 0x636B || lit == 0x3619) printf("mov #%04x,w%u at %06zx\n", lit, w & 15, i * 2); }
    uint32_t lo = w & 0xFFFF; if (lo == 0x8435 || lo == 0x0BB3 || lo == 0x636B || lo == 0x3619) printf("word %06x at %06zx\n", w, i * 2); }
  if (bfind) for (uint32_t a = 0x1000; a < 0xE000; a += 2) { uint16_t v = C.ram[a] | (C.ram[a+1] << 8); if (v == 0x8435 || v == 0x0BB3 || v == 0x636B || v == 0x3619) printf("ram %04x = %04x\n", a, v); }
  for (auto& p : bpatch) { printf("patch %06x: %06x -> %06x\n", p.first, C.prog[p.first / 2], p.second); C.prog[p.first / 2] = p.second; C.invalidate(p.first); }
  for (auto& p : bram) { printf("ram %04x: %04x -> %04x\n", p.first, C.ram[p.first] | (C.ram[p.first+1] << 8), p.second); C.ram[p.first] = p.second & 0xFF; C.ram[p.first+1] = p.second >> 8; }
  if (bstretch > 1 || bjitter > 0 || !bfreeze.empty() || broll_seed) {   // trampoline at 0x3F40 around the scheduler
    const uint32_t T = 0x3F40; auto P = [&](uint32_t a, uint32_t w) { C.prog[a / 2] = w; C.invalidate(a); };
    const uint32_t inc = C.prog[0x1ACBC / 2], bt0 = C.prog[0x1ACBE / 2], g1 = C.prog[0x1ACC0 / 2], g2 = C.prog[0x1ACC2 / 2];
    P(T + 0, 0x320006);                       // bra z, T+14
    P(T + 2, inc); P(T + 4, bt0); P(T + 6, g1); P(T + 8, g2);
    P(T + 10, 0x040000 | (0x1ACC4 & 0xFFFE)); P(T + 12, 0x1ACC4 >> 16);
    P(T + 14, 0x040000 | (0x343E0 & 0xFFFE)); P(T + 16, 0x343E0 >> 16);
    P(0x1ACBC, 0x040000 | T); P(0x1ACBE, 0);
    for (uint32_t a = T; a <= T + 16; a += 2) printf("  %06x %s\n", a, C.disasm_at(a).c_str());
    C.add_hook(T, sched_hook, nullptr);
    printf("bend: stretch %d, jitter %.2f, %zu freezes\n", bstretch, bjitter, bfreeze.size()); }
  if (bint) { C.bend_int = true; C.update_ipl(); printf("bend: fractional/integer swapped\n"); }
  if (!bvox.empty() || broll_seed) { C.add_hook(0x1A9EE, vox_hook, nullptr); printf("bend: %zu voice bends\n", bvox.size()); }
  for (int k = 0; k < 6; k++) { uint32_t t = 0x1A68E + 0x90 * k, a = 0x3F00 + 4 * k;   // trampolines back to the dispatch
    C.prog[a / 2] = 0x040000 | (t & 0xFFFE); C.prog[a / 2 + 1] = (t >> 16) & 0x7F; C.invalidate(a); C.invalidate(a + 2); }
  if (broll_seed) for (int k = 0; k < 6; k++) C.add_hook(0x1A68C + 0x90 * k, skip_hook, (void*)(intptr_t)k);
  else for (int k = 0; k < 6; k++) if (bskip[k] > 1) { C.add_hook(0x1A68C + 0x90 * k, skip_hook, (void*)(intptr_t)k); printf("bend: channel %d engine 1 in %d\n", k + 1, bskip[k]); }
  static FeedBack fbk; fbk.b = &B; if (broll_seed) B.adc_live = &fbk;
  if (bfb != 0) { fbk.b = &B; fbk.g = bfb; fbk.mask = bfbmask; B.adc_live = &fbk; printf("bend: feedback %.2f into pads %x\n", bfb, bfbmask); }
  if (bframe) { B.frame_cycles = bframe; printf("bend: %d cycles per frame\n", bframe); }
  if (bnosat) { C.bend_nosat = true; C.update_ipl(); printf("bend: saturation off\n"); }
  if (bsine) {   // the 2048-point sine table at RAM 0x39B8 (built at boot)
    uint16_t t[2048], o[2048]; for (int i = 0; i < 2048; i++) t[i] = C.ram[0x39B8 + 2 * i] | (C.ram[0x39B9 + 2 * i] << 8);
    std::string m = bsine;
    for (int i = 0; i < 2048; i++) {
      int j = i; uint16_t v = t[i];
      if (m == "swap") { int b2 = (i >> 2) & 1, b9 = (i >> 9) & 1; j = (i & ~((1 << 2) | (1 << 9))) | (b9 << 2) | (b2 << 9); v = t[j]; }
      else if (m == "stuck") v = t[i] | 0x1000;
      else if (m == "crush") v = t[i] & 0xE000;
      else if (m == "gray") v = t[i] ^ (t[i] >> 1);
      o[i] = v; }
    for (int i = 0; i < 2048; i++) { C.ram[0x39B8 + 2 * i] = o[i] & 0xFF; C.ram[0x39B9 + 2 * i] = o[i] >> 8; }
    printf("bend: sine table %s\n", bsine); }

  if (bvs_any && getenv("BVMAP")) {   // per-voice stretch v2: hold the channel's scheduler-owned bytes on skipped samples
    static std::vector<uint16_t> own[6]; static uint8_t keep[6][512]; static bool skip_now[6]; static uint64_t vn[6];
    FILE* f = fopen(getenv("BVMAP"), "r"); unsigned a, k; while (f && fscanf(f, "%x %u", &a, &k) == 2) if (k >= 1 && k <= 6) own[k - 1].push_back((uint16_t)a); if (f) fclose(f);
    if (getenv("BVENV")) {   // per-voice stretch v5: the channel's envelope words, held back on skipped passes when they only took a small step
      static std::vector<uint16_t> ew[6]; static uint16_t ek[6][64]; static int thr = getenv("BVTHR") ? (int)strtol(getenv("BVTHR"), nullptr, 16) : 0x400;
      FILE* g = fopen(getenv("BVENV"), "r"); unsigned a2, k2; while (g && fscanf(g, "%x %u", &a2, &k2) == 2) if (k2 >= 1 && k2 <= 6) ew[k2 - 1].push_back((uint16_t)a2); if (g) fclose(g);
      C.add_hook(0x343E0, [](dspic::CPU& c, void*) {
        bvs_calls++;
        for (int k = 0; k < 6; k++) {
          if (skip_now[k]) for (size_t i = 0; i < ew[k].size(); i++) { const int d = (int16_t)(c.ld16(ew[k][i]) - ek[k][i]);
            if (d != 0 && d >= -thr && d <= thr) { c.st16(ew[k][i], ek[k][i]); bvs_diff[k]++; } }
          skip_now[k] = bvstretch[k] > 1 && (vn[k]++ % (uint64_t)bvstretch[k]) != 0;
          for (size_t i = 0; i < ew[k].size(); i++) ek[k][i] = c.ld16(ew[k][i]);
        } }, nullptr);
    } else if (getenv("BVTIME")) {   // per-voice stretch v4: the channel's envelope time = hi word 0x1F1C+0x18k : lo word 0xD090+8k, held back on skipped passes when it moved forward
      C.add_hook(0x343E0, [](dspic::CPU& c, void*) {
        static uint32_t tk[6];
        bvs_calls++;
        for (int k = 0; k < 6; k++) {
          const uint16_t ah = (uint16_t)(0x1F1C + 0x18 * k), al = (uint16_t)(0xD090 + 8 * k);
          const uint32_t now = ((uint32_t)c.ld16(ah) << 16) | c.ld16(al);
          if (skip_now[k] && now > tk[k] && now - tk[k] < 0x40000u) { c.st16(ah, (uint16_t)(tk[k] >> 16)); c.st16(al, (uint16_t)tk[k]); bvs_diff[k]++; }
          skip_now[k] = bvstretch[k] > 1 && (vn[k]++ % (uint64_t)bvstretch[k]) != 0;
          tk[k] = ((uint32_t)c.ld16(ah) << 16) | c.ld16(al);
        } }, nullptr);
    } else if (getenv("BVCTR")) {   // per-voice stretch v3: hold back the channel's envelope time counters (16-bit words) when they step up
      static std::vector<uint16_t> ctr[6]; static uint16_t ckeep[6][64];
      FILE* g = fopen(getenv("BVCTR"), "r"); unsigned a2, k2; while (g && fscanf(g, "%x %u", &a2, &k2) == 2) if (k2 >= 1 && k2 <= 6) ctr[k2 - 1].push_back((uint16_t)a2); if (g) fclose(g);
      C.add_hook(0x343E0, [](dspic::CPU& c, void*) {
        bvs_calls++;
        for (int k = 0; k < 6; k++) {
          for (size_t i = 0; i < ctr[k].size(); i++) { const uint16_t now = c.ld16(ctr[k][i]);
            if (skip_now[k] && now > ckeep[k][i] && now - ckeep[k][i] <= 4) { c.st16(ctr[k][i], ckeep[k][i]); bvs_diff[k]++; } }
          skip_now[k] = bvstretch[k] > 1 && (vn[k]++ % (uint64_t)bvstretch[k]) != 0;
          for (size_t i = 0; i < ctr[k].size(); i++) ckeep[k][i] = c.ld16(ctr[k][i]);
        } }, nullptr);
    } else if (getenv("BVPASS")) {   // hold across the whole pass (render + scheduler): restore at the loop head what the previous loop head saw
      C.add_hook(0x343E0, [](dspic::CPU& c, void*) {
        bvs_calls++;
        for (int k = 0; k < 6; k++) {
          if (skip_now[k]) { for (size_t i = 0; i < own[k].size(); i++) { bvs_diff[k] += c.ram[own[k][i]] != keep[k][i]; c.ram[own[k][i]] = keep[k][i]; } }
          skip_now[k] = bvstretch[k] > 1 && (vn[k]++ % (uint64_t)bvstretch[k]) != 0;
          for (size_t i = 0; i < own[k].size(); i++) keep[k][i] = c.ram[own[k][i]];
        } }, nullptr);
    } else {
    C.add_hook(0x1ACBC, [](dspic::CPU& c, void*) {
      for (int k = 0; k < 6; k++) { skip_now[k] = bvstretch[k] > 1 && (vn[k]++ % (uint64_t)bvstretch[k]) != 0;
        if (skip_now[k]) for (size_t i = 0; i < own[k].size(); i++) keep[k][i] = c.ram[own[k][i]]; } }, nullptr);
    C.add_hook(0x343E0, [](dspic::CPU& c, void*) {
      bvs_calls++;
      for (int k = 0; k < 6; k++) if (skip_now[k]) { skip_now[k] = false; for (size_t i = 0; i < own[k].size(); i++) { bvs_diff[k] += c.ram[own[k][i]] != keep[k][i]; c.ram[own[k][i]] = keep[k][i]; } } }, nullptr);
    }
    printf("bend: per-voice stretch v2 (%zu %zu %zu %zu %zu %zu bytes)\n", own[0].size(), own[1].size(), own[2].size(), own[3].size(), own[4].size(), own[5].size());
  } else if (bvs_any) {
    for (int i = 0; i < 12; i++) {
      const uint32_t pc = (i < 6 ? 0x192FC : 0x1A2AA) + 4 * (i % 6), ret = pc + 2, a = 0x3F80 + 4 * i;
      vsites[i] = {pc, i % 6, i};
      C.prog[a / 2] = 0x040000 | (ret & 0xFFFE); C.prog[a / 2 + 1] = (ret >> 16) & 0x7F; C.invalidate(a); C.invalidate(a + 2);
      C.add_hook(pc, vstretch_hook, &vsites[i]);
    }
    for (uint32_t pc : {0x192FAu, 0x192FCu, 0x192FEu, 0x1A2A8u, 0x1A2AAu, 0x1A2ACu, 0x1A2BCu, 0x1A2BEu, 0x1A2C0u}) printf("  %06x %s\n", pc, C.disasm_at(pc).c_str());
    printf("bend: per-voice stretch %d %d %d %d %d %d\n", bvstretch[0], bvstretch[1], bvstretch[2], bvstretch[3], bvstretch[4], bvstretch[5]);
  }
  // scheduler write map: RAM bytes changed by scheduler tasks between t0 and t1 (BMAP=t0,t1,outfile)
  static std::vector<uint8_t> snap(0x10000); static std::vector<uint32_t> wcount(0x10000); static bool in_t = false;
  static double map_t0 = 0, map_t1 = 0; static std::string map_out;
  if (const char* bm = getenv("BMAP")) { char f[256]; sscanf(bm, "%lf,%lf,%255s", &map_t0, &map_t1, f); map_out = f;
    C.trace = [&](uint32_t pc, const dspic::Insn&) {
      const double t = C.cycles / FCY; if (t < map_t0 || t > map_t1) return;
      if (pc == 0x1ACBC) { memcpy(snap.data() + 0x1000, C.ram + 0x1000, 0xE000 - 0x1000); in_t = true; }
      else if (pc == 0x343E0 && in_t) { in_t = false; for (uint32_t a = 0x1000; a < 0xE000; a++) if (C.ram[a] != snap[a]) wcount[a]++; }
    }; }
  static std::vector<uint8_t> chg0(0x10000); static double chg_t0 = -1, chg_t1 = -1; static std::string chg_out; static bool chg_taken = false;
  if (const char* bc = getenv("BCHG")) { char f[256]; sscanf(bc, "%lf,%lf,%255s", &chg_t0, &chg_t1, f); chg_out = f; }
  static std::vector<uint16_t> blog; static uint16_t blast[16]; static uint64_t bpass = 0;
  if (const char* bl = getenv("BLOG")) { std::string t = bl; size_t p0 = 0; while (p0 < t.size()) { size_t q = t.find(',', p0); if (q == std::string::npos) q = t.size(); blog.push_back((uint16_t)strtoul(t.substr(p0, q - p0).c_str(), nullptr, 16)); p0 = q + 1; }
    C.add_hook(0x343E0, [](dspic::CPU& c, void*) { bpass++; double t = c.cycles / nd2::FCY; if (t < 1.599 || t > 1.75) return;
      bool ch = false; for (size_t i = 0; i < blog.size(); i++) { uint16_t v = c.ld16(blog[i]); if (v != blast[i]) ch = true; blast[i] = v; }
      if (ch) { printf("%.5f %llu:", t, (unsigned long long)bpass); for (size_t i = 0; i < blog.size(); i++) printf(" %04x", blast[i]); printf("\n"); } }, nullptr); }
  // scheduler analysis: goto Wn sites executed between the scheduler entry (0x1ACBC) and the T5 loop (0x343E0)
  struct GS { uint64_t n = 0; std::map<uint32_t, uint64_t> targets; uint32_t grp_mask = 0; };
  static std::map<uint32_t, GS> gsites; static bool in_s = false; static uint32_t last_goto = 0;
  static int dump_left = getenv("BDUMP") ? 1 : 0; static bool dumping = false; static FILE* df = nullptr;
  if (bsched_an && dump_left) C.trace = [&](uint32_t pc, const dspic::Insn&) {
    if (C.cycles / FCY < 2.0) return;
    if (!dumping && pc == (uint32_t)strtoul(getenv("BDUMP"), nullptr, 16)) { dumping = true; df = fopen("sched_dump.txt", "w"); }
    if (dumping) { fprintf(df, "%06x %s | W0=%04x W1=%04x W2=%04x W8=%04x W9=%04x W10=%04x W11=%04x W12=%04x W13=%04x\n", pc, C.disasm_at(pc).c_str(), C.W(0), C.W(1), C.W(2), C.W(8), C.W(9), C.W(10), C.W(11), C.W(12), C.W(13));
      if (pc == 0x343E0) { dumping = false; fclose(df); dump_left = 0; } }
  };
  else if (bsched_an) C.trace = [&](uint32_t pc, const dspic::Insn&) {
    if (C.cycles / FCY < 2.0) return;
    if (pc == 0x1ACBC) in_s = true;
    else if (pc == 0x343E0) in_s = false;
    if (!in_s) return;
    std::string d = C.disasm_at(pc);
    if (d.rfind("goto W", 0) == 0) { int r = d[6] - '0'; auto& g = gsites[pc]; g.n++; g.targets[C.W(r)]++;
      uint16_t cnt = C.ld16(0x116C); int grp = cnt ? __builtin_ctz(cnt) : 16; g.grp_mask |= 1u << std::min(grp, 31); }
  };

  std::map<uint32_t, int> bphits;
  std::vector<uint32_t> pcc;
  if (pcprof) pcc.assign(0x40000, 0);
  if (!bps.empty() || tr_hi || pcprof) C.trace = [&](uint32_t pc, const dspic::Insn&) {
    if (pcprof) pcc[pc >> 1]++;
    if (pc >= tr_lo && pc < tr_hi && C.cycles / FCY >= tr_t && tr_n < 40000) { tr_n++;
      printf("  %06x %-36s W0=%04x W1=%04x W2=%04x W3=%04x W4=%04x W5=%04x W6=%04x W7=%04x W8=%04x W9=%04x SR=%04x\n", pc, C.disasm_at(pc).c_str(),
        C.W(0), C.W(1), C.W(2), C.W(3), C.W(4), C.W(5), C.W(6), C.W(7), C.W(8), C.W(9), C.sr()); }
    for (auto b : bps) if (pc == b && bphits[b]++ < 4)
      printf("bp %06x t=%.4f ipl=%d W0=%04x W1=%04x W2=%04x W3=%04x W14=%04x W15=%04x\n", pc, C.cycles / FCY, (C.sr() >> 5) & 7, C.W(0), C.W(1), C.W(2), C.W(3), C.W(14), C.W(15));
  };

  // timed run: times are measured from power-on
  std::sort(midi.begin(), midi.end(), [](auto& a, auto& b) { return a.first < b.first; });
  std::stable_sort(uis.begin(), uis.end(), [](auto& a, auto& b) { return a.t < b.t; });
  std::sort(hits.begin(), hits.end(), [](auto& a, auto& b) { return a.t < b.t; });
  size_t mi = 0, sx_pos = 0, scan = 0, msg_start = 0; int sent = 0, replies = 0; double last_send = 0; bool sx_done = syx.empty();
  B.audio.clear();
  uint64_t audio_origin = C.cycles;
  auto t0 = std::chrono::steady_clock::now(); uint64_t i0 = C.instret, c0 = C.cycles;
  const uint64_t end = (uint64_t)(secs * FCY);
  while (!C.halted && C.cycles < end) {
    double now = C.cycles / FCY;
    while (mi < midi.size() && (uint64_t)std::ceil(midi[mi].first * FCY) <= C.cycles) B.midi_in(midi[mi++].second);
    while (ui < uis.size() && (uint64_t)std::ceil(uis[ui].t * FCY) <= C.cycles) {
      auto& u = uis[ui++];
      if (u.kind == 0) B.set_button(u.a, u.b);
      else if (u.kind == 1) B.encoder_turn(u.a);
      else if (u.kind == 2) B.panel(true);
      else { auto p = B.panel(true); printf("panel @%.2f s: [%s]  LEDs on:", u.t, p.display.c_str());
        for (int k = 0; k < 32; k++) if (p.led_duty[k] > 0.25) printf(" %d", k); printf("\n"); }
    }
    while (hi < hits.size() && (uint64_t)std::ceil(hits[hi].t * FCY) <= C.cycles) { B.piezo_hit(hits[hi].an, hits[hi].amp, hits[hi].f, hits[hi].dec); hi++; }
    if (!sx_done && now >= syx_t) {      // stream SysEx one message at a time, waiting for each reply from the drum
      for (; scan < B.midi_out.size(); scan++) {
        if (B.midi_out[scan] == 0xF0) msg_start = scan;
        if (B.midi_out[scan] == 0xF7 && scan > msg_start + 6 && B.midi_out[msg_start + 3] == 0x19) replies++;
      }
      if (replies == sent) {
        if (sx_pos >= syx.size()) { sx_done = true; printf("sysex: %d/%d replied at %.2f s\n", replies, sent, now); }
        else {
          size_t e = sx_pos; while (e < syx.size() && syx[e] != 0xF7) e++;
          B.midi_in(std::vector<uint8_t>(syx.begin() + sx_pos, syx.begin() + std::min(e + 1, syx.size())));
          sx_pos = e + 1; sent++; last_send = now;
        }
      } else if (now - last_send > 3.0) { printf("no reply to message %d after 3 s\n", sent); sx_done = true; }
    }
    // run to the next MIDI event (cycle-exact injection) or at most 4096 cycles
    uint64_t stop = std::min<uint64_t>(end, C.cycles + 4096);
    if (mi < midi.size()) stop = std::min<uint64_t>(stop, std::max<uint64_t>(C.cycles + 1, (uint64_t)std::ceil(midi[mi].first * FCY)));
    if (ui < uis.size()) stop = std::min<uint64_t>(stop, std::max<uint64_t>(C.cycles + 1, (uint64_t)std::ceil(uis[ui].t * FCY)));
    if (hi < hits.size()) stop = std::min<uint64_t>(stop, std::max<uint64_t>(C.cycles + 1, (uint64_t)std::ceil(hits[hi].t * FCY)));
    if (broll_seed && broll_at >= 0 && now >= broll_at) {
      broll_at = -1; brng = broll_seed * 2654435761u + 1;
      for (int i = 0; i < 2048; i++) bsaved.sine[i] = C.ld16(0x39B8 + 2 * i);
      bsaved.lcg[0] = C.ld16(0x1DFA); bsaved.lcg[1] = C.ld16(0xDDB4); bsaved.lcg[2] = C.ld16(0x1DFC); bsaved.lcg[3] = C.ld16(0xDDB6);
      for (int i = 0; i < 256; i++) bsaved.decay[i] = C.ld16(0xAD62 + 2 * i);
      bsaved.mix[0] = C.prog[0x1AA1E / 2]; bsaved.mix[1] = C.prog[0x1AA58 / 2];
      int n = 0; char d[96]; bool want_hot = false;
      while (n == 0) for (int b = 0; b < 10; b++) if (ru() < 0.3) { n++; d[0] = 0; switch (b) {
        // pool v2 (owner, 2026-10-17): no feedback, no constant roar (no jammed decay), no excessive noise
        // (mix bus at most +6 dB and never with clipping off, mild per-voice gains, light crackle, stretch up to x4)
        case 0: { int fc = 900 + (int)(ru() * 900); B.frame_cycles = fc; snprintf(d, sizeof d, "starve %d; ", fc); break; }
        case 1: { int m = rr() % 4; for (int i = 0; i < 2048; i++) { uint16_t v = bsaved.sine[i];
            if (m == 0) { int b2 = (i >> 2) & 1, b9 = (i >> 9) & 1; v = bsaved.sine[(i & ~0x204) | (b9 << 2) | (b2 << 9)]; }
            else if (m == 1) v |= (uint16_t)(1u << (8 + (int)(ru() * 7)));
            else if (m == 2) v &= 0xE000; else v ^= v >> 1;
            C.st16(0x39B8 + 2 * i, v); } snprintf(d, sizeof d, "sine %d; ", m); break; }
        case 2: { uint16_t m = (uint16_t)rr(); C.st16(0x1DFA, m); if (ru() < 0.5) C.st16(0xDDB4, (uint16_t)rr()); snprintf(d, sizeof d, "noise %04x; ", m); break; }
        case 3: want_hot = true; break;
        case 4: if (C.prog[0x1AA1E / 2] == bsaved.mix[0]) { C.bend_nosat = true; C.update_ipl(); snprintf(d, sizeof d, "nosat; "); } break;
        case 5: { uint16_t v = (uint16_t)(0x1000 + rr() % 0x7000); for (int i = 0; i < 256; i++) C.st16(0xAD62 + 2 * i, v); snprintf(d, sizeof d, "choke %04x; ", v); break; }
        case 6: break;   // feedback: left out
        case 7: { int nv = 1 + rr() % 6; std::string t = "voices:"; bool used[6] = {}; for (int k = 0; k < nv; k++) { int ch = 1 + rr() % 6, m = rr() % 6;
            if (used[ch - 1]) continue; used[ch - 1] = true;
            if (m == 5) { bskip[ch - 1] = 2 + rr() % 3; t += " " + std::to_string(ch) + "=under" + std::to_string(bskip[ch - 1]); continue; }
            static const char* mm[5] = {"crush", "hold", "wrap", "fold", "crackle"}; static const double lo[5] = {3, 2, 1.5, 1.2, 0.0003}, hi[5] = {10, 16, 3, 3, 0.003};
            BVox v; v.ch = ch; v.mode = mm[m]; v.amt = lo[m] + ru() * (hi[m] - lo[m]); bvox.push_back(v); t += " " + std::to_string(ch) + "=" + v.mode; }
          snprintf(d, sizeof d, "%s; ", t.c_str()); break; }
        case 8: bstretch = 2 + rr() % 2; snprintf(d, sizeof d, "stretch %d; ", bstretch); break;
        case 9: bbrown = 0.00002 + ru() * 0.00008; snprintf(d, sizeof d, "brownouts; "); break; }
        broll_desc += d; }
      if (want_hot) { bool wrap = false; for (auto& v : bvox) wrap |= v.mode == "wrap";
        if (bstretch == 1 && !wrap && !C.bend_nosat) { uint32_t w = 0xC80040 | ((64 - 4) & 0x3F); C.prog[0x1AA1E / 2] = w; C.prog[0x1AA58 / 2] = w; C.invalidate(0x1AA1E); C.invalidate(0x1AA58); broll_desc += "hot +6dB; "; } }
      printf("roll %u at %.2f: %s\n", broll_seed, now, broll_desc.c_str());
    }
    if (broll_seed && broll_reset > 0 && now >= broll_reset) {
      broll_reset = 0;
      B.frame_cycles = FRAME_CYCLES; bbrown = 0; bstretch = 1; bvox.clear(); for (int k = 0; k < 6; k++) bskip[k] = 0; fbk.g = 0;
      C.bend_nosat = false; C.update_ipl();
      for (int i = 0; i < 2048; i++) C.st16(0x39B8 + 2 * i, bsaved.sine[i]);
      C.st16(0x1DFA, bsaved.lcg[0]); C.st16(0xDDB4, bsaved.lcg[1]); C.st16(0x1DFC, bsaved.lcg[2]); C.st16(0xDDB6, bsaved.lcg[3]);
      for (int i = 0; i < 256; i++) C.st16(0xAD62 + 2 * i, bsaved.decay[i]);
      C.prog[0x1AA1E / 2] = bsaved.mix[0]; C.prog[0x1AA58 / 2] = bsaved.mix[1]; C.invalidate(0x1AA1E); C.invalidate(0x1AA58);
      printf("reset at %.2f\n", now);
    }
    if (bbrown > 0) { brng = brng * 1664525u + 1013904223u;
      if (C.cycles >= bbrown_until && (brng >> 8) / 16777216.0 < bbrown) { bbrown_until = C.cycles + (uint64_t)(FCY * (0.02 + 0.08 * ((brng >> 4) & 255) / 255.0)); }
      B.frame_cycles = C.cycles < bbrown_until ? 700 : 2048; }
    if (chg_t0 >= 0) { if (!chg_taken && now >= chg_t0) { memcpy(chg0.data(), C.ram, 0x10000); chg_taken = true; }
      if (chg_taken && now >= chg_t1) { FILE* f = fopen(chg_out.c_str(), "w"); for (uint32_t a = 0x1000; a < 0xE000; a += 2) if (C.ram[a] != chg0[a] || C.ram[a+1] != chg0[a+1]) fprintf(f, "%04x\n", a); fclose(f); chg_t0 = -1; } }
    C.run_cycles(stop - C.cycles);
  }
  if (bsched_an) for (auto& [pc, g] : gsites) { printf("site %06x n=%llu groups=%x targets:", pc, (unsigned long long)g.n, g.grp_mask);
    int k = 0; for (auto& [t, c] : g.targets) { if (k++ < 6) printf(" %04x(%llu)", t, (unsigned long long)c); } printf("%s\n", g.targets.size() > 6 ? " ..." : ""); }
  if (bvs_any) printf("vstretch: %llu loop-head calls, restored-changed bytes per ch: %llu %llu %llu %llu %llu %llu\n", (unsigned long long)bvs_calls,
    (unsigned long long)bvs_diff[0], (unsigned long long)bvs_diff[1], (unsigned long long)bvs_diff[2], (unsigned long long)bvs_diff[3], (unsigned long long)bvs_diff[4], (unsigned long long)bvs_diff[5]);
  if (!map_out.empty()) { FILE* f = fopen(map_out.c_str(), "w"); for (uint32_t a = 0x1000; a < 0xE000; a++) if (wcount[a]) fprintf(f, "%04x %u\n", a, wcount[a]); fclose(f); }
  double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  double emu = (C.cycles - c0) / FCY;
  printf("ran %.3f s in %.2f s wall (%.2fx real time, %.1f MIPS)\n", emu, wall, emu / wall, (C.instret - i0) / wall / 1e6);
  printf("fast-forward: %llu calls, %llu instructions skipped of %llu\n", (unsigned long long)ff_stat_calls(), (unsigned long long)ff_stat_skipped(), (unsigned long long)C.instret);
  if (C.halted) printf("HALTED at %06x: %s\n", C.pc, C.fault.c_str());

  if (stats) {
    printf("adc blocks: %llu\n", (unsigned long long)B.adc_blocks);
    printf("irqs:"); for (int i = 0; i < 128; i++) if (B.irq_count[i]) printf(" %d:%llu", i, (unsigned long long)B.irq_count[i]); printf("\n");
    if (!B.midi_out.empty()) { printf("midi out:"); for (size_t i = 0; i < B.midi_out.size() && i < 32; i++) printf(" %02x", B.midi_out[i]); printf("\n"); }
    if (!B.log.empty()) printf("%s", B.log.substr(0, 2000).c_str());
  }
  for (auto& d : dumps) { printf("dump %04x:", d.first); for (uint32_t i = 0; i < d.second; i += 2) printf(" %04x", C.ram[d.first + i] | (C.ram[d.first + i + 1] << 8)); printf("\n"); }
  if (pcprof) writefile(pcprof, pcc.data(), pcc.size() * 4);
  if (midiout_path) writefile(midiout_path, B.midi_out.data(), B.midi_out.size());
  if (flash_out) { auto f = dev.getFlash(); writefile(flash_out, f.data(), f.size()); printf("saved flash to %s\n", flash_out); }
  if (wav) {
    // B.audio starts at audio_origin; skip to -from
    double origin = audio_origin / FCY;
    size_t skip = std::min(B.audio.size(), (size_t)(std::max(0.0, wav_from - origin) * kNativeRate) * 2);
    std::vector<int32_t> out(B.audio.begin() + skip, B.audio.end());
    if (bdac) { uint32_t keep = ~((1u << (32 - bdac)) - 1); for (auto& v : out) v = (int32_t)((uint32_t)v & keep); }
    int64_t pk = 0; for (auto v : out) pk = std::max<int64_t>(pk, std::llabs((long long)v));
    printf("audio peak %.1f dBFS\n", 20 * log10(std::max<int64_t>(1, pk) / 2147483648.0));
    write_wav(wav, out, kNativeRate, 16);
    printf("wrote %s\n", wav);
  }
  return 0;
}
