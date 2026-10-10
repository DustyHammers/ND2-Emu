#include "device.h"

namespace nd2 {

namespace { constexpr uint32_t kMainLoop = 0x31B54; }   // OS v3.00 event loop entry

Device::Device() : b(new Board) { setMasterLevel(1.0); }

bool Device::setOs(const OsImage& os, std::string& err) {
  if (!isSupportedOs(os)) { err = "unsupported OS version " + std::to_string(os.version) + " (v3.00 required)"; return false; }
  app = os.image;
  return true;
}

void Device::setFlash(const std::vector<uint8_t>& img) {
  auto& p = b->cpu.prog;
  for (uint32_t pc = kFlashStart, i = 0; pc < kFlashEnd; pc += 2, i += 4)
    p[pc >> 1] = (i + 3 < img.size()) ? (img[i] | (img[i + 1] << 8) | (img[i + 2] << 16)) : 0xFFFFFF;
}

std::vector<uint8_t> Device::getFlash() const {
  std::vector<uint8_t> out;
  out.reserve((kFlashEnd - kFlashStart) * 2);
  for (uint32_t pc = kFlashStart; pc < kFlashEnd; pc += 2) {
    uint32_t w = b->cpu.prog[pc >> 1];
    out.push_back((uint8_t)w); out.push_back((uint8_t)(w >> 8)); out.push_back((uint8_t)(w >> 16)); out.push_back(0);
  }
  return out;
}

void Device::boot() {
  b->load_app(app);
  b->boot();
  b->audio.clear(); audio_read = 0;
  auto& c = b->cpu;
  c.run_until_pc(kMainLoop, (uint64_t)(3.0 * FCY));
  c.run_cycles((uint64_t)(0.1 * FCY));     // the OS loads the current program after entering its loop (~10 ms)
  b->audio.clear(); audio_read = 0;
}

void Device::runSeconds(double s) {
  b->cpu.run_cycles((uint64_t)(s * FCY));
  b->audio.clear(); audio_read = 0;
}

void Device::render(float* left, float* right, size_t frames) {
  auto& a = b->audio;
  while ((a.size() - audio_read) / 2 < frames && !b->cpu.halted) b->cpu.run_cycles(FRAME_CYCLES);
  const float k = 1.0f / 2147483648.0f;
  for (size_t i = 0; i < frames; i++) {
    size_t j = audio_read + 2 * i;
    left[i]  = j < a.size() ? a[j] * k : 0.0f;
    right[i] = j + 1 < a.size() ? a[j + 1] * k : 0.0f;
  }
  audio_read += 2 * frames;
  if (audio_read > 1 << 16) { a.erase(a.begin(), a.begin() + std::min(audio_read, a.size())); audio_read = 0; }
}

}  // namespace nd2
