// Core self-tests that need no firmware: instruction decode + DSP/ALU semantics.
#include "cpu.h"
#include <cstdio>
#include <initializer_list>
#include <string>

using namespace dspic;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct Rig {
  CPU c;
  Rig(std::initializer_list<uint32_t> code) {
    c.prog.assign(0x400, 0x000000);                       // NOPs
    uint32_t pc = 0x100; for (auto w : code) { c.prog[pc >> 1] = w; pc += 2; }
    c.reset(0x100);
    c.setcore16(A_CORCON, CC_SATA | CC_SATB | CC_SATDW | CC_ACCSAT);   // as the OS sets it
  }
  void run(int n) { for (int i = 0; i < n; i++) c.step(); }
};

int main() {
  {  // ADD Wb,#lit5,Wd overflow flags
    Rig r({0x27FFF1 /*mov #0x7fff,W1*/, 0x408161 /*add W1,#1,W2*/});
    CHECK(r.c.disasm_at(0x102).find("add") == 0, "disasm: %s", r.c.disasm_at(0x102).c_str());
    r.run(2);
    CHECK(r.c.W(2) == 0x8000, "W2=%04x", r.c.W(2));
    CHECK((r.c.sr() & (SR_OV | SR_N)) == (SR_OV | SR_N), "SR=%04x", r.c.sr());
  }
  {  // MPY (fractional) + SAC.R
    Rig r({0x240004 /*mov #0x4000,W4*/, 0x240005 /*mov #0x4000,W5*/, 0xC00113 /*mpy W4*W5,A*/, 0xCD0000 /*sac.r A,W0*/});
    CHECK(r.c.disasm_at(0x104).find("mpy") == 0, "disasm: %s", r.c.disasm_at(0x104).c_str());
    r.run(4);
    CHECK(r.c.acc(0) == 0x20000000LL, "ACCA=%llx", (long long)r.c.acc(0));
    CHECK(r.c.W(0) == 0x2000, "W0=%04x", r.c.W(0));
  }
  {  // MPY -1 * -1 saturates to 0x7FFFFFFF-ish? (fractional: 1.0 = 0x80000000 fits in 40 bits, SAC saturates to 0x7FFF)
    Rig r({0x280004 /*mov #0x8000,W4*/, 0x280005 /*mov #0x8000,W5*/, 0xC00113, 0xCD0000});
    r.run(4);
    CHECK(r.c.acc(0) == 0x80000000LL, "ACCA=%llx", (long long)r.c.acc(0));
    CHECK(r.c.W(0) == 0x7FFF, "W0=%04x (SATDW)", r.c.W(0));
  }
  {  // REPEAT #17 + DIVF: 0.25 / 0.5 = 0.5
    Rig r({0x220002 /*mov #0x2000,W2*/, 0x240003 /*mov #0x4000,W3*/, 0x090011 /*repeat #17*/, 0xD91003 /*divf W2,W3*/});
    CHECK(r.c.disasm_at(0x106).find("divf") == 0, "disasm: %s", r.c.disasm_at(0x106).c_str());
    r.run(5);
    CHECK(r.c.W(0) == 0x4000, "W0=%04x", r.c.W(0));
  }
  {  // convergent rounding: 0x0000_8000 rounds to even (0), 0x0001_8000 rounds up to 2
    Rig r({0xCD0000});
    r.c.setacc_raw(0, 0x8000); r.run(1);
    CHECK(r.c.W(0) == 0, "round-to-even low: W0=%04x", r.c.W(0));
    Rig r2({0xCD0000});
    r2.c.setacc_raw(0, 0x18000); r2.run(1);
    CHECK(r2.c.W(0) == 2, "round-to-even high: W0=%04x", r2.c.W(0));
  }
  printf(fails ? "%d failure(s)\n" : "all core tests passed\n", fails);
  return fails ? 1 : 0;
}
