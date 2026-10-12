// Bank transfers: a machine run offline (as fast as the computer can), restored from an engine state or booted
// with a flash image, and driven only through its MIDI input the way the original manager drives the hardware:
//   read  = QryPerf_File (ProgDump msg 7) -> the drum answers DtaPerf_File (msg 8) with the stored program, or
//           AckPerf_File (msg 9) code 01 when the slot is empty;
//   write = DtaPerf_File (msg 8) -> the drum stores it and answers AckPerf_File (msg 9) code 00.
// The OS decides what is accepted and stores it in its own flash; nothing here touches the flash directly.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "banks.h"
#include "board.h"

namespace nd2 {

class BankMachine {
public:
  BankMachine();
  ~BankMachine();
  // osImage/osHash: the decrypted OS (OsImage::image, OsImage::hash); state: Engine::getState()
  bool restore(const std::vector<uint8_t>& osImage, uint64_t osHash, const std::vector<uint8_t>& state, std::string& err);
  bool boot(const OsImage& os, const std::vector<uint8_t>& flash, std::string& err);   // flash empty = erased

  enum class Read { Stored, Empty, Failed };
  Read readProgram(int bank, int program, BankProgram& out, std::string& err);
  // Stores p's body at bank (0-7), program p.program.
  bool writeProgram(const BankProgram& p, int bank, std::string& err);
  // Program changes to `index` (bank * 50 + program), so the OS loads it again from its memory.
  void selectProgram(int index);
  int currentProgram() const;                  // bank * 50 + program, -1 if unknown

  std::vector<uint8_t> state() const;          // an engine state (Engine::setState)
  std::vector<uint8_t> flash() const;          // the preset memory (Device::getFlash layout)
  Board& board() { return *b_; }

private:
  std::unique_ptr<Board> b_;
  uint64_t osHash_ = 0;
  // Sends one message and waits (up to 3 s of machine time) for the drum's next complete SysEx message.
  bool exchange(const std::vector<uint8_t>& msg, std::vector<uint8_t>& reply, std::string& err);
};

// ProgDump messages
std::vector<uint8_t> programQuery(int bank, int program);          // QryPerf_File
// A DtaPerf_File message from the drum (or a file) -> program (bank/program from its header); checks its checksum.
bool parseProgramMessage(const uint8_t* msg, size_t n, BankProgram& out, std::string& err);
// A 201-byte .nd2p program file (the original manager's format). id: the file's 32-bit id field.
std::vector<uint8_t> programFile(const BankProgram& p, uint32_t id);
// The CRC the OS keeps with a program (its last two body bytes): identifies the program's content.
inline uint16_t programCrc(const BankProgram& p) { return uint16_t(p.body[173] | (p.body[174] << 8)); }

}  // namespace nd2
