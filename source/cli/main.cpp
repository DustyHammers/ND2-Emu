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
    c.st16(a, (uint16_t)((uint32_t)x >> 16)); c.st16(a + 2, (uint16_t)x);
  }
}
static void skip_hook(dspic::CPU& c, void* ctx) {
  const int ch = (int)(intptr_t)ctx;
  if (bskip[ch] > 1 && (bskip_n[ch]++ % bskip[ch]) != 0) c.st16(0, (uint16_t)(0x3F00 + 4 * ch));   // skip the engine this sample
}
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
  if (bint) { C.bend_int = true; C.update_ipl(); printf("bend: fractional/integer swapped\n"); }
  if (!bvox.empty()) { C.add_hook(0x1A9EE, vox_hook, nullptr); printf("bend: %zu voice bends\n", bvox.size()); }
  for (int k = 0; k < 6; k++) { uint32_t t = 0x1A68E + 0x90 * k, a = 0x3F00 + 4 * k;   // trampolines back to the dispatch
    C.prog[a / 2] = 0x040000 | (t & 0xFFFE); C.prog[a / 2 + 1] = (t >> 16) & 0x7F; C.invalidate(a); C.invalidate(a + 2); }
  for (int k = 0; k < 6; k++) if (bskip[k] > 1) { C.add_hook(0x1A68C + 0x90 * k, skip_hook, (void*)(intptr_t)k); printf("bend: channel %d engine 1 in %d\n", k + 1, bskip[k]); }
  static FeedBack fbk; if (bfb != 0) { fbk.b = &B; fbk.g = bfb; fbk.mask = bfbmask; B.adc_live = &fbk; printf("bend: feedback %.2f into pads %x\n", bfb, bfbmask); }
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

  // debug hooks
  std::map<uint32_t, int> bphits;
  std::vector<uint32_t> pcc;
  if (pcprof) pcc.assign(0x40000, 0);
  if (!bps.empty() || tr_hi || pcprof) C.trace = [&](uint32_t pc, const dspic::Insn&) {
    if (pcprof) pcc[pc >> 1]++;
    if (pc >= tr_lo && pc < tr_hi && C.cycles / FCY >= tr_t && tr_n < 400) { tr_n++;
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
    C.run_cycles(stop - C.cycles);
  }
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
