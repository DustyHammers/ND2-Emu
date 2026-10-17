// dsPIC33E core emulator (subset sufficient for the original OS v3.00)
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <functional>
#include "insn.h"
#include "../util/state_io.h"

#if defined(_MSC_VER)
#define DSPIC_FORCEINLINE __forceinline
#else
#define DSPIC_FORCEINLINE inline __attribute__((always_inline))
#endif

namespace dspic {

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "CPU::ld16/st16 assume a little-endian host"
#endif

enum : uint16_t {  // core SFR byte addresses
  A_SPLIM = 0x20, A_ACCAL = 0x22, A_ACCAH = 0x24, A_ACCAU = 0x26,
  A_ACCBL = 0x28, A_ACCBH = 0x2A, A_ACCBU = 0x2C, A_PCL = 0x2E, A_PCH = 0x30,
  A_DSRPAG = 0x32, A_DSWPAG = 0x34, A_RCOUNT = 0x36, A_DCOUNT = 0x38,
  A_SR = 0x42, A_CORCON = 0x44, A_MODCON = 0x46, A_DISICNT = 0x52, A_TBLPAG = 0x54,
};

enum SRbits : uint16_t {
  SR_C = 1, SR_Z = 2, SR_OV = 4, SR_N = 8, SR_RA = 0x10, SR_IPL = 0xE0,
  SR_DC = 0x100, SR_DA = 0x200, SR_SAB = 0x400, SR_OAB = 0x800,
  SR_SB = 0x1000, SR_SA = 0x2000, SR_OB = 0x4000, SR_OA = 0x8000,
};
enum CORbits : uint16_t {
  CC_IF = 1, CC_RND = 2, CC_SFA = 4, CC_IPL3 = 8, CC_ACCSAT = 0x10, CC_SATDW = 0x20,
  CC_SATB = 0x40, CC_SATA = 0x80, CC_US = 0x3000,
};

// Peripheral side: SFRs 0x0060..0x0FFF and NVM table writes
struct Bus {
  virtual ~Bus() {}
  virtual uint16_t sfr_read(uint16_t addr) = 0;                         // word-aligned
  virtual void sfr_write(uint16_t addr, uint16_t v, uint16_t mask) = 0;  // mask = bytes written
  virtual void tblwt(uint32_t paddr, uint16_t v, bool high, bool byte) {}
  virtual void sync() {}                                                 // catch peripherals up to cpu.cycles
  virtual void irq_taken(uint32_t vec) {}
};

class CPU {
public:
  CPU();
  std::vector<uint32_t> prog;   // 24-bit program words, index = pc/2
  Bus* bus = nullptr;
  bool mul_acc_fractional = true;
  bool bend_nosat = false, bend_int = false;
  void bend_setz(bool on) { setflag(SR_Z, on); }          // BEND experiment   // MUL.xx Wb,Ws,Acc obeys CORCON.IF (fractional <<1)

  int64_t accv[2] = {0, 0};    // ACCA/ACCB (40-bit, sign-extended); memory-mapped at 0x22..0x2D
  uint32_t pc = 0;
  uint64_t cycles = 0, instret = 0;
  // interrupt controller (board): highest pending enabled priority > cur, fills vector address
  // interrupt controller state published by the board: highest pending enabled priority + vector
  int irq_prio = 0; uint32_t irq_vec = 0;
  int cur_ipl = 0;              // effective CPU priority (SR.IPL, or 8 when CORCON.IPL3); kept in sync on SR/CORCON writes
  void update_ipl() {
    uint16_t cc = corcon();
    slow_at = 0;
    sat_cfg[0] = (cc & CC_SATA) ? ((cc & CC_ACCSAT) ? 0x7FFFFFFFFFLL : 0x7FFFFFFFLL) : 0;
    sat_cfg[1] = (cc & CC_SATB) ? ((cc & CC_ACCSAT) ? 0x7FFFFFFFFFLL : 0x7FFFFFFFLL) : 0;
    if (bend_nosat) { sat_cfg[0] = sat_cfg[1] = 0; }
    cur_ipl = (cc & CC_IPL3) ? 8 : ((sr() & SR_IPL) >> 5);
    dsp_frac = !(cc & CC_IF); if (bend_int) dsp_frac = !dsp_frac;
    acc_rnd_conv = !(cc & CC_RND); acc_satdw = (cc & CC_SATDW) != 0 && !bend_nosat;
    int us = (cc & CC_US) >> 12; dsp_sm = us == 0 ? 3 : us == 1 ? 0 : 2;   // 10 = mixed: Wm unsigned x Wn signed
  }
  bool dsp_frac = true; int dsp_sm = 3; int64_t sat_cfg[2] = {0, 0};   // cached from CORCON (sat_cfg: max |acc|, 0 = no saturation)
  bool acc_rnd_conv = true, acc_satdw = true;                          // cached from CORCON: convergent rounding, SATDW
  uint64_t next_event = 0;      // board asks to be synced when cycles reach this
  uint64_t slow_at = 0;         // next cycle at which the step loop must sync / check interrupts
  void attention() { slow_at = 0; }   // call after changing next_event, irq_prio/irq_vec or the CPU priority
  uint64_t run_end = 0;         // end of the current run slice; fast-forward never crosses it
  bool halted = false;
  std::string fault;
  std::function<void(uint32_t pc, const Insn&)> trace;
  std::function<void(uint32_t paddr)> tblrd_hook;   // debug: program-space table reads

  void reset(uint32_t start_pc);
  void step();
  // project state: registers, RAM, accumulators, timing (program memory is the board's business)
  void saveState(nd2::StateWriter& w) const;
  bool loadState(nd2::StateReader& r);
  void run_cycles(uint64_t n);
  void run_until_pc(uint32_t target, uint64_t max_cycles);   // stops at the first instruction boundary with pc == target

  // Hooks run before the instruction at `pc` executes; used to fast-forward pure busy-wait loops cycle-exactly.
  using HookFn = void (*)(CPU& cpu, void* ctx);
  void add_hook(uint32_t pc, HookFn fn, void* ctx);
  // cycles that may be skipped without crossing a peripheral event or a DISI expiry
  void flags_sub(uint16_t a, uint16_t b);   // SR as after SUB.W a - b
  uint64_t ff_budget() const {
    uint64_t lim = next_event < run_end ? next_event : run_end;
    if (irq_prio && cycles < disi_until && disi_until < lim) lim = disi_until;
    return lim > cycles + 1 ? lim - cycles - 1 : 0;   // land strictly before the event: the skipped-to instruction must not be one the interpreter would have preempted
  }

  // 16-bit little-endian words in ram[]: one load/store (the hosts, x86-64 and arm64, are little-endian)
  uint16_t ld16(uint32_t a) const { uint16_t v; std::memcpy(&v, ram + a, 2); return v; }
  void st16(uint32_t a, uint16_t v) { std::memcpy(ram + a, &v, 2); }
  uint16_t W(int n) const { return ld16(2u * (unsigned)n); }
  void setW(int n, uint16_t v) { st16(2u * (unsigned)n, v); }
  uint16_t core16(uint16_t a) const { return ld16(a); }
  static bool is_accreg(uint16_t a) { return a >= A_ACCAL && a < A_PCL; }
  uint16_t accreg_rd(uint16_t a) const {      // word view of ACCxL/H/U
    int b = a >= A_ACCBL; int w = (a - (b ? A_ACCBL : A_ACCAL)) >> 1; int64_t v = accv[b];
    return w == 0 ? (uint16_t)v : w == 1 ? (uint16_t)(v >> 16) : (uint16_t)(int16_t)(int8_t)(v >> 32);
  }
  void accreg_wr(uint16_t a, uint16_t x) {
    int b = a >= A_ACCBL; int w = (a - (b ? A_ACCBL : A_ACCAL)) >> 1; uint64_t v = (uint64_t)accv[b];
    if (w == 0) v = (v & ~0xFFFFull) | x; else if (w == 1) v = (v & ~0xFFFF0000ull) | ((uint64_t)x << 16);
    else v = (v & 0xFFFFFFFFull) | ((uint64_t)(uint8_t)x << 32);
    setacc_raw(b, (int64_t)v);
  }
  void setcore16(uint16_t a, uint16_t v) { st16(a, v); }
  uint16_t sr() const { return core16(A_SR); }   // raw: OA/OB/OAB/SAB are not maintained here (see sr_full)
  uint16_t sr_full() const {                       // architectural SR: accumulator overflow bits derived from ACCA/ACCB
    uint16_t s = core16(A_SR) & ~(SR_OA | SR_OB | SR_OAB | SR_SAB);
    if ((uint64_t)(accv[0] + 0x80000000LL) > 0xFFFFFFFFULL) s |= SR_OA | SR_OAB;
    if ((uint64_t)(accv[1] + 0x80000000LL) > 0xFFFFFFFFULL) s |= SR_OB | SR_OAB;
    if (s & (SR_SA | SR_SB)) s |= SR_SAB;
    return s;
  }
  uint16_t corcon() const { return core16(A_CORCON); }
  uint32_t progword(uint32_t addr) const { uint32_t i = (addr & 0x7FFFFE) >> 1; return i < prog.size() ? prog[i] : 0xFFFFFF; }
  void invalidate(uint32_t addr);   // after a flash write: re-decode the words touching addr

  // data space access; plain RAM 0x1000..0x7FFF is the inline fast path
  // (0x8000..0xFFFF is RAM too while DSRPAG/DSWPAG select EDS page 0/1)
  uint16_t rd16(uint16_t a) { a &= 0xFFFE; if ((uint16_t)(a - 0x1000) < rd_lim) return ld16(a); return rd16_slow(a); }
  uint8_t  rd8(uint16_t a)  { if ((uint16_t)(a - 0x1000) < rd_lim) return ram[a]; return rd8_slow(a); }
  void wr16(uint16_t a, uint16_t v) { a &= 0xFFFE; if ((uint16_t)(a - 0x1000) < wr_lim) { st16(a, v); return; } wr16_slow(a, v); }
  void wr8(uint16_t a, uint8_t v)   { if ((uint16_t)(a - 0x1000) < wr_lim) { ram[a] = v; return; } wr8_slow(a, v); }
  uint16_t rd_lim = 0xF000, wr_lim = 0xF000;
  void update_pages() { rd_lim = core16(A_DSRPAG) <= 1 ? 0xF000 : 0x7000; wr_lim = core16(A_DSWPAG) <= 1 ? 0xF000 : 0x7000; }
  uint16_t rd16_slow(uint16_t a);
  uint8_t  rd8_slow(uint16_t a);
  void wr16_slow(uint16_t a, uint16_t v);
  void wr8_slow(uint16_t a, uint8_t v);
  int64_t acc(int b) const { return accv[b]; }
  void setacc_raw(int b, int64_t v) { accv[b] = (int64_t)((uint64_t)v << 24) >> 24; }   // 40-bit, sign-extended
  std::string disasm_at(uint32_t addr);

private:
  std::vector<Insn> dcache;
  struct Hook { uint32_t pc; HookFn fn; void* ctx; };
  std::vector<Hook> hooks;
  void redecode(uint32_t i);
  const Insn* dc = nullptr; uint32_t dc_n = 0;
  const Insn& fetch(uint32_t addr) { uint32_t i = (addr & 0x7FFFFE) >> 1; return i < dc_n ? dc[i] : bad_insn; }
  void slow_path();
  template <bool Trace, bool StopPc> void run_loop(uint64_t end, uint32_t stop_pc);
  template <bool StopPc> void run_fast(uint64_t end, uint32_t stop_pc);   // run_loop<false, StopPc>, threaded dispatch
  // run_fast keeps pc, next_pc and cycles in registers; these move them to and from the object
  void store_pc_cycles(uint32_t p, uint32_t np, uint64_t c) { pc = p; next_pc = np; cycles = c; }
  void load_pc_cycles(uint32_t& p, uint32_t& np, uint64_t& c) const { p = pc; np = next_pc; c = cycles; }
  void store_cycles(uint64_t c) { cycles = c; }
  void exec_outlined(const Insn& I);
  void do_repeat(int count);
  void halt(const char* why) { halted = true; fault = why; slow_at = 0; }
  std::vector<Op> hooked_op;   // original op of instructions replaced by I_HOOK
  Insn bad_insn;
  DSPIC_FORCEINLINE void step_inline();
  int rcount = 0; bool in_repeat = false; uint32_t next_pc = 0;
public:
  uint64_t disi_until = 0; bool idle = false;
private:
  DSPIC_FORCEINLINE void exec(const Insn& I);
  void take_irq(int prio, uint32_t vec);
  void setsr(uint16_t v) { setcore16(A_SR, v); }
  void setflag(uint16_t f, bool on) { uint16_t s = sr(); setsr(on ? (s | f) : (s & ~f)); }
  DSPIC_FORCEINLINE void flags_nz(uint16_t r, bool byte);
  DSPIC_FORCEINLINE uint16_t ea(int mode, int reg, bool byte, int breg);
  DSPIC_FORCEINLINE uint16_t rd_op(int mode, int reg, bool byte, int breg = 0);
  DSPIC_FORCEINLINE void     wr_op(int mode, int reg, bool byte, uint16_t v, int breg = 0);
  uint16_t rd_f(uint16_t f, bool byte) { return byte ? rd8(f) : rd16(f & 0xFFFE); }
  void     wr_f(uint16_t f, bool byte, uint16_t v) { if (byte) wr8(f, (uint8_t)v); else wr16(f & 0xFFFE, v); }
  DSPIC_FORCEINLINE uint16_t alu_add(uint16_t a, uint16_t b, int cin, bool byte, bool sticky_z);
  bool cond(int c) const;
  void skip_next();
  void push16(uint16_t v) { uint16_t sp = W(15); wr16(sp, v); setW(15, sp + 2); }
  uint16_t pop16() { uint16_t sp = W(15) - 2; setW(15, sp); return rd16(sp); }
  // DSP
  DSPIC_FORCEINLINE void store_acc(int b, int64_t v);              // saturate per CORCON, update OA/SA flags
  DSPIC_FORCEINLINE int64_t product(uint16_t a, uint16_t b, int signmode, bool frac) const;
  DSPIC_FORCEINLINE uint16_t acc_word(int b, int shift, bool round);   // SAC/SAC.R/AWB value with SATDW
  DSPIC_FORCEINLINE void mac_prefetch_awb(const Insn& I, bool do_awb);
public:
  // data space (W regs, core SFRs, RAM incl. EDS page 1). Last member on purpose: the hot scalars above stay at
  // small offsets from `this` (short addressing on arm64, fewer pinned registers on x86-64).
  alignas(64) uint8_t ram[0x10000];
};

}  // namespace dspic
