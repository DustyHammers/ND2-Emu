// dsPIC33E instruction decoding (shared by executor and disassembler)
#pragma once
#include <cstdint>
#include <string>

namespace dspic {

// Every operation, in enum order (the threaded dispatcher builds its label table from this list).
#define DSPIC_OPS(X) \
  X(I_INVALID) X(I_NOP) \
  X(I_CALL) X(I_CALLW) X(I_CALLL) X(I_RCALL) X(I_RCALLW) X(I_GOTO) X(I_GOTOW) X(I_GOTOL) X(I_BRA) X(I_BRAW) X(I_BRACC) \
  X(I_RETURN) X(I_RETFIE) X(I_RETLW) X(I_REPEAT) X(I_REPEATW) X(I_DO) X(I_DOW) X(I_RESET) X(I_BREAK) X(I_PWRSAV) X(I_CLRWDT) X(I_DISI) \
  X(I_ADD3) X(I_ADDC3) X(I_SUB3) X(I_SUBB3) X(I_SUBR3) X(I_SUBBR3) X(I_AND3) X(I_IOR3) X(I_XOR3) \
  X(I_ADDK) X(I_ADDCK) X(I_SUBK) X(I_SUBBK) X(I_ANDK) X(I_IORK) X(I_XORK) \
  X(I_ADDF) X(I_ADDCF) X(I_SUBF) X(I_SUBBF) X(I_SUBRF) X(I_SUBBRF) X(I_ANDF) X(I_IORF) X(I_XORF) \
  X(I_SLF) X(I_LSRF) X(I_ASRF) X(I_RLCF) X(I_RLNCF) X(I_RRCF) X(I_RRNCF) \
  X(I_INCF) X(I_INC2F) X(I_DECF) X(I_DEC2F) X(I_NEGF) X(I_COMF) X(I_CLRF) X(I_SETMF) X(I_MOVF) X(I_MOVWF) X(I_MULF) \
  X(I_CPF) X(I_CPBF) X(I_CP0F) \
  X(I_SL) X(I_LSR) X(I_ASR) X(I_RLC) X(I_RLNC) X(I_RRC) X(I_RRNC) X(I_INC) X(I_INC2) X(I_DEC) X(I_DEC2) X(I_NEG) X(I_COM) X(I_CLR) X(I_SETM) \
  X(I_SLK) X(I_LSRK) X(I_ASRK) X(I_SLW) X(I_LSRW) X(I_ASRW) \
  X(I_MOVK16) X(I_MOVK8) X(I_MOVFW) X(I_MOVWFF) X(I_MOV) X(I_MOVSLIT) X(I_MOVDSLIT) \
  X(I_MOVD_LD) X(I_MOVD_ST) X(I_PUSHF) X(I_POPF) X(I_LNK) X(I_ULNK) X(I_EXCH) X(I_SWAP) X(I_SE) X(I_ZE) X(I_MOVPAGK) X(I_MOVPAGW) \
  X(I_PUSHS) X(I_POPS) \
  X(I_CP) X(I_CPB) X(I_CPK) X(I_CPBK) X(I_CP0) \
  X(I_CPSEQ) X(I_CPSNE) X(I_CPSGT) X(I_CPSLT) X(I_CPBEQ) X(I_CPBNE) X(I_CPBGT) X(I_CPBLT) \
  X(I_BSET) X(I_BCLR) X(I_BTG) X(I_BTST) X(I_BTSTS) X(I_BTSTW) X(I_BTSS) X(I_BTSC) X(I_BSW) \
  X(I_BSETF) X(I_BCLRF) X(I_BTGF) X(I_BTSTF) X(I_BTSTSF) X(I_BTSSF) X(I_BTSCF) \
  X(I_FBCL) X(I_FF1L) X(I_FF1R) \
  X(I_MUL) X(I_MULW) X(I_MULACC) X(I_DIV) X(I_DIVF) \
  X(I_TBLRDL) X(I_TBLRDH) X(I_TBLWTL) X(I_TBLWTH) \
  X(I_MAC) X(I_MSC) X(I_MPY) X(I_MPYN) X(I_CLRAC) X(I_MOVSAC) X(I_MACSQ) X(I_MPYSQ) X(I_ED) X(I_EDAC) \
  X(I_SFTACK) X(I_SFTACW) X(I_ADDACW) X(I_LAC) X(I_ADDAB) X(I_NEGAB) X(I_SUBAB) X(I_SAC) X(I_SACR) \
  X(I_HOOK) \
  /* end */

enum Op : uint8_t {
#define DSPIC_OP_ENUM(n) n,
  DSPIC_OPS(DSPIC_OP_ENUM)
#undef DSPIC_OP_ENUM
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
