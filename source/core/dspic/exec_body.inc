// The instruction handlers, shared by the two dispatchers in cpu.cpp (included twice):
//   switch dispatch (CPU::exec: tracing, stepping, REPEAT, hooks):  OPCASE(x) = case x:, OPEND = break;
//   threaded dispatch (CPU::run_fast, the normal run):  OPCASE(x) = label, OPEND = next instruction's dispatch.
// A handler that can change the flow (next_pc other than the following instruction) ends with OPJUMP instead of
// OPEND. DSPIC_SPILL()/DSPIC_FILL() write the threaded dispatcher's registers (pc, next_pc, cycles) back to the CPU
// object and reload them, around calls that use the CPU object's copies.
// In a handler, I is the instruction, B its byte flag, srcv/fdest/wreg/branch the operand helpers. Inner switches
// and loops use plain break; only OPEND ends a handler.
  OPCASE(I_NOP) OPEND
  // ---------------- control
  OPCASE(I_BRA) branch(I.target); OPJUMP
  OPCASE(I_BRACC) if (cond(I.cond)) branch(I.target); OPJUMP
  OPCASE(I_BRAW) branch(pc + 2 + 2 * (int16_t)W(I.ws)); OPJUMP
  OPCASE(I_GOTO) next_pc = I.target; cycles += 3; OPJUMP
  OPCASE(I_GOTOW) next_pc = W(I.ws) & 0xFFFE; cycles += 3; OPJUMP
  OPCASE(I_GOTOL) next_pc = ((W(I.wb) & 0x7F) << 16) | (W(I.ws) & 0xFFFE); cycles += 3; OPJUMP
  OPCASE(I_CALL) OPCASE(I_RCALL) OPCASE(I_CALLW) OPCASE(I_RCALLW) OPCASE(I_CALLL) {
    uint32_t ret = next_pc;
    uint32_t t = I.op == I_CALL || I.op == I_RCALL ? I.target
               : I.op == I_CALLW ? (uint32_t)(W(I.ws) & 0xFFFE)
               : I.op == I_RCALLW ? pc + 2 + 2 * (int16_t)W(I.ws)
               : (((W(I.wb) & 0x7F) << 16) | (W(I.ws) & 0xFFFE));
    push16((uint16_t)ret); push16((ret >> 16) & 0x7F);
    next_pc = t; cycles += 3; OPJUMP }
  OPCASE(I_RETURN) { uint16_t hi = pop16(), lo = pop16(); next_pc = ((hi & 0x7F) << 16) | lo; cycles += 5; OPJUMP }
  OPCASE(I_RETLW) { if (B) ram[2 * I.wd] = (uint8_t)I.k; else setW(I.wd, I.k);
    uint16_t hi = pop16(), lo = pop16(); next_pc = ((hi & 0x7F) << 16) | lo; cycles += 5; OPJUMP }
  OPCASE(I_RETFIE) {
    uint16_t hi = pop16(), lo = pop16();
    setsr((sr() & 0xFF00) | (hi >> 8));
    uint16_t cc = corcon(); setcore16(A_CORCON, (hi & 0x80) ? (cc | CC_IPL3) : (cc & ~CC_IPL3));
    update_ipl();
    next_pc = ((hi & 0x7F) << 16) | lo; cycles += 5; OPJUMP }
  OPCASE(I_REPEAT) DSPIC_SPILL(); do_repeat(I.k); DSPIC_FILL(); OPJUMP
  OPCASE(I_REPEATW) DSPIC_SPILL(); do_repeat(W(I.ws) & 0x3FFF); DSPIC_FILL(); OPJUMP
  OPCASE(I_HOOK) {
    const Hook& h = hooks[I.hook - 1];
    cycles -= 1; DSPIC_SPILL(); h.fn(*this, h.ctx); DSPIC_FILL(); cycles += 1;     // hook sees the state before this instruction
    Insn orig = I; orig.op = hooked_op[I.hook - 1]; DSPIC_SPILL(); exec_outlined(orig); DSPIC_FILL();
    OPJUMP }
  OPCASE(I_DISI) disi_until = cycles + I.k + 1; setcore16(A_DISICNT, I.k); OPEND
  OPCASE(I_RESET) halt("reset instruction"); OPEND
  OPCASE(I_BREAK) halt("break"); OPEND
  OPCASE(I_PWRSAV) idle = true; OPEND
  OPCASE(I_CLRWDT) OPEND
  // ---------------- 3-operand ALU
  OPCASE(I_ADD3) wr_op(I.dmode, I.wd, B, alu_add(W(I.wb), srcv(), 0, B, false)); OPEND
  OPCASE(I_ADDC3) { uint16_t s = srcv(); wr_op(I.dmode, I.wd, B, alu_add(W(I.wb), s, sr() & SR_C, B, true)); OPEND }
  OPCASE(I_SUB3) { uint16_t s = srcv(); wr_op(I.dmode, I.wd, B, alu_add(W(I.wb), ~s, 1, B, false)); OPEND }
  OPCASE(I_SUBB3) { uint16_t s = srcv(); wr_op(I.dmode, I.wd, B, alu_add(W(I.wb), ~s, sr() & SR_C, B, true)); OPEND }
  OPCASE(I_SUBR3) { uint16_t s = srcv(); wr_op(I.dmode, I.wd, B, alu_add(s, ~W(I.wb), 1, B, false)); OPEND }
  OPCASE(I_SUBBR3) { uint16_t s = srcv(); wr_op(I.dmode, I.wd, B, alu_add(s, ~W(I.wb), sr() & SR_C, B, true)); OPEND }
  OPCASE(I_AND3) { uint16_t r = W(I.wb) & srcv(); flags_nz(r, B); wr_op(I.dmode, I.wd, B, r); OPEND }
  OPCASE(I_IOR3) { uint16_t r = W(I.wb) | srcv(); flags_nz(r, B); wr_op(I.dmode, I.wd, B, r); OPEND }
  OPCASE(I_XOR3) { uint16_t r = W(I.wb) ^ srcv(); flags_nz(r, B); wr_op(I.dmode, I.wd, B, r); OPEND }
  // ---------------- literal to Wn
  OPCASE(I_ADDK) wr_op(0, I.wd, B, alu_add(W(I.wd), I.k, 0, B, false)); OPEND
  OPCASE(I_ADDCK) wr_op(0, I.wd, B, alu_add(W(I.wd), I.k, sr() & SR_C, B, true)); OPEND
  OPCASE(I_SUBK) wr_op(0, I.wd, B, alu_add(W(I.wd), ~I.k, 1, B, false)); OPEND
  OPCASE(I_SUBBK) wr_op(0, I.wd, B, alu_add(W(I.wd), ~I.k, sr() & SR_C, B, true)); OPEND
  OPCASE(I_ANDK) { uint16_t r = W(I.wd) & I.k; flags_nz(r, B); wr_op(0, I.wd, B, r); OPEND }
  OPCASE(I_IORK) { uint16_t r = W(I.wd) | I.k; flags_nz(r, B); wr_op(0, I.wd, B, r); OPEND }
  OPCASE(I_XORK) { uint16_t r = W(I.wd) ^ I.k; flags_nz(r, B); wr_op(0, I.wd, B, r); OPEND }
  // ---------------- file register forms
  OPCASE(I_ADDF) fdest(alu_add(rd_f(I.f, B), wreg(), 0, B, false)); OPEND
  OPCASE(I_ADDCF) fdest(alu_add(rd_f(I.f, B), wreg(), sr() & SR_C, B, true)); OPEND
  OPCASE(I_SUBF) fdest(alu_add(rd_f(I.f, B), ~wreg(), 1, B, false)); OPEND
  OPCASE(I_SUBBF) fdest(alu_add(rd_f(I.f, B), ~wreg(), sr() & SR_C, B, true)); OPEND
  OPCASE(I_SUBRF) fdest(alu_add(wreg(), ~rd_f(I.f, B), 1, B, false)); OPEND
  OPCASE(I_SUBBRF) fdest(alu_add(wreg(), ~rd_f(I.f, B), sr() & SR_C, B, true)); OPEND
  OPCASE(I_ANDF) { uint16_t r = rd_f(I.f, B) & wreg(); flags_nz(r, B); fdest(r); OPEND }
  OPCASE(I_IORF) { uint16_t r = rd_f(I.f, B) | wreg(); flags_nz(r, B); fdest(r); OPEND }
  OPCASE(I_XORF) { uint16_t r = rd_f(I.f, B) ^ wreg(); flags_nz(r, B); fdest(r); OPEND }
  OPCASE(I_INCF) fdest(alu_add(rd_f(I.f, B), 1, 0, B, false)); OPEND
  OPCASE(I_INC2F) fdest(alu_add(rd_f(I.f, B), 2, 0, B, false)); OPEND
  OPCASE(I_DECF) fdest(alu_add(rd_f(I.f, B), ~1, 1, B, false)); OPEND
  OPCASE(I_DEC2F) fdest(alu_add(rd_f(I.f, B), ~2, 1, B, false)); OPEND
  OPCASE(I_NEGF) fdest(alu_add(0, ~rd_f(I.f, B), 1, B, false)); OPEND
  OPCASE(I_COMF) { uint16_t r = ~rd_f(I.f, B); flags_nz(r, B); fdest(r); OPEND }
  OPCASE(I_CLRF) fdest(0); OPEND
  OPCASE(I_SETMF) fdest(0xFFFF); OPEND
  OPCASE(I_MOVF) { uint16_t r = rd_f(I.f, B); flags_nz(r, B); fdest(r); OPEND }
  OPCASE(I_MOVWF) wr_f(I.f, B, wreg()); OPEND
  OPCASE(I_MULF) {
    if (B) { setW(2, (uint16_t)((W(0) & 0xFF) * rd8(I.f))); }
    else { uint32_t p = (uint32_t)W(2) * rd16(I.f & 0xFFFE); setW(2, (uint16_t)p); setW(3, (uint16_t)(p >> 16)); }
    OPEND }
  OPCASE(I_CPF) alu_add(rd_f(I.f, B), ~wreg(), 1, B, false); OPEND
  OPCASE(I_CPBF) alu_add(rd_f(I.f, B), ~wreg(), sr() & SR_C, B, true); OPEND
  OPCASE(I_CP0F) alu_add(rd_f(I.f, B), ~0, 1, B, false); OPEND
  OPCASE(I_SLF) OPCASE(I_LSRF) OPCASE(I_ASRF) OPCASE(I_RLCF) OPCASE(I_RLNCF) OPCASE(I_RRCF) OPCASE(I_RRNCF)
  OPCASE(I_SL) OPCASE(I_LSR) OPCASE(I_ASR) OPCASE(I_RLC) OPCASE(I_RLNC) OPCASE(I_RRC) OPCASE(I_RRNC) {
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
    OPEND }
  // ---------------- unary Ws,Wd
  OPCASE(I_INC) { uint16_t v = rd_op(I.smode, I.ws, B); wr_op(I.dmode, I.wd, B, alu_add(v, 1, 0, B, false)); OPEND }
  OPCASE(I_INC2) { uint16_t v = rd_op(I.smode, I.ws, B); wr_op(I.dmode, I.wd, B, alu_add(v, 2, 0, B, false)); OPEND }
  OPCASE(I_DEC) { uint16_t v = rd_op(I.smode, I.ws, B); wr_op(I.dmode, I.wd, B, alu_add(v, ~1, 1, B, false)); OPEND }
  OPCASE(I_DEC2) { uint16_t v = rd_op(I.smode, I.ws, B); wr_op(I.dmode, I.wd, B, alu_add(v, ~2, 1, B, false)); OPEND }
  OPCASE(I_NEG) { uint16_t v = rd_op(I.smode, I.ws, B); wr_op(I.dmode, I.wd, B, alu_add(0, ~v, 1, B, false)); OPEND }
  OPCASE(I_COM) { uint16_t r = ~rd_op(I.smode, I.ws, B); flags_nz(r, B); wr_op(I.dmode, I.wd, B, r); OPEND }
  OPCASE(I_CLR) wr_op(I.dmode, I.wd, B, 0); OPEND
  OPCASE(I_SETM) wr_op(I.dmode, I.wd, B, 0xFFFF); OPEND
  // ---------------- multi-bit shifts
  OPCASE(I_SLK) OPCASE(I_LSRK) OPCASE(I_ASRK) OPCASE(I_SLW) OPCASE(I_LSRW) OPCASE(I_ASRW) {
    int n = (I.op == I_SLK || I.op == I_LSRK || I.op == I_ASRK) ? I.k : (W(I.ws) & 0x1F);
    uint16_t v = W(I.wb); uint16_t r;
    if (I.op == I_SLK || I.op == I_SLW) r = n >= 16 ? 0 : (uint16_t)(v << n);
    else if (I.op == I_LSRK || I.op == I_LSRW) r = n >= 16 ? 0 : (uint16_t)(v >> n);
    else r = (uint16_t)((int16_t)v >> (n >= 16 ? 15 : n));
    flags_nz(r, false); setW(I.wd, r); OPEND }
  // ---------------- moves
  OPCASE(I_MOVK16) setW(I.wd, (uint16_t)I.k); OPEND
  OPCASE(I_MOVK8) ram[2 * I.wd] = (uint8_t)I.k; OPEND
  OPCASE(I_MOVFW) setW(I.wd, rd16(I.f)); OPEND
  OPCASE(I_MOVWFF) wr16(I.f, W(I.ws)); OPEND
  OPCASE(I_MOV) { uint16_t v = rd_op(I.smode, I.ws, B, I.sbreg); wr_op(I.dmode, I.wd, B, v, I.dbreg); OPEND }
  OPCASE(I_MOVSLIT) { uint16_t a = W(I.ws) + I.k; uint16_t v = B ? rd8(a) : rd16(a); if (B) ram[2 * I.wd] = (uint8_t)v; else setW(I.wd, v); OPEND }
  OPCASE(I_MOVDSLIT) { uint16_t a = W(I.wd) + I.k; if (B) wr8(a, W(I.ws) & 0xFF); else wr16(a, W(I.ws)); OPEND }
  OPCASE(I_MOVD_LD) {
    uint16_t lo, hi;
    if (I.smode == 0) { lo = W(I.ws & 0xE); hi = W((I.ws & 0xE) + 1); }
    else {
      uint16_t w = W(I.ws), a = w;
      switch (I.smode) { case 2: setW(I.ws, w - 4); break; case 3: setW(I.ws, w + 4); break;
        case 4: a = w - 4; setW(I.ws, a); break; case 5: a = w + 4; setW(I.ws, a); break; default: break; }
      lo = rd16(a); hi = rd16(a + 2);
    }
    setW(I.wd, lo); setW(I.wd + 1, hi); cycles += 1; OPEND }
  OPCASE(I_MOVD_ST) {
    uint16_t lo = W(I.ws), hi = W(I.ws + 1);
    if (I.dmode == 0) { setW(I.wd & 0xE, lo); setW((I.wd & 0xE) + 1, hi); }
    else {
      uint16_t w = W(I.wd), a = w;
      switch (I.dmode) { case 2: setW(I.wd, w - 4); break; case 3: setW(I.wd, w + 4); break;
        case 4: a = w - 4; setW(I.wd, a); break; case 5: a = w + 4; setW(I.wd, a); break; default: break; }
      wr16(a, lo); wr16(a + 2, hi);
    }
    cycles += 1; OPEND }
  OPCASE(I_PUSHF) push16(rd16(I.f)); OPEND
  OPCASE(I_POPF) { uint16_t v = pop16(); wr16(I.f, v); OPEND }
  OPCASE(I_LNK) push16(W(14)); setW(14, W(15)); setW(15, W(15) + I.k); OPEND
  OPCASE(I_ULNK) setW(15, W(14)); setW(14, pop16()); OPEND
  OPCASE(I_EXCH) { uint16_t a = W(I.ws); setW(I.ws, W(I.wd)); setW(I.wd, a); OPEND }
  OPCASE(I_SWAP) { uint16_t v = W(I.ws);
    if (B) ram[2 * I.ws] = (uint8_t)(((v & 0xF) << 4) | ((v >> 4) & 0xF)); else setW(I.ws, (uint16_t)((v << 8) | (v >> 8))); OPEND }
  OPCASE(I_SE) { uint16_t v = (uint16_t)(int16_t)(int8_t)(rd_op(I.smode, I.ws, true) & 0xFF);
    setW(I.wd, v); flags_nz(v, false); setflag(SR_C, !(v & 0x8000)); OPEND }
  OPCASE(I_ZE) { uint16_t v = rd_op(I.smode, I.ws, true) & 0xFF; setW(I.wd, v); flags_nz(v, false); setflag(SR_C, true); OPEND }
  OPCASE(I_MOVPAGK) OPCASE(I_MOVPAGW) {
    uint16_t v = I.op == I_MOVPAGK ? (uint16_t)I.k : W(I.ws);
    if (I.cond == 0) setcore16(A_DSRPAG, v & 0x3FF);
    else if (I.cond == 1) setcore16(A_DSWPAG, v & 0x1FF);
    else if (I.cond == 2) setcore16(A_TBLPAG, v & 0xFF);
    update_pages();
    OPEND }
  OPCASE(I_PUSHS) OPCASE(I_POPS) OPEND   // shadow registers unused by this firmware
  // ---------------- compares
  OPCASE(I_CP) { uint16_t s = rd_op(I.smode, I.ws, B); alu_add(W(I.wb), ~s, 1, B, false); OPEND }
  OPCASE(I_CPB) { uint16_t s = rd_op(I.smode, I.ws, B); alu_add(W(I.wb), ~s, sr() & SR_C, B, true); OPEND }
  OPCASE(I_CPK) alu_add(W(I.wb), ~I.k, 1, B, false); OPEND
  OPCASE(I_CPBK) alu_add(W(I.wb), ~I.k, sr() & SR_C, B, true); OPEND
  OPCASE(I_CP0) { uint16_t s = rd_op(I.smode, I.ws, B); alu_add(s, ~0, 1, B, false); OPEND }
  OPCASE(I_CPSEQ) OPCASE(I_CPSNE) OPCASE(I_CPSGT) OPCASE(I_CPSLT)
  OPCASE(I_CPBEQ) OPCASE(I_CPBNE) OPCASE(I_CPBGT) OPCASE(I_CPBLT) {
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
    OPJUMP }
  // ---------------- bit ops
  OPCASE(I_BSET) OPCASE(I_BCLR) OPCASE(I_BTG) OPCASE(I_BTST) OPCASE(I_BTSTS) OPCASE(I_BTSS) OPCASE(I_BTSC) OPCASE(I_BTSTW) OPCASE(I_BSW) {
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
    OPJUMP }
  OPCASE(I_BSETF) OPCASE(I_BCLRF) OPCASE(I_BTGF) OPCASE(I_BTSTF) OPCASE(I_BTSTSF) OPCASE(I_BTSSF) OPCASE(I_BTSCF) {
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
    OPJUMP }
  OPCASE(I_FBCL) {
    uint16_t v = rd_op(I.smode, I.ws, false); int s = (v >> 15) & 1; int r = -15; bool found = false;
    for (int i = 14; i >= 0; i--) if (((v >> i) & 1) != s) { r = -(14 - i); found = true; break; }
    setW(I.wd, (uint16_t)r); setflag(SR_C, !found); OPEND }
  OPCASE(I_FF1L) {
    uint16_t v = rd_op(I.smode, I.ws, false); int r = 0;
    for (int i = 15; i >= 0; i--) if ((v >> i) & 1) { r = 16 - i; break; }
    setW(I.wd, (uint16_t)r); setflag(SR_C, v == 0); OPEND }
  OPCASE(I_FF1R) {
    uint16_t v = rd_op(I.smode, I.ws, false); int r = 0;
    for (int i = 0; i < 16; i++) if ((v >> i) & 1) { r = i + 1; break; }
    setW(I.wd, (uint16_t)r); setflag(SR_C, v == 0); OPEND }
  // ---------------- multiply / divide
  OPCASE(I_MUL) OPCASE(I_MULW) OPCASE(I_MULACC) {
    uint16_t s = I.lit ? (uint16_t)I.k : rd_op(I.smode, I.ws, false);
    int sm = I.sign; if (I.lit) sm &= 1;     // literal is unsigned
    int64_t p = product(W(I.wb), s, sm, false);
    if (I.op == I_MUL) { setW(I.wd, (uint16_t)p); setW(I.wd + 1, (uint16_t)((uint64_t)p >> 16)); }
    else if (I.op == I_MULW) setW(I.wd, (uint16_t)p);
    else { if (mul_acc_fractional && !(corcon() & CC_IF)) p *= 2; store_acc(I.acc, p); }
    OPEND }
  OPCASE(I_DIV) {
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
    OPEND }
  OPCASE(I_DIVF) {
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
    OPEND }
  // ---------------- table read/write
  OPCASE(I_TBLRDL) OPCASE(I_TBLRDH) {
    uint16_t a = I.smode == 0 ? W(I.ws) : ea(I.smode, I.ws, B, 0);
    uint32_t paddr = ((uint32_t)core16(A_TBLPAG) << 16) | a;
    uint32_t w = progword(paddr & ~1u);
    if (tblrd_hook) tblrd_hook(paddr);
    uint16_t v;
    if (I.op == I_TBLRDL) v = B ? ((paddr & 1) ? (w >> 8) & 0xFF : w & 0xFF) : (w & 0xFFFF);
    else v = B ? ((paddr & 1) ? 0 : (w >> 16) & 0xFF) : ((w >> 16) & 0xFF);
    wr_op(I.dmode, I.wd, B, v); cycles += 4; OPEND }
  OPCASE(I_TBLWTL) OPCASE(I_TBLWTH) {
    uint16_t v = rd_op(I.smode, I.ws, B);
    uint16_t a = I.dmode == 0 ? W(I.wd) : ea(I.dmode, I.wd, B, 0);
    uint32_t paddr = ((uint32_t)core16(A_TBLPAG) << 16) | a;
    if (bus) bus->tblwt(paddr, v, I.op == I_TBLWTH, B);
    cycles += 1; OPEND }
  // ---------------- DSP
  // MAC class: one case per operation (same arithmetic and order as one shared case: product, accumulator, then
  // the prefetches and the accumulator write-back; MPY/MPYN/MACSQ/MPYSQ have no write-back)
  OPCASE(I_MAC) { int64_t p = product(W(kWm[I.mm][0]), W(kWm[I.mm][1]), dsp_sm, dsp_frac);
    store_acc(I.acc, acc(I.acc) + p); mac_prefetch_awb(I, true); OPEND }
  OPCASE(I_MSC) { int64_t p = product(W(kWm[I.mm][0]), W(kWm[I.mm][1]), dsp_sm, dsp_frac);
    store_acc(I.acc, acc(I.acc) - p); mac_prefetch_awb(I, true); OPEND }
  OPCASE(I_MPY) { int64_t p = product(W(kWm[I.mm][0]), W(kWm[I.mm][1]), dsp_sm, dsp_frac);
    store_acc(I.acc, p); mac_prefetch_awb(I, false); OPEND }
  OPCASE(I_MPYN) { int64_t p = product(W(kWm[I.mm][0]), W(kWm[I.mm][1]), dsp_sm, dsp_frac);
    store_acc(I.acc, -p); mac_prefetch_awb(I, false); OPEND }
  OPCASE(I_CLRAC) store_acc(I.acc, 0); mac_prefetch_awb(I, true); OPEND
  OPCASE(I_MOVSAC) mac_prefetch_awb(I, true); OPEND
  OPCASE(I_MACSQ) { uint16_t v = W(4 + I.mm); int64_t p = product(v, v, dsp_sm, dsp_frac);
    store_acc(I.acc, acc(I.acc) + p); mac_prefetch_awb(I, false); OPEND }
  OPCASE(I_MPYSQ) { uint16_t v = W(4 + I.mm); int64_t p = product(v, v, dsp_sm, dsp_frac);
    store_acc(I.acc, p); mac_prefetch_awb(I, false); OPEND }
  OPCASE(I_SFTACK) OPCASE(I_SFTACW) {
    int k = I.op == I_SFTACK ? I.shift : (int)(int16_t)W(I.wb);
    if (k > 16) k = 16; if (k < -16) k = -16;
    int64_t v = acc(I.acc);
    v = k >= 0 ? (v >> k) : (int64_t)((uint64_t)v << (-k));
    store_acc(I.acc, v); OPEND }
  OPCASE(I_LAC) OPCASE(I_ADDACW) {
    int64_t x = (int64_t)(int16_t)rd_op(I.smode, I.ws, false, I.sbreg) * 65536;
    x = I.shift >= 0 ? (x >> I.shift) : (int64_t)((uint64_t)x << (-I.shift));
    store_acc(I.acc, I.op == I_LAC ? x : acc(I.acc) + x); OPEND }
  OPCASE(I_ADDAB) store_acc(I.acc, acc(I.acc) + acc(!I.acc)); OPEND
  OPCASE(I_SUBAB) store_acc(I.acc, acc(I.acc) - acc(!I.acc)); OPEND
  OPCASE(I_NEGAB) store_acc(I.acc, -acc(I.acc)); OPEND
  OPCASE(I_SAC) OPCASE(I_SACR) wr_op(I.smode, I.ws, false, acc_word(I.acc, I.shift, I.op == I_SACR), I.sbreg); OPEND
  OPCASE(I_INVALID) OPCASE(I_DO) OPCASE(I_DOW) OPCASE(I_ED) OPCASE(I_EDAC) OPDEFAULT {
    char b[96]; snprintf(b, sizeof b, "unimplemented/invalid instruction %06x at %06x (%s)", progword(pc), pc, disasm(I, pc).c_str());
    halt(b);
    OPJUMP }

