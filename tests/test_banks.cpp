// Factory banks: program file format and SysEx (no data needed); building a factory flash needs the OS and the
// user's program files: ND2_OS=os file, ND2_BANKS=folder of .nd2p files (any sub-folders), ND2_FLASH=expected image.
#include "banks.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <chrono>
#include <cstring>
using namespace nd2;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static std::vector<uint8_t> synthetic() {
  std::vector<uint8_t> f(201, 0);
  std::memcpy(f.data(), "CBIN", 4); std::memcpy(f.data() + 8, "nd2p", 4);
  f[0x0C] = 2; f[0x0E] = 17; f[0x10] = 0x34; f[0x11] = 0x12; f[0x14] = 3;
  for (int i = 0; i < 173; i++) f[size_t(0x18 + i)] = uint8_t((i * 37 + 11) & 0xFF);
  uint16_t c = crc16(f.data() + 0x18, 173); f[0xC5] = uint8_t(c); f[0xC6] = uint8_t(c >> 8);
  c = crc16(f.data(), 0xC7); f[0xC7] = uint8_t(c); f[0xC8] = uint8_t(c >> 8);
  return f;
}

int main() {
  // ---- format: matches the reference tool (tools/nd2_sysex.py) byte for byte
  {
    const std::vector<uint8_t> f = synthetic();
    BankProgram p; std::string err;
    CHECK(parseProgramFile(f.data(), f.size(), p, err), "parse: %s", err.c_str());
    CHECK(p.bank == 2 && p.program == 17 && p.format == 3, "fields %d %d %d", p.bank, p.program, p.format);
    const std::vector<uint8_t> want = {0xF0,0x33,0x7F,0x7F,0x08,0x03,0x08,0x42,0x11,0x03,0x05,0x4C,0x0A,0x57,0x54,0x7F,0x09,0x69,0x07,0x0C,0x6B,0x07,0x6D,0x0B,0x0F,0x6C,0x08,0x4D,0x4B,0x38,0x05,0x17,0x15,0x6F,0x0A,0x0E,0x2B,0x68,0x1D,0x23,0x1B,0x72,0x0B,0x4F,0x0C,0x18,0x35,0x2F,0x21,0x75,0x0D,0x0F,0x6C,0x48,0x4D,0x3B,0x27,0x78,0x0E,0x50,0x4C,0x78,0x65,0x47,0x2D,0x7B,0x10,0x11,0x2D,0x28,0x7D,0x53,0x33,0x7E,0x11,0x52,0x0D,0x59,0x15,0x5F,0x38,0x01,0x13,0x12,0x6E,0x09,0x2D,0x6B,0x3E,0x04,0x14,0x53,0x4E,0x39,0x45,0x77,0x44,0x07,0x16,0x14,0x2E,0x69,0x5E,0x03,0x4A,0x0A,0x17,0x55,0x0F,0x19,0x76,0x0F,0x50,0x0D,0x19,0x15,0x6F,0x4A,0x0E,0x1B,0x56,0x10,0x1A,0x56,0x4F,0x7A,0x26,0x27,0x5C,0x13,0x1C,0x17,0x30,0x2A,0x3E,0x33,0x62,0x16,0x1D,0x58,0x10,0x5A,0x56,0x3F,0x68,0x19,0x1F,0x18,0x71,0x0A,0x6E,0x4B,0x6E,0x1C,0x20,0x59,0x51,0x3B,0x06,0x57,0x74,0x1F,0x22,0x1A,0x31,0x6B,0x1E,0x63,0x7A,0x22,0x23,0x5B,0x12,0x1B,0x36,0x6C,0x00,0x25,0x25,0x1B,0x72,0x4B,0x4E,0x78,0x06,0x28,0x26,0x5C,0x52,0x7B,0x67,0x04,0x0C,0x2B,0x28,0x1D,0x33,0x2B,0x7F,0x10,0x12,0x2E,0x29,0x5E,0x13,0x5C,0x17,0x1C,0x09,0x44,0x49,0xF7};
    const std::vector<uint8_t> got = programSysex(p);
    CHECK(got == want, "SysEx differs from the reference (%zu vs %zu bytes)", got.size(), want.size());
    std::vector<uint8_t> bad = f; bad[0x40] ^= 1;
    CHECK(!parseProgramFile(bad.data(), bad.size(), p, err), "a damaged file was accepted");
  }
  // ---- building a factory flash with the real OS (local only)
  const char* osPath = getenv("ND2_OS"); const char* banks = getenv("ND2_BANKS");
  if (!osPath || !banks) { printf(fails ? "%d failure(s)\n" : "bank format tests passed (factory build skipped: ND2_OS / ND2_BANKS not set)\n", fails); return fails ? 1 : 0; }
  OsImage os; std::string err;
  if (!loadOsUpdateFile(osPath, os, err)) { printf("FAIL: %s\n", err.c_str()); return 1; }
  std::vector<BankProgram> progs;
  for (const auto& e : std::filesystem::recursive_directory_iterator(banks)) {
    if (e.path().extension() != ".nd2p") continue;
    std::ifstream in(e.path(), std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
    BankProgram p;
    if (parseProgramFile(d.data(), d.size(), p, err)) progs.push_back(p); else printf("skipped %s: %s\n", e.path().c_str(), err.c_str());
  }
  std::sort(progs.begin(), progs.end(), [](const BankProgram& a, const BankProgram& b) { return a.bank * 50 + a.program < b.bank * 50 + b.program; });
  const auto t0 = std::chrono::steady_clock::now();
  FactoryBuild r = buildFactoryFlash(os, progs);
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  printf("factory build: %zu programs, %d stored, %d rejected, %.1f s%s%s\n", progs.size(), r.stored, r.rejected, secs,
         r.error.empty() ? "" : ", error: ", r.error.c_str());
  CHECK(r.error.empty() && r.stored == int(progs.size()), "build");
  if (const char* out = getenv("ND2_FLASH_OUT")) { std::ofstream o(out, std::ios::binary); o.write((const char*)r.flash.data(), long(r.flash.size())); }
  if (const char* want = getenv("ND2_FLASH")) {
    std::ifstream in(want, std::ios::binary);
    std::vector<uint8_t> w((std::istreambuf_iterator<char>(in)), {});
    size_t diff = 0; for (size_t i = 0; i < std::min(w.size(), r.flash.size()); i++) diff += w[i] != r.flash[i];
    printf("against %s: %zu bytes differ (%zu vs %zu bytes)\n", want, diff, r.flash.size(), w.size());
  }
  printf(fails ? "%d failure(s)\n" : "all bank tests passed\n", fails);
  return fails ? 1 : 0;
}
