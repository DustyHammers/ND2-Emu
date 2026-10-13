#include "insn.h"
#include <cstdio>

namespace dspic {

static inline uint32_t bits(uint32_t w, int hi, int lo) { return (w >> lo) & ((1u << (hi - lo + 1)) - 1); }
static inline int32_t sbits(uint32_t w, int hi, int lo) {
  int n = hi - lo + 1; int32_t v = (int32_t)bits(w, hi, lo);
  if (v & (1 << (n - 1))) v -= (1 << n);
  return v;
}

// standard Ws (bits 6:4 mode, 3:0 reg) and Wd (bits 13:11 mode, 10:7 reg)
static void ws_std(Insn& I, uint32_t w) { I.smode = bits(w, 6, 4); I.ws = bits(w, 3, 0); }
static void wd_std(Insn& I, uint32_t w) { I.dmode = bits(w, 13, 11); I.wd = bits(w, 10, 7); }

Insn decode(uint32_t w, uint32_t w2, uint32_t pc) {
  Insn I;
  uint32_t top = w >> 16;
  bool b15 = (w >> 15) & 1, b14 = (w >> 14) & 1;
  uint32_t lo16 = w & 0xFFFF;
  auto f_form = [&](Op op) {   // OOOO OOOO xBDf ffff ffff ffff
    I.op = op; I.byte = b14; I.to_wreg = !((w >> 13) & 1); I.f = bits(w, 12, 0);
  };
  auto three = [&](Op op) {    // xxxx xwww wBqq qddd dppp ssss ; literal if ppp=11x
    I.op = op; I.byte = b14; I.wb = bits(w, 18, 15); wd_std(I, w);
    if (bits(w, 6, 5) == 3) { I.lit = true; I.k = bits(w, 4, 0); } else ws_std(I, w);
  };
  auto unary = [&](Op op) {    // xxxx xxxx xBqq qddd dppp ssss
    I.op = op; I.byte = b14; wd_std(I, w); ws_std(I, w);
  };
  switch (top) {
  case 0x00: I.op = I_NOP; break;
  case 0x01: {
    uint32_t a = bits(w, 15, 12), b = bits(w, 11, 8), c = bits(w, 7, 4);
    I.ws = bits(w, 3, 0);
    if (b15) {
      I.wb = bits(w, 14, 11);
      uint32_t s = bits(w, 10, 8);
      if (s == 0) I.op = I_CALLL; else if (s == 4) I.op = I_GOTOL;
    } else if (c == 0) {
      if (a == 0 && b == 0) I.op = I_CALLW;
      else if ((a == 2 && b == 0) || (a == 0 && b == 2)) I.op = I_RCALLW;
      else if ((a == 4 && b == 0) || (a == 0 && b == 4)) I.op = I_GOTOW;
      else if ((a == 6 && b == 0) || (a == 0 && b == 6)) I.op = I_BRAW;
    }
    break; }
  case 0x02: case 0x04:
    I.op = top == 2 ? I_CALL : I_GOTO; I.words = 2;
    I.target = (lo16 & 0xFFFE) | ((w2 & 0x7F) << 16);
    break;
  case 0x05: I.op = I_RETLW; I.byte = b14; I.k = bits(w, 13, 4); I.wd = bits(w, 3, 0); break;
  case 0x06:
    if (lo16 == 0) I.op = I_RETURN; else if (lo16 == 0x4000) I.op = I_RETFIE;
    break;
  case 0x07: I.op = I_RCALL; I.target = pc + 2 + 2 * (int16_t)lo16; break;
  case 0x08: I.words = 2; I.op = b15 ? I_DOW : I_DO; I.k = bits(w, 14, 0); I.ws = bits(w, 3, 0);
    I.target = pc + 4 + 2 * (int16_t)(w2 & 0xFFFF); break;
  case 0x09:
    if (!b15) { I.op = I_REPEAT; I.k = bits(w, 14, 0); }
    else if (bits(w, 14, 4) == 0) { I.op = I_REPEATW; I.ws = bits(w, 3, 0); }
    break;
  case 0x0C: case 0x0D: case 0x0E: case 0x0F:
    I.op = I_BRACC; I.cond = 0x10 | (top & 0xF); I.target = pc + 2 + 2 * (int16_t)lo16; break;
  case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17: three(I_SUBR3); break;
  case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F: three(I_SUBBR3); break;
  case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
  case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
    I.op = I_MOVK16; I.k = bits(w, 19, 4); I.wd = bits(w, 3, 0); break;
  case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
  case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E:
    I.op = top == 0x37 ? I_BRA : I_BRACC; I.cond = top & 0xF; I.target = pc + 2 + 2 * (int16_t)lo16; break;
  case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: three(I_ADD3); break;
  case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: three(I_ADDC3); break;
  case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: three(I_SUB3); break;
  case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: three(I_SUBB3); break;
  case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67: three(I_AND3); break;
  case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6E: case 0x6F: three(I_XOR3); break;
  case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77: three(I_IOR3); break;
  case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F:
    I.op = I_MOV; I.byte = b14; wd_std(I, w); ws_std(I, w);
    I.sbreg = I.dbreg = bits(w, 18, 15);
    break;
  case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
    I.op = I_MOVFW; I.f = bits(w, 18, 4) << 1; I.wd = bits(w, 3, 0); break;
  case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F:
    I.op = I_MOVWFF; I.f = bits(w, 18, 4) << 1; I.ws = bits(w, 3, 0); break;
  case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
  case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F: {
    I.byte = b14;
    int32_t v = sbits(w, 18, 15) * 64 | (bits(w, 13, 11) << 3) | bits(w, 6, 4);   // * 64: a negative value shifted left
    I.k = I.byte ? v : v * 2;
    if (top < 0x98) { I.op = I_MOVSLIT; I.ws = bits(w, 3, 0); I.wd = bits(w, 10, 7); }
    else { I.op = I_MOVDSLIT; I.wd = bits(w, 10, 7); I.ws = bits(w, 3, 0); }
    break; }
  case 0xA0: case 0xA1: case 0xA2: case 0xA4: case 0xA6: case 0xA7:
    I.op = top == 0xA0 ? I_BSET : top == 0xA1 ? I_BCLR : top == 0xA2 ? I_BTG : top == 0xA4 ? I_BTSTS : top == 0xA6 ? I_BTSS : I_BTSC;
    I.bit = bits(w, 15, 12); I.byte = (top <= 0xA2) ? (bool)((w >> 10) & 1) : false; ws_std(I, w);
    if (top == 0xA4) I.cond = (w >> 11) & 1;   // Z variant
    break;
  case 0xA3: I.op = I_BTST; I.bit = bits(w, 15, 12); I.cond = (w >> 11) & 1; ws_std(I, w); break;
  case 0xA5: I.op = I_BTSTW; I.cond = b15; I.wb = bits(w, 14, 11); ws_std(I, w); break;
  case 0xAD: I.op = I_BSW; I.cond = b15; I.wb = bits(w, 14, 11); ws_std(I, w); break;
  case 0xA8: case 0xA9: case 0xAA: case 0xAB: case 0xAC: case 0xAE: case 0xAF:
    I.op = top == 0xA8 ? I_BSETF : top == 0xA9 ? I_BCLRF : top == 0xAA ? I_BTGF : top == 0xAB ? I_BTSTF : top == 0xAC ? I_BTSTSF : top == 0xAE ? I_BTSSF : I_BTSCF;
    I.f = (bits(w, 12, 1) << 1); I.bit = (bits(w, 0, 0) << 3) | bits(w, 15, 13);
    I.byte = !(w & 1);   // display hint only (Ghidra .b/.w)
    break;
  case 0xB0: I.op = b15 ? I_ADDCK : I_ADDK; I.byte = b14; I.k = bits(w, 13, 4); I.wd = bits(w, 3, 0); break;
  case 0xB1: I.op = b15 ? I_SUBBK : I_SUBK; I.byte = b14; I.k = bits(w, 13, 4); I.wd = bits(w, 3, 0); break;
  case 0xB2: I.op = b15 ? I_XORK : I_ANDK; I.byte = b14; I.k = bits(w, 13, 4); I.wd = bits(w, 3, 0); break;
  case 0xB3:
    if (bits(w, 15, 12) == 0xC) { I.op = I_MOVK8; I.byte = true; I.k = bits(w, 11, 4); I.wd = bits(w, 3, 0); }
    else if (!b15) { I.op = I_IORK; I.byte = b14; I.k = bits(w, 13, 4); I.wd = bits(w, 3, 0); }
    break;
  case 0xB4: f_form(b15 ? I_ADDCF : I_ADDF); break;
  case 0xB5: f_form(b15 ? I_SUBBF : I_SUBF); break;
  case 0xB6: f_form(b15 ? I_XORF : I_ANDF); break;
  case 0xB7: if (b15) { if ((w >> 13) & 1) { I.op = I_MOVWF; I.byte = b14; I.f = bits(w, 12, 0); } } else f_form(I_IORF); break;
  case 0xB8: case 0xB9: {
    I.wb = bits(w, 14, 11); I.wd = bits(w, 10, 7);
    // sign: B8 bit15=0 UU, B8 bit15=1 US, B9 bit15=0 SU, B9 bit15=1 SS
    bool bs = top == 0xB9, ss = b15 ^ (top == 0xB9) ? false : false;
    (void)ss;
    if (top == 0xB8) I.sign = b15 ? 2 : 0;   // US: Wb unsigned, Ws signed
    else I.sign = b15 ? 3 : 1;                // SU: Wb signed, Ws unsigned / SS
    (void)bs;
    if (bits(w, 6, 5) == 3) { I.lit = true; I.k = bits(w, 4, 0); } else ws_std(I, w);
    if (bits(w, 10, 8) == 7) { I.op = I_MULACC; I.acc = (w >> 7) & 1; }
    else if ((w >> 7) & 1) { I.op = I_MULW; I.wd = bits(w, 10, 8) << 1; }
    else { I.op = I_MUL; I.wd = bits(w, 10, 8) << 1; }
    break; }
  case 0xBA: I.op = b15 ? I_TBLRDH : I_TBLRDL; I.byte = b14; wd_std(I, w); ws_std(I, w); break;
  case 0xBB: I.op = b15 ? I_TBLWTH : I_TBLWTL; I.byte = b14; wd_std(I, w); ws_std(I, w); break;
  case 0xBC: if (!b15) { I.op = I_MULF; I.byte = b14; I.f = bits(w, 12, 0); } break;
  case 0xBD: f_form(b15 ? I_SUBBRF : I_SUBRF); break;
  case 0xBE:
    if (bits(w, 15, 14) == 2) { I.op = I_MOVD_ST; wd_std(I, w); I.ws = bits(w, 3, 0) & 0xE; }
    else if (bits(w, 15, 11) == 0) { I.op = I_MOVD_LD; I.wd = bits(w, 10, 8) << 1; ws_std(I, w); }
    break;
  case 0xBF: if (b15) f_form(I_MOVF); break;
  case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: case 0xC6: case 0xC7: {
    I.mm = bits(w, 18, 16); I.acc = b15; I.xx = bits(w, 13, 12); I.yy = bits(w, 11, 10);
    I.iiii = bits(w, 9, 6); I.jjjj = bits(w, 5, 2); I.aa = bits(w, 1, 0);
    if (I.mm == 3) I.op = b14 ? I_INVALID : I_CLRAC;
    else if (I.mm == 7) I.op = b14 ? I_INVALID : I_MOVSAC;
    else if (I.aa == 3) I.op = b14 ? I_MPYN : I_MPY;
    else I.op = b14 ? I_MSC : I_MAC;
    break; }
  case 0xC8:
    I.acc = b15;
    if (bits(w, 14, 8) == 0 && bits(w, 7, 6) == 1) { I.op = I_SFTACK; I.shift = (int8_t)sbits(w, 5, 0); }
    else if (bits(w, 14, 4) == 0) { I.op = I_SFTACW; I.wb = bits(w, 3, 0); }
    break;
  case 0xC9: case 0xCA: case 0xCC: case 0xCD:
    I.op = top == 0xC9 ? I_ADDACW : top == 0xCA ? I_LAC : top == 0xCC ? I_SAC : I_SACR;
    I.acc = b15; I.shift = (int8_t)sbits(w, 10, 7); ws_std(I, w); I.sbreg = bits(w, 14, 11);
    break;
  case 0xCB:
    I.acc = b15;
    if ((lo16 & 0x7FFF) == 0x0000) I.op = I_ADDAB;
    else if ((lo16 & 0x7FFF) == 0x1000) I.op = I_NEGAB;
    else if ((lo16 & 0x7FFF) == 0x3000) I.op = I_SUBAB;
    break;
  case 0xCF:
    if (bits(w, 15, 11) == 0x10) { I.op = I_FF1L; I.wd = bits(w, 10, 7); ws_std(I, w); }
    else if (bits(w, 15, 11) == 0) { I.op = I_FF1R; I.wd = bits(w, 10, 7); ws_std(I, w); }
    break;
  case 0xD0: if (!b15) unary(I_SL); break;
  case 0xD1: unary(b15 ? I_ASR : I_LSR); break;
  case 0xD2: unary(b15 ? I_RLC : I_RLNC); break;
  case 0xD3: unary(b15 ? I_RRC : I_RRNC); break;
  case 0xD4: if (!b15) f_form(I_SLF); break;
  case 0xD5: f_form(b15 ? I_ASRF : I_LSRF); break;
  case 0xD6: f_form(b15 ? I_RLCF : I_RLNCF); break;
  case 0xD7: f_form(b15 ? I_RRCF : I_RRNCF); break;
  case 0xD8:
    I.op = I_DIV; I.sign = b15 ? 0 : 1; I.byte = !((w >> 6) & 1);  // byte flag reused: word (16/16) form
    I.wd = bits(w, 10, 7); I.ws = bits(w, 3, 0); I.k = bits(w, 5, 4);
    break;
  case 0xD9: I.op = I_DIVF; I.wb = bits(w, 14, 11); I.ws = bits(w, 3, 0); I.k = bits(w, 5, 4); break;
  case 0xDA: if (w == 0xDA4000) I.op = I_BREAK; break;
  case 0xDD: if (!b15) { I.wb = bits(w, 14, 11); I.wd = bits(w, 10, 7);
      if (bits(w, 6, 4) == 4) { I.op = I_SLK; I.k = bits(w, 3, 0); } else if (bits(w, 6, 4) == 0) { I.op = I_SLW; I.ws = bits(w, 3, 0); } } break;
  case 0xDE: { I.wb = bits(w, 14, 11); I.wd = bits(w, 10, 7);
      if (bits(w, 6, 4) == 4) { I.op = b15 ? I_ASRK : I_LSRK; I.k = bits(w, 3, 0); }
      else if (bits(w, 6, 4) == 0) { I.op = b15 ? I_ASRW : I_LSRW; I.ws = bits(w, 3, 0); } break; }
  case 0xDF: if (bits(w, 15, 11) == 0) { I.op = I_FBCL; I.wd = bits(w, 10, 7); ws_std(I, w); } break;
  case 0xE0: if (bits(w, 15, 11) == 0 && bits(w, 9, 7) == 0) { I.op = I_CP0; I.byte = (w >> 10) & 1; ws_std(I, w); } break;
  case 0xE1:
    I.wb = bits(w, 14, 11); I.byte = (w >> 10) & 1;
    if (bits(w, 6, 5) == 3) { I.op = b15 ? I_CPBK : I_CPK; I.k = (bits(w, 9, 7) << 5) | bits(w, 4, 0); }
    else if (bits(w, 9, 7) == 0) { I.op = b15 ? I_CPB : I_CP; ws_std(I, w); }
    break;
  case 0xE2: if (!b15) f_form(I_CP0F), I.to_wreg = false; break;
  case 0xE3: f_form(b15 ? I_CPBF : I_CPF); I.to_wreg = false; break;
  case 0xE6: case 0xE7: {
    I.wb = bits(w, 14, 11); I.byte = (w >> 10) & 1; I.ws = bits(w, 3, 0);
    bool skipform = bits(w, 9, 8) == 0 && bits(w, 7, 4) <= 1;
    if (top == 0xE6) { if (skipform) I.op = b15 ? I_CPSLT : I_CPSGT; else { I.op = b15 ? I_CPBLT : I_CPBGT; } }
    else { if (skipform) I.op = b15 ? I_CPSEQ : I_CPSNE; else { I.op = b15 ? I_CPBEQ : I_CPBNE; } }
    if (!skipform) I.target = pc + 2 + 2 * sbits(w, 9, 4);
    break; }
  case 0xE8: unary(b15 ? I_INC2 : I_INC); break;
  case 0xE9: unary(b15 ? I_DEC2 : I_DEC); break;
  case 0xEA: unary(b15 ? I_COM : I_NEG); break;
  case 0xEB: I.byte = b14; wd_std(I, w); I.op = b15 ? I_SETM : I_CLR; break;
  case 0xEC: f_form(b15 ? I_INC2F : I_INCF); break;
  case 0xED: f_form(b15 ? I_DEC2F : I_DECF); break;
  case 0xEE: f_form(b15 ? I_COMF : I_NEGF); break;
  case 0xEF: f_form(b15 ? I_SETMF : I_CLRF); break;
  case 0xF0: case 0xF1: case 0xF2: case 0xF3: {
    I.mm = bits(w, 17, 16); I.acc = b15; I.xx = bits(w, 13, 12); I.yy = bits(w, 11, 10);
    I.iiii = bits(w, 9, 6); I.jjjj = bits(w, 5, 2); I.aa = bits(w, 1, 0);
    if (!b14) I.op = (I.aa == 0) ? I_MACSQ : (I.aa == 1) ? I_MPYSQ : I_INVALID;
    // square forms carry no accumulator write-back
    else I.op = (I.aa == 3) ? I_ED : (I.aa == 2) ? I_EDAC : I_INVALID;
    break; }
  case 0xF8: I.op = I_PUSHF; I.f = lo16 & 0xFFFE; break;
  case 0xF9: I.op = I_POPF; I.f = lo16 & 0xFFFE; break;
  case 0xFA:
    if (bits(w, 15, 14) == 0) { I.op = I_LNK; I.k = bits(w, 13, 0); }
    else if (lo16 == 0x8000) I.op = I_ULNK;
    break;
  case 0xFB:
    if (bits(w, 15, 11) == 0) { I.op = I_SE; I.wd = bits(w, 10, 7); ws_std(I, w); }
    else if (bits(w, 15, 11) == 0x10) { I.op = I_ZE; I.wd = bits(w, 10, 7); ws_std(I, w); }
    break;
  case 0xFC: if (bits(w, 15, 14) == 0) { I.op = I_DISI; I.k = bits(w, 13, 0); } break;
  case 0xFD:
    if (bits(w, 15, 11) == 0 && bits(w, 6, 4) == 0) { I.op = I_EXCH; I.wd = bits(w, 10, 7); I.ws = bits(w, 3, 0); }
    else if (b15 && bits(w, 13, 4) == 0) { I.op = I_SWAP; I.byte = b14; I.ws = bits(w, 3, 0); }
    break;
  case 0xFE:
    if (lo16 == 0) I.op = I_RESET;
    else if ((lo16 & 0xFFFE) == 0x4000) { I.op = I_PWRSAV; I.k = w & 1; }
    else if (lo16 == 0x6000) I.op = I_CLRWDT;
    else if (lo16 == 0x8000) I.op = I_POPS;
    else if (lo16 == 0xA000) I.op = I_PUSHS;
    else if (bits(w, 15, 12) == 0xC) { I.op = I_MOVPAGK; I.cond = bits(w, 11, 10); I.k = bits(w, 9, 0); }
    else if (bits(w, 15, 12) == 0xD && bits(w, 9, 4) == 0) { I.op = I_MOVPAGW; I.cond = bits(w, 11, 10); I.ws = bits(w, 3, 0); }
    break;
  case 0xFF: I.op = I_NOP; break;
  default: break;
  }
  return I;
}

// ----------------------------------------------------------------------------- disassembler (Ghidra style)
static const char* condname(int c) {
  static const char* n1[16] = {"ov","c","z","n","le","lt","leu","","nov","nc","nz","nn","gt","ge","gtu",""};
  static const char* n2[4] = {"oa","ob","sa","sb"};
  return (c & 0x10) ? n2[c & 3] : n1[c & 15];
}
static std::string hx(uint32_t v, int width = 0) {
  char b[32]; if (width) snprintf(b, sizeof b, "0x%0*x", width, v); else snprintf(b, sizeof b, "0x%x", v); return b;
}
static std::string reg(int n) { return "W" + std::to_string(n); }
static std::string ea(int mode, int r, int breg) {
  std::string R = reg(r);
  switch (mode) {
  case 0: return R; case 1: return "[" + R + "]"; case 2: return "[" + R + "--]"; case 3: return "[" + R + "++]";
  case 4: return "[--" + R + "]"; case 5: return "[++" + R + "]"; default: return "[" + R + "+" + reg(breg) + "]";
  }
}
static std::string fname(uint32_t f) { return hx(f, 4); }
static std::string sfx(const Insn& I) { return I.byte ? ".b" : ".w"; }

std::string disasm(const Insn& I, uint32_t pc) {
  auto src = [&]() { return I.lit ? "#" + hx(I.k) : ea(I.smode, I.ws, I.sbreg); };
  auto dst = [&]() { return ea(I.dmode, I.wd, I.dbreg); };
  auto fwr = [&](const char* m) { return std::string(m) + sfx(I) + " " + fname(I.f) + (I.to_wreg ? ",wreg" : ""); };
  static const char* wm[8][2] = {{"W4","W5"},{"W4","W6"},{"W4","W7"},{"",""},{"W5","W6"},{"W5","W7"},{"W6","W7"},{"",""}};
  auto pref = [&]() {
    std::string s;
    static const int inc[8] = {0, 2, 4, 6, 0, -6, -4, -2};
    auto one = [&](int code, int base, const char* dreg) {
      if ((code & 7) == 4) { if (code == 0xC) return std::string(",[") + reg(base + 1) + "+W12]," + std::string(",") + dreg; return std::string(); }
      int r = base + (code >> 3);
      std::string a = "[" + reg(r) + "]";
      int d = inc[code & 7];
      if (d > 0) a += "+=" + std::to_string(d); else if (d < 0) a += "-=" + std::to_string(-d);
      return "," + a + ",," + dreg;
    };
    const char* xd[4] = {"W4","W5","W6","W7"};
    std::string x = one(I.iiii, 8, xd[I.xx]);
    std::string y = one(I.jjjj, 10, xd[I.yy]);
    if (I.iiii == 0xC) x = ",[W9+W12],," + std::string(xd[I.xx]);
    if (I.jjjj == 0xC) y = ",[W11+W12],," + std::string(xd[I.yy]);
    s = x + y;
    bool sq = I.op == I_MACSQ || I.op == I_MPYSQ || I.op == I_ED || I.op == I_EDAC;
    if (!sq) { if (I.aa == 0) s += ",W13"; else if (I.aa == 1) s += ",[W13]+=2"; }
    return s;
  };
  const char* A = I.acc ? "ACCB" : "ACCA";
  switch (I.op) {
  case I_NOP: return "nop";
  case I_CALL: return "call " + hx(I.target, 6);
  case I_GOTO: return "goto " + hx(I.target, 6);
  case I_RCALL: return "rcall " + hx(I.target, 6);
  case I_CALLW: return "call " + reg(I.ws);
  case I_RCALLW: return "rcall " + reg(I.ws);
  case I_GOTOW: return "goto " + reg(I.ws);
  case I_BRAW: return "bra " + reg(I.ws);
  case I_CALLL: return "call.l " + reg(I.ws);
  case I_GOTOL: return "goto.l " + reg(I.ws);
  case I_BRA: return "bra " + hx(I.target, 6);
  case I_BRACC: return std::string("bra ") + condname(I.cond) + "," + hx(I.target, 6);
  case I_RETURN: return "return";
  case I_RETFIE: return "retfie";
  case I_RETLW: return "retlw" + sfx(I) + " #" + hx(I.k) + "," + reg(I.wd);
  case I_REPEAT: return "repeat #" + hx(I.k);
  case I_REPEATW: return "repeat " + reg(I.ws);
  case I_DO: return "do #" + hx(I.k) + "," + hx(I.target, 6);
  case I_DOW: return "do " + reg(I.ws) + "," + hx(I.target, 6);
  case I_RESET: return "reset";
  case I_BREAK: return "break";
  case I_PWRSAV: return "pwrsav #" + std::to_string(I.k);
  case I_CLRWDT: return "clrwdt";
  case I_DISI: return "disi #" + hx(I.k);
  case I_ADD3: return "add" + sfx(I) + " " + reg(I.wb) + "," + src() + "," + dst();
  case I_ADDC3: return "addc" + sfx(I) + " " + reg(I.wb) + "," + src() + "," + dst();
  case I_SUB3: return "sub" + sfx(I) + " " + reg(I.wb) + "," + src() + "," + dst();
  case I_SUBB3: return "subb" + sfx(I) + " " + reg(I.wb) + "," + src() + "," + dst();
  case I_SUBR3: return "subr" + sfx(I) + " " + reg(I.wb) + "," + src() + "," + dst();
  case I_SUBBR3: return "subbr" + sfx(I) + " " + reg(I.wb) + "," + src() + "," + dst();
  case I_AND3: return "and" + sfx(I) + " " + reg(I.wb) + "," + src() + "," + dst();
  case I_IOR3: return "ior" + sfx(I) + " " + reg(I.wb) + "," + src() + "," + dst();
  case I_XOR3: return "xor" + sfx(I) + " " + reg(I.wb) + "," + src() + "," + dst();
  case I_ADDK: return "add" + sfx(I) + " #" + hx(I.k) + "," + reg(I.wd);
  case I_ADDCK: return "addc" + sfx(I) + " #" + hx(I.k) + "," + reg(I.wd);
  case I_SUBK: return "sub" + sfx(I) + " #" + hx(I.k) + "," + reg(I.wd);
  case I_SUBBK: return "subb" + sfx(I) + " #" + hx(I.k) + "," + reg(I.wd);
  case I_ANDK: return "and" + sfx(I) + " #" + hx(I.k) + "," + reg(I.wd);
  case I_IORK: return "ior" + sfx(I) + " #" + hx(I.k) + "," + reg(I.wd);
  case I_XORK: return "xor" + sfx(I) + " #" + hx(I.k) + "," + reg(I.wd);
  case I_ADDF: return fwr("add"); case I_ADDCF: return fwr("addc"); case I_SUBF: return fwr("sub");
  case I_SUBBF: return fwr("subb"); case I_SUBRF: return fwr("subr"); case I_SUBBRF: return fwr("subbr");
  case I_ANDF: return fwr("and"); case I_IORF: return fwr("ior"); case I_XORF: return fwr("xor");
  case I_SLF: return fwr("sl"); case I_LSRF: return fwr("lsr"); case I_ASRF: return fwr("asr");
  case I_RLCF: return fwr("rlc"); case I_RLNCF: return fwr("rlnc"); case I_RRCF: return fwr("rrc"); case I_RRNCF: return fwr("rrnc");
  case I_INCF: return fwr("inc"); case I_INC2F: return fwr("inc2"); case I_DECF: return fwr("dec"); case I_DEC2F: return fwr("dec2");
  case I_NEGF: return fwr("neg"); case I_COMF: return fwr("com");
  case I_CLRF: return I.to_wreg ? "clr" + sfx(I) + " wreg" : "clr" + sfx(I) + " " + fname(I.f);
  case I_SETMF: return I.to_wreg ? "setm" + sfx(I) + " wreg" : "setm" + sfx(I) + " " + fname(I.f);
  case I_MOVF: return fwr("mov");
  case I_MOVWF: return "mov" + sfx(I) + " wreg," + fname(I.f);
  case I_MULF: return "mul" + sfx(I) + " " + fname(I.f);
  case I_CPF: return "cp" + sfx(I) + " " + fname(I.f);
  case I_CPBF: return "cpb" + sfx(I) + " " + fname(I.f);
  case I_CP0F: return "cp0" + sfx(I) + " " + fname(I.f);
  case I_SL: return "sl" + sfx(I) + " " + src() + "," + dst();
  case I_LSR: return "lsr" + sfx(I) + " " + src() + "," + dst();
  case I_ASR: return "asr" + sfx(I) + " " + src() + "," + dst();
  case I_RLC: return "rlc" + sfx(I) + " " + src() + "," + dst();
  case I_RLNC: return "rlnc" + sfx(I) + " " + src() + "," + dst();
  case I_RRC: return "rrc" + sfx(I) + " " + src() + "," + dst();
  case I_RRNC: return "rrnc" + sfx(I) + " " + src() + "," + dst();
  case I_INC: return "inc" + sfx(I) + " " + src() + "," + dst();
  case I_INC2: return "inc2" + sfx(I) + " " + src() + "," + dst();
  case I_DEC: return "dec" + sfx(I) + " " + src() + "," + dst();
  case I_DEC2: return "dec2" + sfx(I) + " " + src() + "," + dst();
  case I_NEG: return "neg" + sfx(I) + " " + src() + "," + dst();
  case I_COM: return "com" + sfx(I) + " " + src() + "," + dst();
  case I_CLR: return "clr" + sfx(I) + " " + dst();
  case I_SETM: return "setm" + sfx(I) + " " + dst();
  case I_SLK: return "sl " + reg(I.wb) + ",#" + hx(I.k) + "," + reg(I.wd);
  case I_LSRK: return "lsr.w " + reg(I.wb) + ",#" + hx(I.k) + "," + reg(I.wd);
  case I_ASRK: return "asr.w " + reg(I.wb) + ",#" + hx(I.k) + "," + reg(I.wd);
  case I_SLW: return "sl " + reg(I.wb) + "," + reg(I.ws) + "," + reg(I.wd);
  case I_LSRW: return "lsr.w " + reg(I.wb) + "," + reg(I.ws) + "," + reg(I.wd);
  case I_ASRW: return "asr.w " + reg(I.wb) + "," + reg(I.ws) + "," + reg(I.wd);
  case I_MOVK16: return "mov.w #" + hx(I.k) + "," + reg(I.wd);
  case I_MOVK8: return "mov.b #" + hx(I.k) + "," + reg(I.wd);
  case I_MOVFW: return "mov.w " + fname(I.f) + "," + reg(I.wd);
  case I_MOVWFF: return "mov.w " + reg(I.ws) + "," + fname(I.f);
  case I_MOV: return "mov" + sfx(I) + " " + ea(I.smode, I.ws, I.sbreg) + "," + ea(I.dmode, I.wd, I.dbreg);
  case I_MOVSLIT: { char b[64]; snprintf(b, sizeof b, "[%s+%s]", reg(I.ws).c_str(), I.k < 0 ? ("-" + hx(-I.k)).c_str() : hx(I.k).c_str()); return "mov" + sfx(I) + " " + b + "," + reg(I.wd); }
  case I_MOVDSLIT: { char b[64]; snprintf(b, sizeof b, "[%s+%s]", reg(I.wd).c_str(), I.k < 0 ? ("-" + hx(-I.k)).c_str() : hx(I.k).c_str()); return "mov" + sfx(I) + " " + reg(I.ws) + "," + b; }
  case I_MOVD_LD: return "mov.d " + ea(I.smode, I.ws, 0) + "," + reg(I.wd);
  case I_MOVD_ST: return "mov.d " + reg(I.ws) + "," + ea(I.dmode, I.wd, 0);
  case I_PUSHF: return "push " + fname(I.f);
  case I_POPF: return "pop " + fname(I.f);
  case I_LNK: return "lnk #" + hx(I.k);
  case I_ULNK: return "ulnk";
  case I_EXCH: return "exch " + reg(I.ws) + "," + reg(I.wd);
  case I_SWAP: return "swap" + sfx(I) + " " + reg(I.ws);
  case I_SE: return "se " + ea(I.smode, I.ws, 0) + "," + reg(I.wd);
  case I_ZE: return "ze " + ea(I.smode, I.ws, 0) + "," + reg(I.wd);
  case I_MOVPAGK: { const char* p[4] = {"DSRPAG","DSWPAG","TBLPAG","?"}; return "movpag #" + hx(I.k) + "," + p[I.cond]; }
  case I_MOVPAGW: { const char* p[4] = {"DSRPAG","DSWPAG","TBLPAG","?"}; return "movpag " + reg(I.ws) + "," + p[I.cond]; }
  case I_PUSHS: return "push.s"; case I_POPS: return "pop.s";
  case I_CP: return "cp" + sfx(I) + " " + reg(I.wb) + "," + ea(I.smode, I.ws, 0);
  case I_CPB: return "cpb" + sfx(I) + " " + reg(I.wb) + "," + ea(I.smode, I.ws, 0);
  case I_CPK: return "cp" + sfx(I) + " " + reg(I.wb) + ",#" + hx(I.k);
  case I_CPBK: return "cpb" + sfx(I) + " " + reg(I.wb) + ",#" + hx(I.k);
  case I_CP0: return "cp0" + sfx(I) + " " + ea(I.smode, I.ws, 0);
  case I_CPSEQ: return "cpseq" + sfx(I) + " " + reg(I.wb) + "," + reg(I.ws);
  case I_CPSNE: return "cpsne" + sfx(I) + " " + reg(I.wb) + "," + reg(I.ws);
  case I_CPSGT: return "cpsgt" + sfx(I) + " " + reg(I.wb) + "," + reg(I.ws);
  case I_CPSLT: return "cpslt" + sfx(I) + " " + reg(I.wb) + "," + reg(I.ws);
  case I_CPBEQ: return "cpbeq" + sfx(I) + " " + reg(I.wb) + "," + reg(I.ws) + "," + hx(I.target, 6);
  case I_CPBNE: return "cpbne" + sfx(I) + " " + reg(I.wb) + "," + reg(I.ws) + "," + hx(I.target, 6);
  case I_CPBGT: return "cpbgt" + sfx(I) + " " + reg(I.wb) + "," + reg(I.ws) + "," + hx(I.target, 6);
  case I_CPBLT: return "cpblt" + sfx(I) + " " + reg(I.wb) + "," + reg(I.ws) + "," + hx(I.target, 6);
  case I_BSET: return "bset" + sfx(I) + " " + ea(I.smode, I.ws, 0) + ",#" + hx(I.bit);
  case I_BCLR: return "bclr" + sfx(I) + " " + ea(I.smode, I.ws, 0) + ",#" + hx(I.bit);
  case I_BTG: return "btg" + sfx(I) + " " + ea(I.smode, I.ws, 0) + ",#" + hx(I.bit);
  case I_BTSS: return "btss.w " + ea(I.smode, I.ws, 0) + ",#" + hx(I.bit);
  case I_BTSC: return "btsc.w " + ea(I.smode, I.ws, 0) + ",#" + hx(I.bit);
  case I_BTST: return std::string("btst.") + (I.cond ? "z " : "c ") + ea(I.smode, I.ws, 0) + ",#" + hx(I.bit);
  case I_BTSTS: return std::string("btsts.") + (I.cond ? "z " : "c ") + ea(I.smode, I.ws, 0) + ",#" + hx(I.bit);
  case I_BTSTW: return std::string("btst.") + (I.cond ? "z " : "c ") + ea(I.smode, I.ws, 0) + "," + reg(I.wb);
  case I_BSW: return std::string("bsw.") + (I.cond ? "z " : "c ") + ea(I.smode, I.ws, 0) + "," + reg(I.wb);
  case I_BSETF: return "bset.w " + fname(I.f) + ",#" + hx(I.bit);
  case I_BCLRF: return "bclr.w " + fname(I.f) + ",#" + hx(I.bit);
  case I_BTGF: return "btg" + std::string(I.byte ? ".b " : ".w ") + fname(I.f) + ",#" + hx(I.bit);
  case I_BTSTF: return "btst.w " + fname(I.f) + ",#" + hx(I.bit);
  case I_BTSTSF: return "btsts" + std::string(I.byte ? ".b " : ".w ") + fname(I.f) + ",#" + hx(I.bit);
  case I_BTSSF: return "btss" + std::string(I.byte ? ".b " : ".w ") + fname(I.f) + ",#" + hx(I.bit);
  case I_BTSCF: return "btsc" + std::string(I.byte ? ".b " : ".w ") + fname(I.f) + ",#" + hx(I.bit);
  case I_FBCL: return "fbcl " + ea(I.smode, I.ws, 0) + "," + reg(I.wd);
  case I_FF1L: return "ff1l " + ea(I.smode, I.ws, 0) + "," + reg(I.wd);
  case I_FF1R: return "ff1r " + ea(I.smode, I.ws, 0) + "," + reg(I.wd);
  case I_MUL: case I_MULW: case I_MULACC: {
    static const char* s[4] = {"uu","su","us","ss"};
    std::string m = std::string(I.op == I_MULW ? "mulw." : "mul.") + s[I.sign];
    std::string d = I.op == I_MULACC ? std::string(A) : reg(I.wd);
    return m + " " + reg(I.wb) + "," + (I.lit ? "#" + hx(I.k) : ea(I.smode, I.ws, 0)) + "," + d; }
  case I_DIV: return std::string("div.") + (I.sign ? "s" : "u") + (I.byte ? "w " : "d ") + reg(I.wd) + "," + reg(I.ws);
  case I_DIVF: return "divf " + reg(I.wb) + "," + reg(I.ws);
  case I_TBLRDL: return "tblrdl" + sfx(I) + " " + ea(I.smode, I.ws, 0) + "," + ea(I.dmode, I.wd, 0);
  case I_TBLRDH: return "tblrdh" + sfx(I) + " " + ea(I.smode, I.ws, 0) + "," + ea(I.dmode, I.wd, 0);
  case I_TBLWTL: return "tblwtl" + sfx(I) + " " + ea(I.smode, I.ws, 0) + "," + ea(I.dmode, I.wd, 0);
  case I_TBLWTH: return "tblwth" + sfx(I) + " " + ea(I.smode, I.ws, 0) + "," + ea(I.dmode, I.wd, 0);
  case I_MAC: case I_MSC: case I_MPY: case I_MPYN: {
    const char* mn = I.op == I_MAC ? "mac" : I.op == I_MSC ? "msc" : I.op == I_MPY ? "mpy" : "mpy.n";
    return std::string(mn) + " " + wm[I.mm][0] + "*" + wm[I.mm][1] + "," + A + pref(); }
  case I_MACSQ: case I_MPYSQ: { const char* r[4] = {"W4","W5","W6","W7"};
    return std::string(I.op == I_MACSQ ? "mac " : "mpy ") + r[I.mm] + "*" + r[I.mm] + "," + A + pref(); }
  case I_ED: case I_EDAC: { const char* r[4] = {"W4","W5","W6","W7"};
    return std::string(I.op == I_ED ? "ed " : "edac ") + r[I.mm] + "*" + r[I.mm] + "," + A + pref(); }
  case I_CLRAC: return std::string("clr ") + A + pref();
  case I_MOVSAC: return std::string("movsac ") + A + pref();
  case I_SFTACK: return std::string("sftac ") + A + ",#" + (I.shift < 0 ? "-" + hx(-I.shift) : hx(I.shift));
  case I_SFTACW: return std::string("sftac ") + A + "," + reg(I.wb);
  case I_ADDAB: return std::string("add ") + A;
  case I_SUBAB: return std::string("sub ") + A;
  case I_NEGAB: return std::string("neg ") + A;
  case I_LAC: case I_ADDACW: { std::string r = I.shift ? ",#" + (I.shift < 0 ? "-" + hx(-I.shift) : hx(I.shift)) : "";
    return std::string(I.op == I_LAC ? "lac " : "add ") + ea(I.smode, I.ws, I.sbreg) + r + "," + A; }
  case I_SAC: case I_SACR: { std::string r = I.shift ? ",#" + (I.shift < 0 ? "-" + hx(-I.shift) : hx(I.shift)) : "";
    return std::string(I.op == I_SAC ? "sac " : "sac.r ") + A + r + "," + ea(I.smode, I.ws, I.sbreg); }
  default: return "??";
  }
}

}  // namespace dspic
