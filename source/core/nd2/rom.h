// OS update loader: finds os.cab inside any container that stores it uncompressed
// (os.cab itself, the Mac updater's Contents/Resources/os.cab, the Windows "Update.exe"),
// derives the XOR key and returns the decrypted dsPIC program image (load address 0x4000).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nd2 {

struct OsImage {
  std::vector<uint8_t> image;   // 4 bytes per instruction (lo, mid, hi, 0) from program address 0x4000
  uint32_t version = 0;         // 300 = v3.00
  uint64_t hash = 0;            // FNV-1a 64 of the decrypted image
};

// Returns false and fills err when no supported OS is found.
bool loadOsUpdate(const std::vector<uint8_t>& file, OsImage& out, std::string& err);
bool loadOsUpdateFile(const std::string& path, OsImage& out, std::string& err);
// The original os.cab inside a container (os.cab itself, the updater app's file, the Windows updater), unchanged
// (still encrypted): what the plugin keeps a copy of. False if there is none.
bool extractOsCab(const std::vector<uint8_t>& file, std::vector<uint8_t>& cab);

// The emulator hard-codes v3.00 addresses (vectors, entry point), so only that exact image is accepted.
bool isSupportedOs(const OsImage& os);

}  // namespace nd2
