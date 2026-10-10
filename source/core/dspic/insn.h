// dsPIC33E instruction decoding (shared by executor and disassembler)
#pragma once
#include <cstdint>
#include <string>

namespace dspic {

enum Op : uint8_t {
  I_INVALID = 0, I_NOP,
  // control
  I_CALL, I_CALLW, I_CALLL, I_RCALL, I_RCALLW, I_GOTO, I_GOTOW, I_GOTOL, I_BRA, I_BRAW, I_BRACC,
  I_RETURN, I_RETFIE, I_RETLW, I_REPEAT, I_REPEATW, I_DO, I_DOW, I_RESET, I_BREAK, I_PWRSAV, I_CLRWDT, I_DISI,
  // 3-operand ALU: Wb, Ws|#lit5, Wd
  I_ADD3, I_ADDC3, I_SUB3, I_SUBB3, I_SUBR3, I_SUBBR3, I_AND3, I_IOR3, I_XOR3,
  // literal to Wn
  I_ADDK, I_ADDCK, I_SUBK, I_SUBBK, I_ANDK, I_IORK, I_XORK,
  // file register forms (f {,WREG})
  I_ADDF, I_ADDCF, I_SUBF, I_SUBBF, I_SUBRF, I_SUBBRF, I_ANDF, I_IORF, I_XORF,
  I_SLF, I_LSRF, I_ASRF, I_RLCF, I_RLNCF, I_RRCF, I_RRNCF,
  I_INCF, I_INC2F, I_DECF, I_DEC2F, I_NEGF, I_COMF, I_CLRF, I_SETMF, I_MOVF, I_MOVWF, I_MULF,
  I_CPF, I_CPBF, I_CP0F,
  // Ws,Wd unary
  I_SL, I_LSR, I_ASR, I_RLC, I_RLNC, I_RRC, I_RRNC, I_INC, I_INC2, I_DEC, I_DEC2, I_NEG, I_COM, I_CLR, I_SETM,
  // multi-bit shifts
  I_SLK, I_LSRK, I_ASRK, I_SLW, I_LSRW, I_ASRW,
  // moves
  I_MOVK16, I_MOVK8, I_MOVFW, I_MOVWFF /*Wn->f*/, I_MOV, I_MOVSLIT /*[Ws+lit],Wd*/, I_MOVDSLIT /*Wn,[Wd+lit]*/,
  I_MOVD_LD, I_MOVD_ST, I_PUSHF, I_POPF, I_LNK, I_ULNK, I_EXCH, I_SWAP, I_SE, I_ZE, I_MOVPAGK, I_MOVPAGW,
  I_PUSHS, I_POPS,
  // compares
  I_CP, I_CPB, I_CPK, I_CPBK, I_CP0,
  I_CPSEQ, I_CPSNE, I_CPSGT, I_CPSLT, I_CPBEQ, I_CPBNE, I_CPBGT, I_CPBLT,
  // bit ops
  I_BSET, I_BCLR, I_BTG, I_BTST, I_BTSTS, I_BTSTW, I_BTSS, I_BTSC, I_BSW,
  I_BSETF, I_BCLRF, I_BTGF, I_BTSTF, I_BTSTSF, I_BTSSF, I_BTSCF,
  I_FBCL, I_FF1L, I_FF1R,
  // multiply/divide
  I_MUL, I_MULW, I_MULACC, I_DIV, I_DIVF,
  // table
  I_TBLRDL, I_TBLRDH, I_TBLWTL, I_TBLWTH,
  // DSP
  I_MAC, I_MSC, I_MPY, I_MPYN, I_CLRAC, I_MOVSAC, I_MACSQ, I_MPYSQ, I_ED, I_EDAC,
  I_SFTACK, I_SFTACW, I_ADDACW, I_LAC, I_ADDAB, I_NEGAB, I_SUBAB, I_SAC, I_SACR,
  I_HOOK,      // emulator-internal: fast-forward hook, then the original instruction
  I_COUNT
};

struct Insn {
  Op op = I_INVALID;
  uint8_t words = 1;
  bool byte = false;
  uint8_t wb = 0, ws = 0, wd = 0;     // register numbers
  uint8_t smode = 0, dmode = 0;       // addressing modes (0..7)
  uint8_t sbreg = 0, dbreg = 0;       // offset registers for [Ws+Wb] / [Wd+Wb]
  bool lit = false;                   // operand 2 is literal
  int32_t k = 0;                      // literal / offset / displacement
  uint32_t target = 0;                // branch/call target
  uint16_t f = 0;                     // file register address
  bool to_wreg = false;               // f-form destination WREG
  uint8_t bit = 0;
  uint8_t cond = 0;
  uint8_t acc = 0;                    // 0=A 1=B
  uint8_t sign = 0;                   // mul: bit0 = Wb signed, bit1 = Ws signed
  // MAC class
  uint8_t mm = 0, xx = 0, yy = 0, iiii = 4, jjjj = 4, aa = 2;
  int8_t shift = 0;                   // r4 / k6
  uint8_t hook = 0;                   // 1-based index into CPU::hooks (fast-forward handlers)
};

Insn decode(uint32_t w, uint32_t w2, uint32_t pc);
std::string disasm(const Insn& I, uint32_t pc);

}  // namespace dspic
