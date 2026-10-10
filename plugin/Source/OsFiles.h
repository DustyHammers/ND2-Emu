// Reading what the user picks: the OS update file (in any of the forms it comes in) and factory bank files.
// Nothing here writes a decrypted OS image anywhere: only the original, encrypted os.cab is ever stored.
#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "banks.h"

namespace osfiles {

// Finds a supported OS (v3.00) in what the user chose: os.cab, the Windows updater (.exe), a .zip holding either,
// the Mac updater app (a folder), a folder or opened disk image holding it, or (macOS) the .dmg itself.
// On success `cab` is the original os.cab. May take a few seconds (a .dmg is opened and closed): call it off the
// message thread.
bool locateOs(const juce::File& chosen, std::vector<uint8_t>& cab, juce::String& error);

// Program files from the user's choice: .nd2p files, .nd2_bank files (a zip of .nd2p files), .zip files holding
// either (any depth), or folders holding any of these. Programs with a bad checksum are skipped. Sorted by bank and
// program; a later file replaces an earlier one at the same place.
bool readPrograms(const juce::Array<juce::File>& chosen, std::vector<nd2::BankProgram>& out, juce::String& error);

}  // namespace osfiles
