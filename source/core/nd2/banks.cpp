#include "banks.h"
#include <cstring>
#include "device.h"

namespace nd2 {

uint16_t crc16(const uint8_t* data, size_t n, uint16_t crc) {
  for (size_t i = 0; i < n; i++) {
    crc ^= uint16_t(data[i] << 8);
    for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
  }
  return crc;
}

bool parseProgramFile(const uint8_t* d, size_t n, BankProgram& out, std::string& err) {
  if (n != 201 || std::memcmp(d, "CBIN", 4) != 0 || std::memcmp(d + 8, "nd2p", 4) != 0) { err = "not a program file"; return false; }
  auto u16 = [&](size_t o) { return int(d[o] | (d[o + 1] << 8)); };
  out.bank = u16(0x0C);
  out.program = u16(0x0E);
  out.format = int(d[0x14] | (d[0x15] << 8) | (d[0x16] << 16) | (uint32_t(d[0x17]) << 24));
  if (out.bank > 7 || out.program > 49) { err = "bank or program out of range"; return false; }
  std::memcpy(out.body.data(), d + 0x18, out.body.size());
  out.crcOk = u16(0xC5) == crc16(d + 0x18, 173) && u16(0xC7) == crc16(d, 0xC7);
  if (!out.crcOk) { err = "checksum mismatch"; return false; }
  return true;
}

// Continuous MSB-first bitstream, 7 bits per byte.
static void pack7(const uint8_t* data, size_t n, std::vector<uint8_t>& out) {
  uint32_t acc = 0; int bits = 0;
  for (size_t i = 0; i < n; i++) {
    acc = (acc << 8) | data[i]; bits += 8;
    while (bits >= 7) { bits -= 7; out.push_back(uint8_t((acc >> bits) & 0x7F)); }
  }
  if (bits > 0) out.push_back(uint8_t((acc << (7 - bits)) & 0x7F));
}

std::vector<uint8_t> sysexFrame(int proto, int ver, int msg, const std::vector<uint8_t>& header, const uint8_t* data, size_t n) {
  std::vector<uint8_t> m = {0xF0, 0x33, 0x7F, 0x7F, uint8_t(proto), uint8_t(ver), uint8_t(msg)};
  for (uint8_t h : header) m.push_back(h & 0x7F);
  pack7(data, n, m);
  m.push_back(uint8_t((crc16(m.data(), m.size()) >> 9) & 0x7F));
  m.push_back(0xF7);
  return m;
}

std::vector<uint8_t> programSysex(const BankProgram& p) {
  return sysexFrame(8, 3, 8, {uint8_t(0x40 | p.bank), uint8_t(p.program), uint8_t(p.format)}, p.body.data(), p.body.size());
}

FactoryBuild buildFactoryFlash(const OsImage& os, const std::vector<BankProgram>& programs,
                               const std::function<void(int, int)>& progress, const std::atomic<bool>* cancel) {
  FactoryBuild r;
  Device d;
  if (!d.setOs(os, r.error)) return r;
  d.setFlash({});                                   // erased
  d.boot();
  Board& b = d.board();
  const int total = int(programs.size());
  for (int i = 0; i < total; i++) {
    if (cancel && cancel->load()) { r.error = "cancelled"; return r; }
    b.midi_out.clear();
    b.midi_in(programSysex(programs[size_t(i)]));
    // Wait for the drum's reply (a whole SysEx message); the OS writes the program to flash meanwhile.
    bool replied = false, ack = false;
    for (int slice = 0; slice < 300 && !replied && !b.cpu.halted; slice++) {   // up to 3 s of machine time
      b.cpu.run_cycles(uint64_t(0.01 * FCY));
      const auto& o = b.midi_out;
      for (size_t s = 0; s < o.size() && !replied; s++) {
        if (o[s] != 0xF0) continue;
        size_t e = s; while (e < o.size() && o[e] != 0xF7) e++;
        if (e >= o.size()) break;                     // not complete yet
        replied = true;
        ack = e - s > 6 && o[s + 6] == 9;               // AckPerf_File
      }
    }
    if (b.cpu.halted) { r.error = "the OS stopped: " + b.cpu.fault; return r; }
    if (!replied) { r.error = "the drum did not answer program " + std::to_string(i + 1); return r; }
    if (ack) r.stored++; else r.rejected++;
    if (progress) progress(i + 1, total);
  }
  b.cpu.run_cycles(uint64_t(0.2 * FCY));          // let the last flash write finish
  r.flash = d.getFlash();
  return r;
}

}  // namespace nd2
