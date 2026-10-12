// Bank files: the original manager's .nd2_bank format (a stored zip of "Program NNN.nd2p", one 201-byte program file
// per program) and the bank library folder:
//
//   ~/Documents/ND2/Library/            the user's bank files (Add files... copies them here; saved banks go here)
//   ~/Documents/ND2/Library/Factory/    the factory banks, from the user's own factory bank files
//
// Files in the library are never overwritten: a name that is taken gets " 2", " 3", ...
#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <vector>
#include "banks.h"

namespace bankfiles {

constexpr int kEmpty = -1;                            // a program CRC for an empty slot

struct BankFile {
    juce::File file;
    juce::String name;                                // the file name without its extension
    bool factory = false;                             // in Library/Factory
    std::array<nd2::BankProgram, nd2::kBankPrograms> programs{};
    std::array<int, nd2::kBankPrograms> crcs{};       // each program's CRC (identifies its content), kEmpty if absent
    int count = 0;                                    // programs in the file
    int sourceBank = -1;                              // the bank its programs say they come from (0-7), -1 if mixed
};

// What was last loaded into a bank of the drum's memory: shown as the bank's name; "stored on the panel since
// loading" compares against its CRCs. Saved with each project, and with the factory memory for new instances.
struct BankRecord {
    bool set = false;
    juce::String name;
    std::array<int, nd2::kBankPrograms> crcs{};
};
using BankRecords = std::array<BankRecord, nd2::kBanks>;
void writeRecords(juce::OutputStream& out, const BankRecords& r);
bool readRecords(juce::InputStream& in, BankRecords& r);

juce::File libraryFolder();
juce::File factoryFolder();

// A .nd2_bank file: programs by their program number (their bank field is ignored, except for sourceBank).
bool readBankFile(const juce::File& f, BankFile& out, juce::String& error);
// programs: up to 50, each at p.program; written as bank `bank`.
bool writeBankFile(const juce::File& f, const std::vector<nd2::BankProgram>& programs, int bank, juce::String& error);
// The bank files in the library: Factory/ first, then the rest (sub-folders included), in natural name order.
std::vector<BankFile> scanLibrary();
// folder/<base><ext>, or folder/<base> 2<ext>, ... : a name that does not exist yet.
juce::File freeFile(const juce::File& folder, const juce::String& base, const juce::String& ext);
// Copies a file into the library (or Library/Factory) without overwriting anything; returns the copy.
juce::File addToLibrary(const juce::File& f, bool factory, juce::String& error);
bool inLibrary(const juce::File& f);

}  // namespace bankfiles
