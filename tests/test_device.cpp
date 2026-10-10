// Device-level smoke test. Needs the user's OS update (ND2_OS=/path/to/os.cab); skipped when absent.
#include "device.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace nd2;

int main() {
  const char* path = getenv("ND2_OS");
  if (!path) { printf("ND2_OS not set: device test skipped\n"); return 0; }
  OsImage os; std::string err;
  if (!loadOsUpdateFile(path, os, err)) { printf("FAIL: %s\n", err.c_str()); return 1; }
  Device d;
  if (!d.setOs(os, err)) { printf("FAIL: %s\n", err.c_str()); return 1; }
  d.boot();                                     // erased flash: OS defaults
  if (d.board().cpu.halted) { printf("FAIL: halted during boot: %s\n", d.board().cpu.fault.c_str()); return 1; }
  d.panel();                                    // reset the panel averages
  d.runSeconds(0.05);
  auto pan = d.panel();
  printf("display: [%s]\n", pan.display.c_str());
  if (pan.display.empty() || pan.display[0] != 'P') { printf("FAIL: display should show the program (P...)\n"); return 1; }
  d.turnEncoder(1);
  d.runSeconds(0.1); d.panel(); d.runSeconds(0.05);
  auto pan2 = d.panel();
  printf("after encoder +1: [%s]\n", pan2.display.c_str());
  if (pan2.display == pan.display) { printf("FAIL: encoder had no effect\n"); return 1; }
  const uint8_t note[] = {0x99, 60, 100};       // ch 10, pad 1
  d.midiIn(note, 3);
  const size_t n = (size_t)kNativeRate / 2;
  std::vector<float> l(n), r(n);
  d.render(l.data(), r.data(), n);
  double e = 0; for (size_t i = 0; i < n; i++) e += l[i] * l[i] + r[i] * r[i];
  double rms = std::sqrt(e / (2 * n));
  printf("pad 1 rms over 0.5 s: %.1f dBFS\n", 20 * std::log10(rms + 1e-12));
  if (rms < 1e-4) { printf("FAIL: silent\n"); return 1; }
  printf("device test passed\n");
  return 0;
}
