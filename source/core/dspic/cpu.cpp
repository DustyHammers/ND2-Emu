#include "cpu.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace dspic {

CPU::CPU() { memset(ram, 0, sizeof ram); }

void CPU::reset(uint32_t start_pc) {
  memset(ram, 0, 0x60);
  setW(15, 0x1000);
  setcore16(A_SPLIM, 0);
  setcore16(A_DSRPAG, 0x0001);
  setcore16(A_DSWPAG, 0x0001);
  setcore16(A_CORCON, 0x0020);   // SATDW=1 at reset
  setcore16(A_SR, 0);
  update_ipl(); update_pages();
  pc = start_pc; halted = false; rcount = 0; in_repeat = false; disi_until = 0;
  hooks.clear(); hooked_op.clear();
  dcache.assign(prog.size() + 2, Insn());          // + 2 invalid sentinels: stepping past the end of the program
  dc = dcache.data(); dc_n = (uint32_t)prog.size();  // stops there exactly as fetch's bad_insn does
  for (uint32_t i = 0; i < prog.size(); i++) redecode(i);      // eager decode of the whole program space
}

static void predecode(Insn& I);
void CPU::redecode(uint32_t i) {
  uint32_t addr = i << 1;
  dcache[i] = decode(progword(addr), progword(addr + 2), addr);
  predecode(dcache[i]);
  for (size_t h = 0; h < hooks.size(); h++) if (hooks[h].pc == addr) {
    hooked_op[h] = dcache[i].op; dcache[i].op = I_HOOK; dcache[i].hook = (uint8_t)(h + 1);
  }
}

void CPU::invalidate(uint32_t addr) {
  uint32_t i = (addr & 0x7FFFFE) >> 1;
  if (i < dc_n) redecode(i);
  if (i > 0 && i - 1 < dc_n) redecode(i - 1);      // a 2-word instruction may end here
}

void CPU::add_hook(uint32_t addr, HookFn fn, void* ctx) {
  hooks.push_back({addr, fn, ctx}); hooked_op.push_back(I_NOP);
  uint32_t i = (addr & 0x7FFFFE) >> 1;
  if (i < dc_n) redecode(i);
}

std::string CPU::disasm_at(uint32_t addr) { return disasm(decode(progword(addr), progword(addr + 2), addr), addr); }

// ---------------------------------------------------------------- data space
uint16_t CPU::rd16_slow(uint16_t a) {
  if (a < 0x60) return is_accreg(a) ? accreg_rd(a) : a == A_SR ? sr_full() : core16(a);
  if (a < 0x1000) return bus ? bus->sfr_read(a) : core16(a);
  if (a < 0x8000) return ram[a] | (ram[a + 1] << 8);
  uint16_t pag = core16(A_DSRPAG);
  if (pag & 0x200) {
    uint32_t paddr = ((uint32_t)(pag & 0x1FF) << 15) | (a & 0x7FFF);
    return progword(paddr) & 0xFFFF;
  }
  if (pag <= 1) return ram[a] | (ram[a + 1] << 8);
  return 0;
}
uint8_t CPU::rd8_slow(uint16_t a) {
  if (a < 0x60) { if (is_accreg(a & ~1)) { uint16_t v = accreg_rd(a & ~1); return (a & 1) ? v >> 8 : v & 0xFF; } if ((a & ~1) == A_SR) return (a & 1) ? sr_full() >> 8 : sr_full() & 0xFF; return ram[a]; }
  if (a < 0x1000) { uint16_t v = bus ? bus->sfr_read(a & 0xFFFE) : core16(a & 0xFFFE); return (a & 1) ? v >> 8 : v & 0xFF; }
  if (a < 0x8000) return ram[a];
  uint16_t pag = core16(A_DSRPAG);
  if (pag & 0x200) {
    uint32_t paddr = ((uint32_t)(pag & 0x1FF) << 15) | (a & 0x7FFE);
    uint32_t w = progword(paddr);
    return (a & 1) ? (w >> 8) & 0xFF : w & 0xFF;
  }
  if (pag <= 1) return ram[a];
  return 0;
}
void CPU::wr16_slow(uint16_t a, uint16_t v) {
  if (a < 0x60) {
    if (a == A_DISICNT) disi_until = cycles + (v & 0x3FFF);
    if (a == A_RCOUNT) rcount = v & 0x3FFF;
    if (is_accreg(a)) { accreg_wr(a, v); return; }
    setcore16(a, v);
    if (a == A_SR || a == A_CORCON) update_ipl();
    if (a == A_DSRPAG || a == A_DSWPAG) update_pages();
    return;
  }
  if (a < 0x1000) { if (bus) bus->sfr_write(a, v, 0xFFFF); else setcore16(a, v); return; }
  if (a < 0x8000) { ram[a] = (uint8_t)v; ram[a + 1] = (uint8_t)(v >> 8); return; }
  uint16_t pag = core16(A_DSWPAG);
  if (pag <= 1) { ram[a] = (uint8_t)v; ram[a + 1] = (uint8_t)(v >> 8); }
}
void CPU::wr8_slow(uint16_t a, uint8_t v) {
  if (a < 0x60) { if (is_accreg(a & ~1)) { uint16_t w = accreg_rd(a & ~1); w = (a & 1) ? (uint16_t)((w & 0xFF) | (v << 8)) : (uint16_t)((w & 0xFF00) | v); accreg_wr(a & ~1, w); return; } ram[a] = v; if ((a & ~1) == A_SR || (a & ~1) == A_CORCON) update_ipl(); if ((a & ~1) == A_DSRPAG || (a & ~1) == A_DSWPAG) update_pages(); if ((a & ~1) == A_DISICNT) disi_until = cycles + (core16(A_DISICNT) & 0x3FFF); return; }
  if (a < 0x1000) {
    if (bus) { int sh = (a & 1) * 8; bus->sfr_write(a & 0xFFFE, (uint16_t)v << sh, 0xFF << sh); } else ram[a] = v;
    return;
  }
  if (a < 0x8000) { ram[a] = v; return; }
  if (core16(A_DSWPAG) <= 1) ram[a] = v;
}

// ---------------------------------------------------------------- addressing
DSPIC_FORCEINLINE uint16_t CPU::ea(int mode, int r, bool byte, int breg) {
  int inc = byte ? 1 : 2; uint16_t w = W(r), a = w;
  switch (mode) {
  case 1: break;
  case 2: setW(r, w - inc); break;
  case 3: setW(r, w + inc); break;
  case 4: a = w - inc; setW(r, a); break;
  case 5: a = w + inc; setW(r, a); break;
  default: a = w + W(breg); break;
  }
  return a;
}
DSPIC_FORCEINLINE uint16_t CPU::rd_op(int mode, int r, bool byte, int breg) {
  if (mode == 0) return byte ? (W(r) & 0xFF) : W(r);
  uint16_t a = ea(mode, r, byte, breg);
  if (mode > 5 && sine_remap) a = remap_sine(a);           // register offset [Ws+Wb]
  return byte ? rd8(a) : rd16(a);
}
DSPIC_FORCEINLINE void CPU::wr_op(int mode, int r, bool byte, uint16_t v, int breg) {
  if (mode == 0) { if (byte) ram[2 * r] = (uint8_t)v; else setW(r, v); return; }
  uint16_t a = ea(mode, r, byte, breg);
  if (byte) wr8(a, (uint8_t)v); else wr16(a, v);
}

// ---------------------------------------------------------------- flags / ALU
DSPIC_FORCEINLINE void CPU::flags_nz(uint16_t r, bool byte) {
  uint16_t s = sr() & ~(SR_N | SR_Z);
  if (byte) { if (!(r & 0xFF)) s |= SR_Z; if (r & 0x80) s |= SR_N; }
  else { if (!r) s |= SR_Z; if (r & 0x8000) s |= SR_N; }
  setsr(s);
}
DSPIC_FORCEINLINE uint16_t CPU::alu_add(uint16_t a, uint16_t b, int cin, bool byte, bool sticky_z) {
  uint32_t mask = byte ? 0xFF : 0xFFFF, sign = byte ? 0x80 : 0x8000;
  a &= mask; b &= mask;
  uint32_t r = a + b + (cin ? 1 : 0);
  uint16_t res = r & mask;
  uint16_t s = sr() & ~(SR_C | SR_DC | SR_OV | SR_N);
  if (r > mask) s |= SR_C;
  if ((a ^ b ^ r) & (byte ? 0x10 : 0x100)) s |= SR_DC;
  if ((~(a ^ b) & (a ^ res)) & sign) s |= SR_OV;
  if (res & sign) s |= SR_N;
  if (sticky_z) { if (res) s &= ~SR_Z; }
  else { s &= ~SR_Z; if (!res) s |= SR_Z; }
  setsr(s);
  return res;
}
bool CPU::cond(int c) const {
  uint16_t s = sr();
  bool C = s & SR_C, Z = s & SR_Z, N = s & SR_N, V = s & SR_OV;
  switch (c) {
  case 0: return V; case 1: return C; case 2: return Z; case 3: return N;
  case 4: return Z || (N != V); case 5: return N != V; case 6: return !C || Z; case 7: return true;
  case 8: return !V; case 9: return !C; case 10: return !Z; case 11: return !N;
  case 12: return !Z && (N == V); case 13: return N == V; case 14: return C && !Z;
  case 0x10: return sr_full() & SR_OA; case 0x11: return sr_full() & SR_OB; case 0x12: return s & SR_SA; case 0x13: return s & SR_SB;
  }
  return false;
}
void CPU::skip_next() {
  uint32_t top = progword(next_pc) >> 16;
  bool two = (top == 0x02 || top == 0x04 || top == 0x08);
  next_pc += two ? 4 : 2;
  cycles += two ? 2 : 1;
}

// ---------------------------------------------------------------- DSP helpers
DSPIC_FORCEINLINE void CPU::store_acc(int b, int64_t v) {
  const int64_t mx = sat_cfg[b];
  bool sat;
  if (mx) { sat = v > mx || v < -mx - 1; if (sat) v = v > 0 ? mx : -mx - 1; }
  else { int64_t w = (int64_t)((uint64_t)v << 24) >> 24; sat = w != v; v = w; }
  accv[b] = v;
  if (sat) ram[A_SR + 1] |= (b ? SR_SB : SR_SA) >> 8;   // sticky; OA/OB/OAB/SAB are derived in sr_full()
}
DSPIC_FORCEINLINE int64_t CPU::product(uint16_t a, uint16_t b, int signmode, bool frac) const {
  int64_t x = (signmode & 1) ? (int64_t)(int16_t)a : (int64_t)a;
  int64_t y = (signmode & 2) ? (int64_t)(int16_t)b : (int64_t)b;
  int64_t p = x * y;
  return frac ? p * 2 : p;
}
DSPIC_FORCEINLINE uint16_t CPU::acc_word(int b, int shift, bool round) {
  int64_t v = acc(b);
  if (shift > 0) v >>= shift; else if (shift < 0) v = (int64_t)((uint64_t)v << (-shift));
  if (round) {
    uint32_t lo = (uint32_t)v & 0xFFFF;
    if (!acc_rnd_conv) { if (lo >= 0x8000) v += 0x10000; }
    else { if (lo > 0x8000 || (lo == 0x8000 && (v & 0x10000))) v += 0x10000; }
  }
  if (acc_satdw) {
    if (v > 0x7FFFFFFFLL) return 0x7FFF;
    if (v < -0x80000000LL) return 0x8000;
  }
  return (uint16_t)(v >> 16);
}
static const int8_t kPre[8] = {0, 2, 4, 6, 0, -6, -4, -2};
static const uint8_t kWm[8][2] = {{4,5},{4,6},{4,7},{0,0},{5,6},{5,7},{6,7},{0,0}};   // MAC multiplicand pairs
// MAC-class operand prefetch and accumulator write-back, worked out once per decoded instruction (CPU-side only:
// the disassembler reads iiii/jjjj/xx/yy/aa, which stay as decoded). Fields the MAC class does not otherwise use:
//   smode / dmode  X / Y prefetch: 0 none, 1 [Wr] with post-modify, 2 [W9+W12] / [W11+W12]
//   wb / ws        X / Y register for kind 1 (W8/W9, W10/W11);  sbreg / dbreg  its post-modify (int8)
//   wd             write-back: 0 none, 1 W13, 2 [W13]+=2 (none for MPY, MPY.N, MAC/MPY Wm*Wm)
//   bit / cond     the multiplicand registers
// SFTAC #k: k clamped to -16..16.
static void prefetch_kind(int c, int base, uint8_t& kind, uint8_t& reg, uint8_t& inc) {
  if (c == 0xC) { kind = 2; reg = 0; inc = 0; }
  else if ((c & 7) == 4) { kind = 0; reg = 0; inc = 0; }
  else { kind = 1; reg = (uint8_t)(base + (c >> 3)); inc = (uint8_t)kPre[c & 7]; }
}
static void predecode(Insn& I) {
  switch (I.op) {
  case I_MAC: case I_MSC: case I_CLRAC: case I_MOVSAC: case I_MPY: case I_MPYN: case I_MACSQ: case I_MPYSQ: {
    prefetch_kind(I.iiii, 8, I.smode, I.wb, I.sbreg);
    prefetch_kind(I.jjjj, 10, I.dmode, I.ws, I.dbreg);
    const bool awb = I.op == I_MAC || I.op == I_MSC || I.op == I_CLRAC || I.op == I_MOVSAC;
    I.wd = awb && I.aa < 2 ? (uint8_t)(I.aa + 1) : 0;
    // bit / cond: the multiplicand registers (Wm*Wn; Wm*Wm for the squaring forms)
    if (I.op == I_MACSQ || I.op == I_MPYSQ) { I.bit = I.cond = (uint8_t)(4 + I.mm); }
    else { I.bit = kWm[I.mm][0]; I.cond = kWm[I.mm][1]; }
    break; }
  case I_SFTACK:                                  // the constant shift, clamped as the instruction does
    if (I.shift > 16) I.shift = 16;
    if (I.shift < -16) I.shift = -16;
    break;
  default: break;
  }
}
// X prefetch: W8/W9 (+W12 indexed), Y prefetch: W10/W11; operands are fetched before any register update.
// do_awb is decided at decode time (Insn::wd); the argument documents it at the call.
DSPIC_FORCEINLINE void CPU::mac_prefetch_awb(const Insn& I, bool) {
  uint16_t vx = 0, vy = 0;
  const int xk = I.smode, yk = I.dmode;
  if (xk == 2) { uint16_t a = W(9) + W(12); if (sine_remap) a = remap_sine(a); vx = rd16(a); }
  else if (xk == 1) { const int r = I.wb; uint16_t a = W(r); vx = rd16(a); setW(r, (uint16_t)(a + (int8_t)I.sbreg)); }
  if (yk == 2) { uint16_t a = W(11) + W(12); if (sine_remap) a = remap_sine(a); vy = rd16(a); }
  else if (yk == 1) { const int r = I.ws; uint16_t a = W(r); vy = rd16(a); setW(r, (uint16_t)(a + (int8_t)I.dbreg)); }
  if (xk) setW(4 + I.xx, vx);
  if (yk) setW(4 + I.yy, vy);
  if (I.wd) {
    uint16_t v = acc_word(!I.acc, 0, true);
    if (I.wd == 1) setW(13, v); else { wr16(W(13), v); setW(13, (uint16_t)(W(13) + 2)); }
  }
}

// ---------------------------------------------------------------- interrupts
void CPU::take_irq(int prio, uint32_t vec) {
  uint16_t s = sr(), cc = corcon();
  push16((uint16_t)pc);
  push16((uint16_t)(((s & 0xFF) << 8) | ((cc & CC_IPL3) ? 0x80 : 0) | ((pc >> 16) & 0x7F)));
  if (prio > 7) { setcore16(A_CORCON, cc | CC_IPL3); }
  else setsr((s & ~SR_IPL) | (prio << 5));
  update_ipl();
  pc = vec;
  cycles += 6;
}

void CPU::saveState(nd2::StateWriter& w) const {
  w.put(uint32_t(1));                                   // version
  w.raw(ram, sizeof ram);
  w.put(accv[0]); w.put(accv[1]);
  w.put(pc); w.put(cycles); w.put(instret);
  w.put(int32_t(rcount)); w.put(uint8_t(in_repeat)); w.put(next_pc);
  w.put(disi_until); w.put(next_event);
  w.put(uint8_t(halted)); w.str(fault);
}

bool CPU::loadState(nd2::StateReader& r) {
  if (r.get<uint32_t>() != 1) return false;
  r.raw(ram, sizeof ram);
  r.get(accv[0]); r.get(accv[1]);
  r.get(pc); r.get(cycles); r.get(instret);
  rcount = r.get<int32_t>(); in_repeat = r.get<uint8_t>() != 0; r.get(next_pc);
  r.get(disi_until); r.get(next_event);
  halted = r.get<uint8_t>() != 0; r.str(fault);
  update_ipl(); update_pages();
  slow_at = 0; run_end = cycles;
  return r.ok();
}

void CPU::flags_sub(uint16_t a, uint16_t b) { alu_add(a, (uint16_t)~b, 1, false, false); }

// ---------------------------------------------------------------- main loop
DSPIC_FORCEINLINE void CPU::exec(const Insn& I) {
  const bool B = I.byte;
  auto srcv = [&]() -> uint16_t { return I.lit ? (uint16_t)I.k : rd_op(I.smode, I.ws, B, I.sbreg); };
  auto fdest = [&](uint16_t v) { if (I.to_wreg) { if (B) ram[0] = (uint8_t)v; else setW(0, v); } else wr_f(I.f, B, v); };
  auto wreg = [&]() -> uint16_t { return B ? (W(0) & 0xFF) : W(0); };
  auto branch = [&](uint32_t t) { next_pc = t; cycles += 3; };
  switch (I.op) {
#define OPCASE(x) case x:
#define OPDEFAULT default:
#define OPEND break;
#define OPJUMP break;
#define DSPIC_SPILL()
#define DSPIC_FILL()
#include "exec_body.inc"
#undef OPCASE
#undef OPDEFAULT
#undef OPEND
#undef OPJUMP
#undef DSPIC_SPILL
#undef DSPIC_FILL
  }
}

// ---------------------------------------------------------------- main loop
void CPU::slow_path() {
  if (cycles >= next_event && bus) bus->sync();
  if (irq_prio > cur_ipl && !(irq_prio < 7 && cycles < disi_until)) {
    take_irq(irq_prio, irq_vec); if (bus) bus->irq_taken(irq_vec);
  }
  // keep checking every instruction while an interrupt is pending but blocked (DISI, nesting)
  slow_at = irq_prio > cur_ipl ? cycles : next_event;
  if (halted) slow_at = 0;
}

void CPU::exec_outlined(const Insn& I) { exec(I); }

// REPEAT: the next instruction runs count+1 times back to back. Interrupts are held off until it completes;
// peripherals are still synced between iterations. (DIV/DIVF consume the whole count in one go.)
void CPU::do_repeat(int count) {
  uint32_t tpc = next_pc;
  const Insn& R = fetch(tpc);
  instret++;                                   // the REPEAT itself
  rcount = count; setcore16(A_RCOUNT, rcount); in_repeat = true;
  uint32_t after = tpc + 2 * R.words;
  for (;;) {
    if (cycles >= next_event && bus) bus->sync();
    pc = tpc; next_pc = after;
    cycles += 1;
    if (trace) trace(pc, R);
    exec(R);
    if (rcount <= 0) break;
    instret++;
    rcount--; setcore16(A_RCOUNT, rcount);
  }
  in_repeat = false;
  next_pc = after;
}

DSPIC_FORCEINLINE void CPU::step_inline() {
  if (cycles >= slow_at) { if (halted) return; slow_path(); }
  const Insn& I = fetch(pc);
  next_pc = pc + 2 * I.words;
  cycles += 1;
  if (trace) trace(pc, I);
  exec(I);
  instret++;
  pc = next_pc & 0x7FFFFE;
}

// The end of the slice is folded into slow_at (clamped to `end`), so the common path tests one bound per instruction.
// Anything that lowers slow_at only ever sets it to 0, which stays within the clamp.
template <bool Trace, bool StopPc> void CPU::run_loop(uint64_t end, uint32_t stop_pc) {
  run_end = end;
  if (slow_at > end) slow_at = end;
  for (;;) {
    if (cycles >= slow_at) {
      if (cycles >= end) break;
      if (StopPc && pc == stop_pc) break;
      if (halted) break;
      slow_path();
      if (slow_at > end) slow_at = end;
    }
    if (StopPc && pc == stop_pc) break;
    const Insn& I = fetch(pc);
    next_pc = pc + 2 * I.words;
    cycles += 1;
    if (Trace) trace(pc, I);
    exec(I);
    instret++;
    pc = next_pc & 0x7FFFFE;
  }
}

void CPU::step() { run_end = cycles; step_inline(); }

// The normal run: the same handlers as exec (exec_body.inc), but each handler ends with the next instruction's
// dispatch and an indirect jump of its own (threaded dispatch), so the host predicts each jump from where it is made.
// pc, next_pc and cycles live in registers here: cycles is stored to the CPU object at the start of every
// instruction (peripherals read it during SFR accesses), pc and next_pc are written back around the calls that
// use them (interrupts, REPEAT, hooks) and on exit. After an instruction that cannot change the flow (OPEND) the
// next one is the cache entry the instruction's length (as used for next_pc) further on; after OPJUMP it is looked
// up from next_pc.
// Instruction for instruction it does exactly what run_loop<false, StopPc> does (StopPc: also stop at the first
// instruction boundary with pc == stop_pc, as run_until_pc).
template <bool StopPc> void CPU::run_fast(uint64_t end, uint32_t stop_pc) {
#if defined(__GNUC__) || defined(__clang__)
#define DSPIC_OP_LABEL(n) &&L_##n,
  static void* const labels[I_COUNT] = { DSPIC_OPS(DSPIC_OP_LABEL) };
#undef DSPIC_OP_LABEL
  run_end = end;
  if (slow_at > end) slow_at = end;
  const Insn* ip = nullptr;
  uint64_t dspic_retired_ = 0;    // instructions retired here, added to instret whenever the fast path is left
  uint32_t dspic_pc_ = pc, dspic_npc_ = next_pc;
  uint64_t dspic_cyc_ = cycles;
  uint32_t dspic_w_ = 0;          // length (words) of the current instruction, as used for next_pc
  // the decoded program: entries may be re-decoded during a run (flash writes, hooks), but the table never moves
  const Insn* const dcl = dc; const uint32_t dcn = dc_n; const Insn* const badp = &bad_insn;
#define I (*ip)
#define B (ip->byte)
#define pc dspic_pc_
#define next_pc dspic_npc_
#define cycles dspic_cyc_
  // the operand helpers as macros (lambdas capturing ip by reference would keep ip in memory); each argument is
  // evaluated exactly once, as with the lambdas in exec
#define srcv() (I.lit ? (uint16_t)I.k : rd_op(I.smode, I.ws, B, I.sbreg))
#define fdest(v) do { uint16_t fdv_ = (uint16_t)(v); \
    if (I.to_wreg) { if (B) ram[0] = (uint8_t)fdv_; else setW(0, fdv_); } else wr_f(I.f, B, fdv_); } while (0)
#define wreg() ((uint16_t)(B ? (W(0) & 0xFF) : W(0)))
#define branch(t) do { uint32_t brt_ = (t); next_pc = brt_; cycles += 3; } while (0)
#define skip_next() do { uint32_t top_ = progword(next_pc) >> 16; bool two_ = (top_ == 0x02 || top_ == 0x04 || top_ == 0x08); \
    next_pc += two_ ? 4 : 2; cycles += two_ ? 2 : 1; } while (0)
#define DSPIC_SPILL() store_pc_cycles(dspic_pc_, dspic_npc_, dspic_cyc_)
#define DSPIC_FILL() load_pc_cycles(dspic_pc_, dspic_npc_, dspic_cyc_)
#define DSPIC_LOOKUP() do { uint32_t ix_ = dspic_pc_ >> 1; ip = ix_ < dcn ? dcl + ix_ : badp; } while (0)
#define DSPIC_RUN() do { if (StopPc && dspic_pc_ == stop_pc) goto L_exit; \
    dspic_w_ = ip->words; dspic_npc_ = dspic_pc_ + 2 * dspic_w_; dspic_cyc_ += 1; store_cycles(dspic_cyc_); \
    goto *labels[ip->op]; } while (0)
#ifdef ND2_CHECK_FLOW
#define DSPIC_CHECK_SEQ() do { if (dspic_npc_ != dspic_pc_ + 2 * ip->words) { std::fprintf(stderr, "flow changed by %s at %06x\n", disasm(I, dspic_pc_).c_str(), dspic_pc_); std::abort(); } } while (0)
#else
#define DSPIC_CHECK_SEQ() do {} while (0)
#endif
#define OPCASE(x) L_##x:
#define OPDEFAULT
#define OPEND { DSPIC_CHECK_SEQ(); dspic_retired_++; \
    if (dspic_cyc_ >= slow_at) { dspic_pc_ = dspic_npc_ & 0x7FFFFE; goto L_slow; } \
    ip += dspic_w_; dspic_pc_ = dspic_npc_; DSPIC_RUN(); }
#define OPJUMP { dspic_retired_++; dspic_pc_ = dspic_npc_ & 0x7FFFFE; if (dspic_cyc_ >= slow_at) goto L_slow; \
    DSPIC_LOOKUP(); DSPIC_RUN(); }
  if (dspic_cyc_ < slow_at) { DSPIC_LOOKUP(); DSPIC_RUN(); }
L_slow:
  instret += dspic_retired_; dspic_retired_ = 0;
  if (dspic_cyc_ >= end || (StopPc && dspic_pc_ == stop_pc) || halted) goto L_exit;
  DSPIC_SPILL();
  slow_path();
  DSPIC_FILL();
  if (slow_at > end) slow_at = end;
  DSPIC_LOOKUP(); DSPIC_RUN();
#include "exec_body.inc"
L_exit:
  instret += dspic_retired_;
  DSPIC_SPILL();
#undef OPCASE
#undef OPDEFAULT
#undef OPEND
#undef OPJUMP
#undef DSPIC_CHECK_SEQ
#undef DSPIC_RUN
#undef DSPIC_LOOKUP
#undef DSPIC_SPILL
#undef DSPIC_FILL
#undef skip_next
#undef srcv
#undef fdest
#undef wreg
#undef branch
#undef cycles
#undef next_pc
#undef pc
#undef B
#undef I
#else
  run_loop<false, StopPc>(end, stop_pc);
#endif
}

void CPU::run_cycles(uint64_t n) {
  if (trace) run_loop<true, false>(cycles + n, 0); else run_fast<false>(cycles + n, 0);
}

void CPU::run_until_pc(uint32_t target, uint64_t max_cycles) {
  if (trace) run_loop<true, true>(cycles + max_cycles, target); else run_fast<true>(cycles + max_cycles, target);
}

}  // namespace dspic
