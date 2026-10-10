#include "rom.h"
#include <cstdio>
#include <cstring>

namespace nd2 {

namespace {
constexpr size_t kKeyLen = 0x900;
constexpr size_t kErasedStart = 0x61000, kErasedEnd = 0x6D800;   // erased flash inside the image
constexpr uint64_t kKnownOs300 = 0x54240FC1E4DF4A40ULL;   // FNV-1a 64 of the decrypted v3.00 image

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

uint64_t fnv1a(const uint8_t* p, size_t n) {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ULL; }
  return h;
}
}  // namespace

bool extractOsCab(const std::vector<uint8_t>& f, std::vector<uint8_t>& cab) {
  for (size_t off = 0; off + 0x50 <= f.size(); off += 4) {
    const uint8_t* h = f.data() + off;
    if (be32(h) != 0 || be32(h + 8) != 0xDE || be32(h + 12) != 0x8000) continue;
    const uint32_t len = be32(h + 16);
    if (len != 0x78000 || off + 0x50 + len > f.size()) continue;
    std::vector<uint8_t> c(h, h + 0x50 + len);
    OsImage os; std::string err;
    if (!loadOsUpdate(c, os, err) || !isSupportedOs(os)) continue;
    cab.swap(c);
    return true;
  }
  return false;
}

bool loadOsUpdate(const std::vector<uint8_t>& f, OsImage& out, std::string& err) {
  // header: u32be 0, version, product 0xDE, block 0x8000, payload length; 0xFF pad to 0x50
  for (size_t off = 0; off + 0x50 <= f.size(); off += 4) {
    const uint8_t* h = f.data() + off;
    if (be32(h) != 0 || be32(h + 8) != 0xDE || be32(h + 12) != 0x8000) continue;
    uint32_t ver = be32(h + 4), len = be32(h + 16);
    if (len != 0x78000 || off + 0x50 + len > f.size()) continue;
    const uint8_t* ct = h + 0x50;
    // key: majority vote of ct ^ 0xFF over the erased region
    uint8_t key[kKeyLen];
    for (size_t i = 0; i < kKeyLen; i++) {
      unsigned cnt[256] = {};
      size_t first = kErasedStart + ((i + kKeyLen - kErasedStart % kKeyLen) % kKeyLen);
      for (size_t j = first; j < kErasedEnd; j += kKeyLen) cnt[ct[j] ^ 0xFF]++;
      unsigned best = 0; for (int v = 1; v < 256; v++) if (cnt[v] > cnt[best]) best = v;
      key[i] = (uint8_t)best;
    }
    out.image.resize(len);
    for (size_t i = 0; i < len; i++) out.image[i] = ct[i] ^ key[i % kKeyLen];
    if (!(out.image[0] == 0x2C && out.image[1] == 0x01 && out.image[4] == 0xDE)) { err = "decryption check failed"; return false; }
    out.version = ver;
    out.hash = fnv1a(out.image.data(), out.image.size());
    return true;
  }
  err = "no supported OS update found in file (use os.cab or the uncompressed updater)";
  return false;
}

bool loadOsUpdateFile(const std::string& path, OsImage& out, std::string& err) {
  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp) { err = "cannot open " + path; return false; }
  std::vector<uint8_t> b;
  fseek(fp, 0, SEEK_END); long n = ftell(fp); fseek(fp, 0, SEEK_SET);
  if (n > 0) { b.resize((size_t)n); if (fread(b.data(), 1, b.size(), fp) != b.size()) b.clear(); }
  fclose(fp);
  return loadOsUpdate(b, out, err);
}

bool isSupportedOs(const OsImage& os) { return os.version == 300 && os.hash == kKnownOs300; }

}  // namespace nd2
