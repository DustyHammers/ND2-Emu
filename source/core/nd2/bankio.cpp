#include "bankio.h"
#include <cstring>
#include "device.h"
#include "engine.h"

namespace nd2 {

namespace {
constexpr uint32_t kMainLoop = 0x31B54;          // OS v3.00 event loop entry
constexpr double kSliceS = 0.01, kReplyTimeoutS = 3.0;

// MSB-first bitstream, 7 bits per byte -> n bytes
bool unpack7(const uint8_t* in, size_t inN, uint8_t* out, size_t n) {
  if (inN * 7 < n * 8) return false;
  uint32_t acc = 0; int bits = 0; size_t o = 0;
  for (size_t i = 0; i < inN && o < n; i++) {
    acc = (acc << 7) | (in[i] & 0x7F); bits += 7;
    if (bits >= 8) { bits -= 8; out[o++] = uint8_t(acc >> bits); }
  }
  return o == n;
}
}  // namespace

BankMachine::BankMachine() = default;
BankMachine::~BankMachine() = default;

bool BankMachine::restore(const std::vector<uint8_t>& osImage, uint64_t osHash, const std::vector<uint8_t>& state, std::string& err) {
  b_ = Engine::boardFromState(osImage, osHash, state, err);
  osHash_ = osHash;
  if (!b_) return false;
  b_->midi_out.clear(); b_->midi_out_cycles.clear(); b_->audio.clear(); b_->stems.clear();
  return true;
}

bool BankMachine::boot(const OsImage& os, const std::vector<uint8_t>& flash, std::string& err) {
  if (!isSupportedOs(os)) { err = "unsupported OS (v3.00 required)"; return false; }
  osHash_ = os.hash;
  b_ = std::make_unique<Board>();
  b_->load_app(os.image);
  auto& p = b_->cpu.prog;
  for (uint32_t pc = kFlashStart, i = 0; pc < kFlashEnd; pc += 2, i += 4)
    p[pc >> 1] = (i + 3 < flash.size()) ? (flash[i] | (flash[i + 1] << 8) | (flash[i + 2] << 16)) : 0xFFFFFF;
  b_->boot();
  b_->set_adc(panel::kAdcMasterLevel, 0.999);
  b_->cpu.run_until_pc(kMainLoop, uint64_t(3.0 * FCY));
  b_->cpu.run_cycles(uint64_t(0.1 * FCY));        // the OS loads the current program after entering its loop
  if (b_->cpu.halted || b_->cpu.pc == 0) { err = "the OS stopped during boot: " + b_->cpu.fault; return false; }
  b_->midi_out.clear(); b_->midi_out_cycles.clear(); b_->audio.clear();
  return true;
}

bool BankMachine::exchange(const std::vector<uint8_t>& msg, std::vector<uint8_t>& reply, std::string& err) {
  Board& b = *b_;
  b.midi_out.clear(); b.midi_out_cycles.clear();
  b.midi_in(msg);
  reply.clear();
  const int slices = int(kReplyTimeoutS / kSliceS);
  for (int s = 0; s < slices && !b.cpu.halted; s++) {
    b.cpu.run_cycles(uint64_t(kSliceS * FCY));
    b.audio.clear(); b.stems.clear();
    const auto& o = b.midi_out;
    for (size_t i = 0; i < o.size(); i++) {
      if (o[i] != 0xF0) continue;
      size_t e = i; while (e < o.size() && o[e] != 0xF7) e++;
      if (e >= o.size()) break;                     // not complete yet
      reply.assign(o.begin() + long(i), o.begin() + long(e) + 1);
      b.midi_out.clear(); b.midi_out_cycles.clear();
      return true;
    }
  }
  err = b.cpu.halted ? "the OS stopped: " + b.cpu.fault : "the drum did not answer";
  return false;
}

std::vector<uint8_t> programQuery(int bank, int program) {
  return sysexFrame(8, 3, 7, {uint8_t(0x40 | bank), uint8_t(program)}, nullptr, 0);
}

bool parseProgramMessage(const uint8_t* m, size_t n, BankProgram& out, std::string& err) {
  // F0 33 7F dev 08 ver 08 bank prog format <200 data bytes> chk F7 (the drum sends bank without the 0x40 flag)
  if (n != 212 || m[0] != 0xF0 || m[1] != 0x33 || m[4] != 8 || m[6] != 8 || m[n - 1] != 0xF7) { err = "not a program message"; return false; }
  if (((crc16(m, n - 2) >> 9) & 0x7F) != m[n - 2]) { err = "message checksum mismatch"; return false; }
  out.bank = m[7] & 0x3F; out.program = m[8]; out.format = m[9];
  if (out.bank >= kBanks || out.program >= kBankPrograms) { err = "bank or program out of range"; return false; }
  if (!unpack7(m + 10, 200, out.body.data(), out.body.size())) { err = "short program data"; return false; }
  out.crcOk = programCrc(out) == crc16(out.body.data(), 173);
  if (!out.crcOk) { err = "program checksum mismatch"; return false; }
  return true;
}

BankMachine::Read BankMachine::readProgram(int bank, int program, BankProgram& out, std::string& err) {
  std::vector<uint8_t> r;
  if (!exchange(programQuery(bank, program), r, err)) return Read::Failed;
  if (r.size() == 12 && r[6] == 9 && r[9] == 1) return Read::Empty;           // AckPerf_File: not stored
  if (!parseProgramMessage(r.data(), r.size(), out, err)) return Read::Failed;
  if (out.bank != bank || out.program != program) { err = "the drum answered with another program"; return Read::Failed; }
  return Read::Stored;
}

bool BankMachine::writeProgram(const BankProgram& p, int bank, std::string& err) {
  BankProgram q = p;
  q.bank = bank;
  std::vector<uint8_t> r;
  if (!exchange(programSysex(q), r, err)) return false;
  if (r.size() < 12 || r[6] != 9) { err = "unexpected answer from the drum"; return false; }
  if (r[9] != 0) { err = "the drum refused program " + std::to_string(p.program + 1) + " (code " + std::to_string(r[9]) + ")"; return false; }
  return true;
}

void BankMachine::selectProgram(int index) {
  if (index < 0 || index >= kBanks * kBankPrograms) return;
  std::vector<uint8_t> m;
  for (int ch = 0; ch < 16; ch++) {
    const uint8_t s = uint8_t(0xB0 | ch);
    m.insert(m.end(), {s, 0, 0, s, 32, uint8_t(index / kBankPrograms), uint8_t(0xC0 | ch), uint8_t(index % kBankPrograms)});
  }
  b_->midi_in(m);
  b_->cpu.run_cycles(uint64_t(0.3 * FCY));
  b_->audio.clear(); b_->stems.clear(); b_->midi_out.clear(); b_->midi_out_cycles.clear();
}

int BankMachine::currentProgram() const {
  const int cur = b_->cpu.ld16(panel::kRamCurrentProgram);
  return cur < kBanks * kBankPrograms ? cur : -1;
}

std::vector<uint8_t> BankMachine::state() const { return Engine::stateOf(*b_, osHash_); }

std::vector<uint8_t> BankMachine::flash() const {
  std::vector<uint8_t> out;
  out.reserve((kFlashEnd - kFlashStart) * 2);
  for (uint32_t pc = kFlashStart; pc < kFlashEnd; pc += 2) {
    const uint32_t w = b_->cpu.prog[pc >> 1];
    out.push_back(uint8_t(w)); out.push_back(uint8_t(w >> 8)); out.push_back(uint8_t(w >> 16)); out.push_back(0);
  }
  return out;
}

std::vector<uint8_t> programFile(const BankProgram& p, uint32_t id) {
  std::vector<uint8_t> f(201, 0);
  std::memcpy(f.data(), "CBIN", 4);
  std::memcpy(f.data() + 8, "nd2p", 4);
  f[0x0C] = uint8_t(p.bank); f[0x0E] = uint8_t(p.program);
  for (int i = 0; i < 4; i++) { f[size_t(0x10 + i)] = uint8_t(id >> (8 * i)); f[size_t(0x14 + i)] = uint8_t(uint32_t(p.format) >> (8 * i)); }
  std::memcpy(f.data() + 0x18, p.body.data(), p.body.size());
  const uint16_t c = crc16(f.data(), 0xC7);
  f[0xC7] = uint8_t(c); f[0xC8] = uint8_t(c >> 8);
  return f;
}

}  // namespace nd2
