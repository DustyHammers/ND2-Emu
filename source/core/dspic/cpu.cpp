#include "cpu.h"
#include <cstring>
#include <cstdio>

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
  dcache.resize(prog.size());
  dc = dcache.data(); dc_n = (uint32_t)dcache.size();
  for (uint32_t i = 0; i < prog.size(); i++) redecode(i);      // eager decode of the whole program space
}

void CPU::redecode(uint32_t i) {
  uint32_t addr = i << 1;
  dcache[i] = decode(progword(addr), progword(addr + 2), addr);
  for (size_t h = 0; h < hooks.size(); h++) if (hooks[h].pc == addr) {
    hooked_op[h] = dcache[i].op; dcache[i].op = I_HOOK; dcache[i].hook = (uint8_t)(h + 1);
  }
}

void CPU::invalidate(uint32_t addr) {
  uint32_t i = (addr & 0x7FFFFE) >> 1;
  if (i < dcache.size()) redecode(i);
  if (i > 0 && i - 1 < dcache.size()) redecode(i - 1);      // a 2-word instruction may end here
}

void CPU::add_hook(uint32_t addr, HookFn fn, void* ctx) {
  hooks.push_back({addr, fn, ctx}); hooked_op.push_back(I_NOP);
  uint32_t i = (addr & 0x7FFFFE) >> 1;
  if (i < dcache.size()) redecode(i);
}

std::string CPU::disasm_at(uint32_t addr) { return disasm(fetch(addr), addr); }

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
DSPIC_FORCEINLINE void CPU::mac_prefetch_awb(const Insn& I, bool do_awb) {
  // X prefetch: W8/W9 (+W12 indexed), Y prefetch: W10/W11; operands are fetched before any register update
  int ci = I.iiii, cj = I.jjjj;
  uint16_t vx = 0, vy = 0;
  bool hx = true, hy = true;
  if (ci == 0xC) vx = rd16(W(9) + W(12));
  else if ((ci & 7) == 4) hx = false;
  else { int r = 8 + (ci >> 3); uint16_t a = W(r); vx = rd16(a); setW(r, (uint16_t)(a + kPre[ci & 7])); }
  if (cj == 0xC) vy = rd16(W(11) + W(12));
  else if ((cj & 7) == 4) hy = false;
  else { int r = 10 + (cj >> 3); uint16_t a = W(r); vy = rd16(a); setW(r, (uint16_t)(a + kPre[cj & 7])); }
  if (hx) setW(4 + I.xx, vx);
  if (hy) setW(4 + I.yy, vy);
  if (do_awb && I.aa < 2) {
    uint16_t v = acc_word(!I.acc, 0, true);
    if (I.aa == 0) setW(13, v); else { wr16(W(13), v); setW(13, (uint16_t)(W(13) + 2)); }
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
  case I_NOP: break;
  // ---------------- control
  case I_BRA: branch(I.target); break;
  case I_BRACC: if (cond(I.cond)) branch(I.target); break;
  case I_BRAW: branch(pc + 2 + 2 * (int16_t)W(I.ws)); break;
  case I_GOTO: next_pc = I.target; cycles += 3; break;
  case I_GOTOW: next_pc = W(I.ws) & 0xFFFE; cycles += 3; break;
  case I_GOTOL: next_pc = ((W(I.wb) & 0x7F) << 16) | (W(I.ws) & 0xFFFE); cycles += 3; break;
  case I_CALL: case I_RCALL: case I_CALLW: case I_RCALLW: case I_CALLL: {
    uint32_t ret = next_pc;
    uint32_t t = I.op == I_CALL || I.op == I_RCALL ? I.target
               : I.op == I_CALLW ? (uint32_t)(W(I.ws) & 0xFFFE)
               : I.op == I_RCALLW ? pc + 2 + 2 * (int16_t)W(I.ws)
               : (((W(I.wb) & 0x7F) << 16) | (W(I.ws) & 0xFFFE));
    push16((uint16_t)ret); push16((ret >> 16) & 0x7F);
    next_pc = t; cycles += 3; break; }
  case I_RETURN: { uint16_t hi = pop16(), lo = pop16(); next_pc = ((hi & 0x7F) << 16) | lo; cycles += 5; break; }
  case I_RETLW: { if (B) ram[2 * I.wd] = (uint8_t)I.k; else setW(I.wd, I.k);
    uint16_t hi = pop16(), lo = pop16(); next_pc = ((hi & 0x7F) << 16) | lo; cycles += 5; break; }
  case I_RETFIE: {
    uint16_t hi = pop16(), lo = pop16();
    setsr((sr() & 0xFF00) | (hi >> 8));
    uint16_t cc = corcon(); setcore16(A_CORCON, (hi & 0x80) ? (cc | CC_IPL3) : (cc & ~CC_IPL3));
    update_ipl();
    next_pc = ((hi & 0x7F) << 16) | lo; cycles += 5; break; }
  case I_REPEAT: do_repeat(I.k); break;
  case I_REPEATW: do_repeat(W(I.ws) & 0x3FFF); break;
  case I_HOOK: {
    const Hook& h = hooks[I.hook - 1];
    cycles -= 1; h.fn(*this, h.ctx); cycles += 1;     // hook sees the state before this instruction
    Insn orig = I; orig.op = hooked_op[I.hook - 1]; exec_outlined(orig);
    break; }
  case I_DISI: disi_until = cycles + I.k + 1; setcore16(A_DISICNT, I.k); break;
  case I_RESET: halt("reset instruction"); break;
  case I_BREAK: halt("break"); break;
  case I_PWRSAV: idle = true; break;
  case I_CLRWDT: break;
  // ---------------- 3-operand ALU
  case I_ADD3: wr_op(I.dmode, I.wd, B, alu_add(W(I.wb), srcv(), 0, B, false)); break;
  case I_ADDC3: { uint16_t s = srcv(); wr_op(I.dmode, I.wd, B, alu_add(W(I.wb), s, sr() & SR_C, B, true)); break; }
  case I_SUB3: { uint16_t s = srcv(); wr_op(I.dmode, I.wd, B, alu_add(W(I.wb), ~s, 1, B, false)); break; }
  case I_SUBB3: { uint16_t s = srcv(); wr_op(I.dmode, I.wd, B, alu_add(W(I.wb), ~s, sr() & SR_C, B, true)); break; }
  case I_SUBR3: { uint16_t s = srcv(); wr_op(I.dmode, I.wd, B, alu_add(s, ~W(I.wb), 1, B, false)); break; }
  case I_SUBBR3: { uint16_t s = srcv(); wr_op(I.dmode, I.wd, B, alu_add(s, ~W(I.wb), sr() & SR_C, B, true)); break; }
  case I_AND3: { uint16_t r = W(I.wb) & srcv(); flags_nz(r, B); wr_op(I.dmode, I.wd, B, r); break; }
  case I_IOR3: { uint16_t r = W(I.wb) | srcv(); flags_nz(r, B); wr_op(I.dmode, I.wd, B, r); break; }
  case I_XOR3: { uint16_t r = W(I.wb) ^ srcv(); flags_nz(r, B); wr_op(I.dmode, I.wd, B, r); break; }
  // ---------------- literal to Wn
  case I_ADDK: wr_op(0, I.wd, B, alu_add(W(I.wd), I.k, 0, B, false)); break;
  case I_ADDCK: wr_op(0, I.wd, B, alu_add(W(I.wd), I.k, sr() & SR_C, B, true)); break;
  case I_SUBK: wr_op(0, I.wd, B, alu_add(W(I.wd), ~I.k, 1, B, false)); break;
  case I_SUBBK: wr_op(0, I.wd, B, alu_add(W(I.wd), ~I.k, sr() & SR_C, B, true)); break;
  case I_ANDK: { uint16_t r = W(I.wd) & I.k; flags_nz(r, B); wr_op(0, I.wd, B, r); break; }
  case I_IORK: { uint16_t r = W(I.wd) | I.k; flags_nz(r, B); wr_op(0, I.wd, B, r); break; }
  case I_XORK: { uint16_t r = W(I.wd) ^ I.k; flags_nz(r, B); wr_op(0, I.wd, B, r); break; }
  // ---------------- file register forms
  case I_ADDF: fdest(alu_add(rd_f(I.f, B), wreg(), 0, B, false)); break;
  case I_ADDCF: fdest(alu_add(rd_f(I.f, B), wreg(), sr() & SR_C, B, true)); break;
  case I_SUBF: fdest(alu_add(rd_f(I.f, B), ~wreg(), 1, B, false)); break;
  case I_SUBBF: fdest(alu_add(rd_f(I.f, B), ~wreg(), sr() & SR_C, B, true)); break;
  case I_SUBRF: fdest(alu_add(wreg(), ~rd_f(I.f, B), 1, B, false)); break;
  case I_SUBBRF: fdest(alu_add(wreg(), ~rd_f(I.f, B), sr() & SR_C, B, true)); break;
  case I_ANDF: { uint16_t r = rd_f(I.f, B) & wreg(); flags_nz(r, B); fdest(r); break; }
  case I_IORF: { uint16_t r = rd_f(I.f, B) | wreg(); flags_nz(r, B); fdest(r); break; }
  case I_XORF: { uint16_t r = rd_f(I.f, B) ^ wreg(); flags_nz(r, B); fdest(r); break; }
  case I_INCF: fdest(alu_add(rd_f(I.f, B), 1, 0, B, false)); break;
  case I_INC2F: fdest(alu_add(rd_f(I.f, B), 2, 0, B, false)); break;
  case I_DECF: fdest(alu_add(rd_f(I.f, B), ~1, 1, B, false)); break;
  case I_DEC2F: fdest(alu_add(rd_f(I.f, B), ~2, 1, B, false)); break;
  case I_NEGF: fdest(alu_add(0, ~rd_f(I.f, B), 1, B, false)); break;
  case I_COMF: { uint16_t r = ~rd_f(I.f, B); flags_nz(r, B); fdest(r); break; }
  case I_CLRF: fdest(0); break;
  case I_SETMF: fdest(0xFFFF); break;
  case I_MOVF: { uint16_t r = rd_f(I.f, B); flags_nz(r, B); fdest(r); break; }
  case I_MOVWF: wr_f(I.f, B, wreg()); break;
  case I_MULF: {
    if (B) { setW(2, (uint16_t)((W(0) & 0xFF) * rd8(I.f))); }
    else { uint32_t p = (uint32_t)W(2) * rd16(I.f & 0xFFFE); setW(2, (uint16_t)p); setW(3, (uint16_t)(p >> 16)); }
    break; }
  case I_CPF: alu_add(rd_f(I.f, B), ~wreg(), 1, B, false); break;
  case I_CPBF: alu_add(rd_f(I.f, B), ~wreg(), sr() & SR_C, B, true); break;
  case I_CP0F: alu_add(rd_f(I.f, B), ~0, 1, B, false); break;
  case I_SLF: case I_LSRF: case I_ASRF: case I_RLCF: case I_RLNCF: case I_RRCF: case I_RRNCF:
  case I_SL: case I_LSR: case I_ASR: case I_RLC: case I_RLNC: case I_RRC: case I_RRNC: {
    bool isf = I.op <= I_RRNCF;
    uint16_t v = isf ? rd_f(I.f, B) : rd_op(I.smode, I.ws, B);
    int top = B ? 7 : 15; uint16_t mask = B ? 0xFF : 0xFFFF;
    uint16_t s = sr(); bool c = s & SR_C, nc = c; uint16_t r = 0;
    Op base = isf ? (Op)(I.op - I_SLF + I_SL) : I.op;
    switch (base) {
    case I_SL: nc = (v >> top) & 1; r = (v << 1) & mask; break;
    case I_LSR: nc = v & 1; r = (v & mask) >> 1; break;
    case I_ASR: nc = v & 1; r = ((v & mask) >> 1) | (v & (1 << top)); break;
    case I_RLC: nc = (v >> top) & 1; r = ((v << 1) | c) & mask; break;
    case I_RLNC: r = ((v << 1) | ((v >> top) & 1)) & mask; break;
    case I_RRC: nc = v & 1; r = ((v & mask) >> 1) | (c ? (1 << top) : 0); break;
    case I_RRNC: r = ((v & mask) >> 1) | ((v & 1) << top); break;
    default: break;
    }
    flags_nz(r, B);
    if (base != I_RLNC && base != I_RRNC) setflag(SR_C, nc);
    if (isf) fdest(r); else wr_op(I.dmode, I.wd, B, r);
    break; }
  // ---------------- unary Ws,Wd
  case I_INC: { uint16_t v = rd_op(I.smode, I.ws, B); wr_op(I.dmode, I.wd, B, alu_add(v, 1, 0, B, false)); break; }
  case I_INC2: { uint16_t v = rd_op(I.smode, I.ws, B); wr_op(I.dmode, I.wd, B, alu_add(v, 2, 0, B, false)); break; }
  case I_DEC: { uint16_t v = rd_op(I.smode, I.ws, B); wr_op(I.dmode, I.wd, B, alu_add(v, ~1, 1, B, false)); break; }
  case I_DEC2: { uint16_t v = rd_op(I.smode, I.ws, B); wr_op(I.dmode, I.wd, B, alu_add(v, ~2, 1, B, false)); break; }
  case I_NEG: { uint16_t v = rd_op(I.smode, I.ws, B); wr_op(I.dmode, I.wd, B, alu_add(0, ~v, 1, B, false)); break; }
  case I_COM: { uint16_t r = ~rd_op(I.smode, I.ws, B); flags_nz(r, B); wr_op(I.dmode, I.wd, B, r); break; }
  case I_CLR: wr_op(I.dmode, I.wd, B, 0); break;
  case I_SETM: wr_op(I.dmode, I.wd, B, 0xFFFF); break;
  // ---------------- multi-bit shifts
  case I_SLK: case I_LSRK: case I_ASRK: case I_SLW: case I_LSRW: case I_ASRW: {
    int n = (I.op == I_SLK || I.op == I_LSRK || I.op == I_ASRK) ? I.k : (W(I.ws) & 0x1F);
    uint16_t v = W(I.wb); uint16_t r;
    if (I.op == I_SLK || I.op == I_SLW) r = n >= 16 ? 0 : (uint16_t)(v << n);
    else if (I.op == I_LSRK || I.op == I_LSRW) r = n >= 16 ? 0 : (uint16_t)(v >> n);
    else r = (uint16_t)((int16_t)v >> (n >= 16 ? 15 : n));
    flags_nz(r, false); setW(I.wd, r); break; }
  // ---------------- moves
  case I_MOVK16: setW(I.wd, (uint16_t)I.k); break;
  case I_MOVK8: ram[2 * I.wd] = (uint8_t)I.k; break;
  case I_MOVFW: setW(I.wd, rd16(I.f)); break;
  case I_MOVWFF: wr16(I.f, W(I.ws)); break;
  case I_MOV: { uint16_t v = rd_op(I.smode, I.ws, B, I.sbreg); wr_op(I.dmode, I.wd, B, v, I.dbreg); break; }
  case I_MOVSLIT: { uint16_t a = W(I.ws) + I.k; uint16_t v = B ? rd8(a) : rd16(a); if (B) ram[2 * I.wd] = (uint8_t)v; else setW(I.wd, v); break; }
  case I_MOVDSLIT: { uint16_t a = W(I.wd) + I.k; if (B) wr8(a, W(I.ws) & 0xFF); else wr16(a, W(I.ws)); break; }
  case I_MOVD_LD: {
    uint16_t lo, hi;
    if (I.smode == 0) { lo = W(I.ws & 0xE); hi = W((I.ws & 0xE) + 1); }
    else {
      uint16_t w = W(I.ws), a = w;
      switch (I.smode) { case 2: setW(I.ws, w - 4); break; case 3: setW(I.ws, w + 4); break;
        case 4: a = w - 4; setW(I.ws, a); break; case 5: a = w + 4; setW(I.ws, a); break; default: break; }
      lo = rd16(a); hi = rd16(a + 2);
    }
    setW(I.wd, lo); setW(I.wd + 1, hi); cycles += 1; break; }
  case I_MOVD_ST: {
    uint16_t lo = W(I.ws), hi = W(I.ws + 1);
    if (I.dmode == 0) { setW(I.wd & 0xE, lo); setW((I.wd & 0xE) + 1, hi); }
    else {
      uint16_t w = W(I.wd), a = w;
      switch (I.dmode) { case 2: setW(I.wd, w - 4); break; case 3: setW(I.wd, w + 4); break;
        case 4: a = w - 4; setW(I.wd, a); break; case 5: a = w + 4; setW(I.wd, a); break; default: break; }
      wr16(a, lo); wr16(a + 2, hi);
    }
    cycles += 1; break; }
  case I_PUSHF: push16(rd16(I.f)); break;
  case I_POPF: { uint16_t v = pop16(); wr16(I.f, v); break; }
  case I_LNK: push16(W(14)); setW(14, W(15)); setW(15, W(15) + I.k); break;
  case I_ULNK: setW(15, W(14)); setW(14, pop16()); break;
  case I_EXCH: { uint16_t a = W(I.ws); setW(I.ws, W(I.wd)); setW(I.wd, a); break; }
  case I_SWAP: { uint16_t v = W(I.ws);
    if (B) ram[2 * I.ws] = (uint8_t)(((v & 0xF) << 4) | ((v >> 4) & 0xF)); else setW(I.ws, (uint16_t)((v << 8) | (v >> 8))); break; }
  case I_SE: { uint16_t v = (uint16_t)(int16_t)(int8_t)(rd_op(I.smode, I.ws, true) & 0xFF);
    setW(I.wd, v); flags_nz(v, false); setflag(SR_C, !(v & 0x8000)); break; }
  case I_ZE: { uint16_t v = rd_op(I.smode, I.ws, true) & 0xFF; setW(I.wd, v); flags_nz(v, false); setflag(SR_C, true); break; }
  case I_MOVPAGK: case I_MOVPAGW: {
    uint16_t v = I.op == I_MOVPAGK ? (uint16_t)I.k : W(I.ws);
    if (I.cond == 0) setcore16(A_DSRPAG, v & 0x3FF);
    else if (I.cond == 1) setcore16(A_DSWPAG, v & 0x1FF);
    else if (I.cond == 2) setcore16(A_TBLPAG, v & 0xFF);
    update_pages();
    break; }
  case I_PUSHS: case I_POPS: break;   // shadow registers unused by this firmware
  // ---------------- compares
  case I_CP: { uint16_t s = rd_op(I.smode, I.ws, B); alu_add(W(I.wb), ~s, 1, B, false); break; }
  case I_CPB: { uint16_t s = rd_op(I.smode, I.ws, B); alu_add(W(I.wb), ~s, sr() & SR_C, B, true); break; }
  case I_CPK: alu_add(W(I.wb), ~I.k, 1, B, false); break;
  case I_CPBK: alu_add(W(I.wb), ~I.k, sr() & SR_C, B, true); break;
  case I_CP0: { uint16_t s = rd_op(I.smode, I.ws, B); alu_add(s, ~0, 1, B, false); break; }
  case I_CPSEQ: case I_CPSNE: case I_CPSGT: case I_CPSLT:
  case I_CPBEQ: case I_CPBNE: case I_CPBGT: case I_CPBLT: {
    int32_t a, b;
    if (B) { a = (int8_t)W(I.wb); b = (int8_t)W(I.ws); } else { a = (int16_t)W(I.wb); b = (int16_t)W(I.ws); }
    bool t = false;
    switch (I.op) {
    case I_CPSEQ: case I_CPBEQ: t = a == b; break;
    case I_CPSNE: case I_CPBNE: t = a != b; break;
    case I_CPSGT: case I_CPBGT: t = a > b; break;
    default: t = a < b; break;
    }
    if (I.op >= I_CPBEQ) { if (t) { next_pc = I.target; cycles += 4; } }
    else if (t) skip_next();
    break; }
  // ---------------- bit ops
  case I_BSET: case I_BCLR: case I_BTG: case I_BTST: case I_BTSTS: case I_BTSS: case I_BTSC: case I_BTSTW: case I_BSW: {
    bool byte = (I.op == I_BSET || I.op == I_BCLR || I.op == I_BTG) ? B : false;
    int bit = I.op == I_BTSTW || I.op == I_BSW ? (W(I.wb) & 15) : I.bit;
    if (byte) bit &= 7;
    uint16_t a = 0; uint16_t v;
    bool reg = I.smode == 0;
    if (reg) v = W(I.ws); else { a = ea(I.smode, I.ws, byte, 0); v = byte ? rd8(a) : rd16(a); }
    bool bv = (v >> bit) & 1;
    bool write = false;
    switch (I.op) {
    case I_BSET: v |= 1 << bit; write = true; break;
    case I_BCLR: v &= ~(1 << bit); write = true; break;
    case I_BTG: v ^= 1 << bit; write = true; break;
    case I_BTST: case I_BTSTW: if (I.cond) setflag(SR_Z, !bv); else setflag(SR_C, bv); break;
    case I_BTSTS: if (I.cond) setflag(SR_Z, !bv); else setflag(SR_C, bv); v |= 1 << bit; write = true; break;
    case I_BTSS: if (bv) skip_next(); break;
    case I_BTSC: if (!bv) skip_next(); break;
    case I_BSW: { bool nb = I.cond ? !(sr() & SR_Z) : (sr() & SR_C); v = nb ? (v | (1 << bit)) : (v & ~(1 << bit)); write = true; break; }
    default: break;
    }
    if (write) { if (reg) { if (byte) ram[2 * I.ws] = (uint8_t)v; else setW(I.ws, v); } else { if (byte) wr8(a, (uint8_t)v); else wr16(a, v); } }
    break; }
  case I_BSETF: case I_BCLRF: case I_BTGF: case I_BTSTF: case I_BTSTSF: case I_BTSSF: case I_BTSCF: {
    uint16_t a = I.f + (I.bit >> 3); int bit = I.bit & 7;
    uint8_t v = rd8(a); bool bv = (v >> bit) & 1;
    switch (I.op) {
    case I_BSETF: wr8(a, v | (1 << bit)); break;
    case I_BCLRF: wr8(a, v & ~(1 << bit)); break;
    case I_BTGF: wr8(a, v ^ (1 << bit)); break;
    case I_BTSTF: setflag(SR_Z, !bv); break;
    case I_BTSTSF: setflag(SR_Z, !bv); wr8(a, v | (1 << bit)); break;
    case I_BTSSF: if (bv) skip_next(); break;
    case I_BTSCF: if (!bv) skip_next(); break;
    default: break;
    }
    break; }
  case I_FBCL: {
    uint16_t v = rd_op(I.smode, I.ws, false); int s = (v >> 15) & 1; int r = -15; bool found = false;
    for (int i = 14; i >= 0; i--) if (((v >> i) & 1) != s) { r = -(14 - i); found = true; break; }
    setW(I.wd, (uint16_t)r); setflag(SR_C, !found); break; }
  case I_FF1L: {
    uint16_t v = rd_op(I.smode, I.ws, false); int r = 0;
    for (int i = 15; i >= 0; i--) if ((v >> i) & 1) { r = 16 - i; break; }
    setW(I.wd, (uint16_t)r); setflag(SR_C, v == 0); break; }
  case I_FF1R: {
    uint16_t v = rd_op(I.smode, I.ws, false); int r = 0;
    for (int i = 0; i < 16; i++) if ((v >> i) & 1) { r = i + 1; break; }
    setW(I.wd, (uint16_t)r); setflag(SR_C, v == 0); break; }
  // ---------------- multiply / divide
  case I_MUL: case I_MULW: case I_MULACC: {
    uint16_t s = I.lit ? (uint16_t)I.k : rd_op(I.smode, I.ws, false);
    int sm = I.sign; if (I.lit) sm &= 1;     // literal is unsigned
    int64_t p = product(W(I.wb), s, sm, false);
    if (I.op == I_MUL) { setW(I.wd, (uint16_t)p); setW(I.wd + 1, (uint16_t)((uint64_t)p >> 16)); }
    else if (I.op == I_MULW) setW(I.wd, (uint16_t)p);
    else { if (mul_acc_fractional && !(corcon() & CC_IF)) p *= 2; store_acc(I.acc, p); }
    break; }
  case I_DIV: {
    uint16_t s = sr() & ~(SR_N | SR_Z | SR_C | SR_OV);
    int32_t q = 0, r = 0; bool ov = false;
    uint16_t dv = W(I.ws);
    if (I.sign) {
      int32_t num = I.byte ? (int16_t)W(I.wd) : (int32_t)(((uint32_t)W(I.wd + 1) << 16) | W(I.wd));
      int32_t den = (int16_t)dv;
      if (den == 0) ov = true; else { int64_t qq = (int64_t)num / den; q = (int32_t)qq; r = (int32_t)((int64_t)num - qq * den); if (qq > 32767 || qq < -32768) ov = true; }
    } else {
      uint32_t num = I.byte ? W(I.wd) : (((uint32_t)W(I.wd + 1) << 16) | W(I.wd));
      if (dv == 0) ov = true; else { uint32_t qq = num / dv; q = (int32_t)qq; r = (int32_t)(num % dv); if (qq > 0xFFFF) ov = true; }
    }
    setW(0, (uint16_t)q); setW(1, (uint16_t)r);
    if (ov) s |= SR_OV;
    if ((uint16_t)q == 0) s |= SR_Z;
    if ((uint16_t)q & 0x8000) s |= SR_N;
    setsr(s);
    if (in_repeat) { cycles += rcount; rcount = 0; }
    break; }
  case I_DIVF: {
    uint16_t s = sr() & ~(SR_N | SR_Z | SR_C | SR_OV);
    int32_t num = (int32_t)(int16_t)W(I.wb), den = (int16_t)W(I.ws);
    int32_t q = 0, r = 0; bool ov = false;
    if (den == 0) ov = true;
    else {
      int64_t n = (int64_t)num * 32768; int64_t qq = n / den;
      if ((num < 0 ? -num : num) >= (den < 0 ? -den : den)) ov = true;
      q = (int32_t)qq; r = (int32_t)(n - qq * den);
    }
    setW(0, (uint16_t)q); setW(1, (uint16_t)r);
    if (ov) s |= SR_OV;
    if ((uint16_t)q == 0) s |= SR_Z;
    if ((uint16_t)q & 0x8000) s |= SR_N;
    setsr(s);
    if (in_repeat) { cycles += rcount; rcount = 0; }
    break; }
  // ---------------- table read/write
  case I_TBLRDL: case I_TBLRDH: {
    uint16_t a = I.smode == 0 ? W(I.ws) : ea(I.smode, I.ws, B, 0);
    uint32_t paddr = ((uint32_t)core16(A_TBLPAG) << 16) | a;
    uint32_t w = progword(paddr & ~1u);
    if (tblrd_hook) tblrd_hook(paddr);
    uint16_t v;
    if (I.op == I_TBLRDL) v = B ? ((paddr & 1) ? (w >> 8) & 0xFF : w & 0xFF) : (w & 0xFFFF);
    else v = B ? ((paddr & 1) ? 0 : (w >> 16) & 0xFF) : ((w >> 16) & 0xFF);
    wr_op(I.dmode, I.wd, B, v); cycles += 4; break; }
  case I_TBLWTL: case I_TBLWTH: {
    uint16_t v = rd_op(I.smode, I.ws, B);
    uint16_t a = I.dmode == 0 ? W(I.wd) : ea(I.dmode, I.wd, B, 0);
    uint32_t paddr = ((uint32_t)core16(A_TBLPAG) << 16) | a;
    if (bus) bus->tblwt(paddr, v, I.op == I_TBLWTH, B);
    cycles += 1; break; }
  // ---------------- DSP
  // MAC class: one case per operation (same arithmetic and order as one shared case: product, accumulator, then
  // the prefetches and the accumulator write-back; MPY/MPYN/MACSQ/MPYSQ have no write-back)
  case I_MAC: { int64_t p = product(W(kWm[I.mm][0]), W(kWm[I.mm][1]), dsp_sm, dsp_frac);
    store_acc(I.acc, acc(I.acc) + p); mac_prefetch_awb(I, true); break; }
  case I_MSC: { int64_t p = product(W(kWm[I.mm][0]), W(kWm[I.mm][1]), dsp_sm, dsp_frac);
    store_acc(I.acc, acc(I.acc) - p); mac_prefetch_awb(I, true); break; }
  case I_MPY: { int64_t p = product(W(kWm[I.mm][0]), W(kWm[I.mm][1]), dsp_sm, dsp_frac);
    store_acc(I.acc, p); mac_prefetch_awb(I, false); break; }
  case I_MPYN: { int64_t p = product(W(kWm[I.mm][0]), W(kWm[I.mm][1]), dsp_sm, dsp_frac);
    store_acc(I.acc, -p); mac_prefetch_awb(I, false); break; }
  case I_CLRAC: store_acc(I.acc, 0); mac_prefetch_awb(I, true); break;
  case I_MOVSAC: mac_prefetch_awb(I, true); break;
  case I_MACSQ: { uint16_t v = W(4 + I.mm); int64_t p = product(v, v, dsp_sm, dsp_frac);
    store_acc(I.acc, acc(I.acc) + p); mac_prefetch_awb(I, false); break; }
  case I_MPYSQ: { uint16_t v = W(4 + I.mm); int64_t p = product(v, v, dsp_sm, dsp_frac);
    store_acc(I.acc, p); mac_prefetch_awb(I, false); break; }
  case I_SFTACK: case I_SFTACW: {
    int k = I.op == I_SFTACK ? I.shift : (int)(int16_t)W(I.wb);
    if (k > 16) k = 16; if (k < -16) k = -16;
    int64_t v = acc(I.acc);
    v = k >= 0 ? (v >> k) : (int64_t)((uint64_t)v << (-k));
    store_acc(I.acc, v); break; }
  case I_LAC: case I_ADDACW: {
    int64_t x = (int64_t)(int16_t)rd_op(I.smode, I.ws, false, I.sbreg) * 65536;
    x = I.shift >= 0 ? (x >> I.shift) : (int64_t)((uint64_t)x << (-I.shift));
    store_acc(I.acc, I.op == I_LAC ? x : acc(I.acc) + x); break; }
  case I_ADDAB: store_acc(I.acc, acc(I.acc) + acc(!I.acc)); break;
  case I_SUBAB: store_acc(I.acc, acc(I.acc) - acc(!I.acc)); break;
  case I_NEGAB: store_acc(I.acc, -acc(I.acc)); break;
  case I_SAC: case I_SACR: wr_op(I.smode, I.ws, false, acc_word(I.acc, I.shift, I.op == I_SACR), I.sbreg); break;
  default:
    char b[96]; snprintf(b, sizeof b, "unimplemented/invalid instruction %06x at %06x (%s)", progword(pc), pc, disasm(I, pc).c_str());
    halt(b);
    break;
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

void CPU::run_cycles(uint64_t n) {
  if (trace) run_loop<true, false>(cycles + n, 0); else run_loop<false, false>(cycles + n, 0);
}

void CPU::run_until_pc(uint32_t target, uint64_t max_cycles) {
  if (trace) run_loop<true, true>(cycles + max_cycles, target); else run_loop<false, true>(cycles + max_cycles, target);
}

}  // namespace dspic
