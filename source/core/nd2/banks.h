// Factory banks: the user's own program files (.nd2p, 201 bytes each; a .nd2_bank is a zip of 50 of them) turned
// into the OS's ProgDump SysEx and stored by the OS itself in a machine's flash. The flash image made this way is
// what new instances start from (made once per OS and set of files, cached outside the repository).
//
// .nd2p = "CBIN" container: 0x00 'CBIN', 0x08 'nd2p', 0x0C u16 bank (0-7), 0x0E u16 program (0-49),
// 0x14 u32 format version, 0x18 173-byte program + u16 CRC (the 175 bytes that go over MIDI), 0xC7 u16 file CRC.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "rom.h"

namespace nd2 {

constexpr int kBanks = 8, kBankPrograms = 50;   // the drum's memory: 8 banks of 50 programs

struct BankProgram {
  int bank = 0, program = 0, format = 0;
  std::array<uint8_t, 175> body{};
  bool crcOk = false;
};

bool parseProgramFile(const uint8_t* data, size_t size, BankProgram& out, std::string& err);
uint16_t crc16(const uint8_t* data, size_t n, uint16_t crc = 0xFFFF);   // CRC-16/CCITT
// The ProgDump "DtaPerf_File" message that stores one program at bank/program on the drum.
std::vector<uint8_t> programSysex(const BankProgram& p);
// The SysEx frame: F0 33 7F dev proto ver msg header... 7-bit data... checksum F7.
std::vector<uint8_t> sysexFrame(int proto, int ver, int msg, const std::vector<uint8_t>& header, const uint8_t* data, size_t n);

struct FactoryBuild {
  std::vector<uint8_t> flash;   // the flash image (4 bytes per word, as Device::getFlash)
  int stored = 0, rejected = 0;
  std::string error;            // empty: success
};
// Boots a machine with erased flash, sends every program through its MIDI input one at a time (waiting for the
// drum's acknowledgement) and returns its flash. progress(done, total) is called after each program; cancel
// stops early (returns with an error). Takes about a second of machine time per few programs.
FactoryBuild buildFactoryFlash(const OsImage& os, const std::vector<BankProgram>& programs,
                               const std::function<void(int, int)>& progress = {},
                               const std::atomic<bool>* cancel = nullptr);

}  // namespace nd2
