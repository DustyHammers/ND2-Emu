#include "board.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace nd2 {
using namespace dspic;

enum : uint16_t {
  U2MODE = 0x230, U2STA = 0x232, U2TXREG = 0x234, U2RXREG = 0x236,
  DCICON1 = 0x280, NVMCON = 0x728, NVMADRL = 0x72A, NVMADRH = 0x72C, OSCCON = 0x742,
  IFS0 = 0x800, IEC0 = 0x820, IPC0 = 0x840,
  AD1CON1 = 0x320, AD1CON2 = 0x322, AD1CON3 = 0x324, AD1CSSH = 0x32E, AD1CSSL = 0x330, AD1CON4 = 0x332,
  DMA0STAL = 0xB04, DMA0STBL = 0xB08, DMA0CNT = 0xB0E, DMAPPS = 0xBF4,
  DMA0CON = 0xB00, DMA1CON = 0xB10, DMA1STAL = 0xB14, DMA1CNT = 0xB1E,
  TRISB = 0xE10,
};
enum { IRQ_T1 = 3, IRQ_DMA0 = 4, IRQ_T2 = 7, IRQ_T3 = 8, IRQ_DMA1 = 14, IRQ_CN = 19, IRQ_T4 = 27, IRQ_T5 = 28,
       IRQ_U2RX = 30, IRQ_U2TX = 31, IRQ_T6 = 47, IRQ_T7 = 48 };

Board::Board() {
  memset(sfr, 0, sizeof sfr);
  for (auto& p : port_in) p = 0xFFFF;
  timers = { {0x100, 0x102, 0x104, IRQ_T1}, {0x106, 0x10C, 0x110, IRQ_T2}, {0x10A, 0x10E, 0x112, IRQ_T3},
             {0x114, 0x11A, 0x11E, IRQ_T4}, {0x118, 0x11C, 0x120, IRQ_T5}, {0x122, 0x128, 0x12C, IRQ_T6},
             {0x126, 0x12A, 0x12E, IRQ_T7} };
  for (const auto& t : timers) sfr_moves[t.a_tmr >> 1] = true;
  for (uint16_t a = IFS0; a < IEC0; a += 2) sfr_moves[a >> 1] = true;
  for (auto& v : ivt) v = 0x4010;   // default handler slot
  ivt[IRQ_T1] = 0x4030; ivt[IRQ_DMA0] = 0x4054; ivt[IRQ_T3] = 0x4038; ivt[IRQ_CN] = 0x406C; ivt[IRQ_T4] = 0x403C;
  ivt[IRQ_T5] = 0x4040; ivt[IRQ_U2RX] = 0x4070; ivt[IRQ_T6] = 0x4044; ivt[IRQ_T7] = 0x4048;
  cpu.bus = this;
  cpu.prog.assign(0x55800 / 2, 0xFFFFFF);
}

void Board::load_app(const std::vector<uint8_t>& b) {
  // instruction k sits at PC 0x4000 + 2k; prog index = PC/2
  for (size_t i = 0; i + 3 < b.size(); i += 4) {
    uint32_t pc = 0x4000 + (uint32_t)(i / 2);
    if ((pc >> 1) < cpu.prog.size()) cpu.prog[pc >> 1] = b[i] | (b[i + 1] << 8) | (b[i + 2] << 16);
  }
}

// ---------------------------------------------------------------- busy-wait fast-forward (OS v3.00)
// Skips whole iterations of pure counting/polling loops, never across a peripheral event, so results are
// identical to interpreting them (same cycle counts the interpreter charges, same register/flag/RAM state).
namespace {
uint64_t g_ff_calls = 0, g_ff_skipped = 0;
struct CountLoop { uint16_t limit_reg; };
// LED PWM delay in the T1 ISR: { nop; inc W0; sub Wn,W0,[W15]; bra z,exit; nop; inc W0; sub Wn,W0,[W15]; bra nz,top }
void ff_count_loop(CPU& c, void* ctx) {
  int lr = static_cast<CountLoop*>(ctx)->limit_reg;
  uint16_t w0 = c.W(0), lim = c.W(lr);
  uint32_t d = (uint16_t)(lim - w0); if (d == 0) d = 0x10000;
  uint64_t k = (d - 1) / 2;                       // whole iterations that cannot exit
  uint64_t kb = c.ff_budget() / 11;               // 11 cycles, 8 instructions per iteration
  if (kb < k) k = kb;
  g_ff_calls++;
  if (k < 2) return;
  g_ff_skipped += 8 * k;
  w0 = (uint16_t)(w0 + 2 * k);
  c.setW(0, w0);
  c.flags_sub(lim, w0);
  c.wr16(c.W(15), (uint16_t)(lim - w0));
  c.cycles += 11 * k; c.instret += 8 * k;
}
// ms delay: { mov 0x4A3C,W0; sub W1,W0,W0; bra nn,top } waits for the T4 tick counter
void ff_tick_wait(CPU& c, void*) {
  uint16_t tick = c.ram[0x4A3C] | (c.ram[0x4A3D] << 8);
  uint16_t r = (uint16_t)(c.W(1) - tick);
  if (r & 0x8000) return;                         // exits this time round
  uint64_t k = c.ff_budget() / 6;                 // 6 cycles, 3 instructions per iteration
  g_ff_calls++;
  if (k < 2) return;
  g_ff_skipped += 3 * k;
  c.setW(0, r); c.flags_sub(c.W(1), tick);
  c.cycles += 6 * k; c.instret += 3 * k;
}
CountLoop kLoopW2{2}, kLoopW1{1};
bool words_match(const CPU& c, uint32_t pc, std::initializer_list<uint32_t> w) {
  for (uint32_t v : w) { if (c.progword(pc) != v) return false; pc += 2; }
  return true;
}
}  // namespace
uint64_t ff_stat_calls() { return g_ff_calls; }
uint64_t ff_stat_skipped() { return g_ff_skipped; }

void Board::install_fast_forward() {
  int mask = getenv("ND2_FF") ? atoi(getenv("ND2_FF")) : 7;
  if (words_match(cpu, 0x2D922, {0x000000, 0xE80000, 0x510F80, 0x32FFFA, 0x000000, 0xE80000, 0x510F80, 0x3AFFF8}))
    if (mask & 1) cpu.add_hook(0x2D922, ff_count_loop, &kLoopW2);
  if (words_match(cpu, 0x2D936, {0x000000, 0xE80000, 0x508F80, 0x32FFDE, 0x000000, 0xE80000, 0x508F80, 0x3AFFF8}))
    if (mask & 2) cpu.add_hook(0x2D936, ff_count_loop, &kLoopW1);
  if (words_match(cpu, 0x3477A, {0x8251E0, 0x508000, 0x3BFFFD}))
    if (mask & 4) cpu.add_hook(0x3477A, ff_tick_wait, nullptr);
}

// ---------------------------------------------------------------- channel outputs (stems)
namespace {
// the OS's final mix (L: 0x1A9EE-0x1AA22, R: 0x1AA24-0x1AA5C) and the T5 copy of the frame into the output ring
const uint32_t kMixWords[] = {0xC7184A, 0xB83738, 0xC80050, 0xC4184A, 0xB837B8, 0xC88050, 0xC4984A, 0xCB0000, 0xB837B8, 0xC88050,
  0xC4984A, 0xCB0000, 0xB837B8, 0xC88050, 0xC4984A, 0xCB0000, 0xB837B8, 0xC88050, 0xC4984A, 0xCB0000, 0xB837B8, 0xC88050, 0xC48D02,
  0xCB0000, 0xC8007D, 0xCC0038, 0x781C17, 0x21E8E8, 0x2DE88A, 0xC7184A, 0xB83738, 0xC80050, 0xC4184A, 0xB837B8, 0xC88050, 0xC4984A,
  0xCB0000, 0xB837B8, 0xC88050, 0xC4984A, 0xCB0000, 0xB837B8, 0xC88050, 0xC4984A, 0xCB0000, 0xB837B8, 0xC88050, 0xC4984A, 0xCB0000,
  0xB837B8, 0xC88050, 0xC49092, 0xCB0000, 0xC8007D, 0xCC0038, 0x781C17};
// hook pc, accumulator holding the term (0 = ACCA, 1 = ACCB), stem index (L1 = 0, R1 = 1, ...)
struct StemTap { uint32_t pc; int acc; int stem; };
const StemTap kTaps[] = {
  {0x1A9F6, 0, 0}, {0x1A9FC, 1, 2}, {0x1AA04, 1, 4}, {0x1AA0C, 1, 6}, {0x1AA14, 1, 8}, {0x1AA1C, 1, 10},
  {0x1AA30, 0, 1}, {0x1AA36, 1, 3}, {0x1AA3E, 1, 5}, {0x1AA46, 1, 7}, {0x1AA4E, 1, 9}, {0x1AA56, 1, 11}};
void stem_take(dspic::CPU& c, void* ctx) {
  auto* x = static_cast<Board::StemTapCtx*>(ctx);
  x->b->stem_term[x->stem] = c.accv[x->acc];
}
void stem_file(dspic::CPU& c, void* ctx) {
  // T5 is about to copy the frame at 0x1EA6 to the output ring at W0: file this pass's terms under that slot
  auto* b = static_cast<Board*>(ctx);
  const uint32_t slot = ((c.W(0) - 0xDF00u) >> 3) & 15u;
  for (int k = 0; k < Board::kStems; k++) {
    const int64_t v = b->stem_term[k] * 8;                       // the mix's final x8 (sftac -3)
    b->stem_ring[slot][k] = int32_t(std::max<int64_t>(INT32_MIN, std::min<int64_t>(INT32_MAX, v)));
  }
}
}  // namespace

void Board::install_stem_taps() {
  stem_hooks_ok = false;
  uint32_t pc = 0x1A9EE;
  for (uint32_t w : kMixWords) { if (cpu.progword(pc) != w) return; pc += 2; }
  if (!words_match(cpu, 0x343EC, {0x2DF001, 0x400001, 0x21EA61, 0x8A4E32, 0x781831, 0x781831, 0x781831, 0x781831})) return;
  static_assert(sizeof(kTaps) / sizeof(kTaps[0]) == kStems, "one tap per stem");
  stem_ctx.resize(kStems);
  for (int i = 0; i < kStems; i++) {
    stem_ctx[size_t(i)] = Board::StemTapCtx{this, kTaps[i].acc, kTaps[i].stem};
    cpu.add_hook(kTaps[i].pc, stem_take, &stem_ctx[size_t(i)]);
  }
  cpu.add_hook(0x343F4, stem_file, this);
  stem_hooks_ok = true;
}

void Board::boot() {
  memset(sfr, 0, sizeof sfr);
  R(OSCCON) = 0x3320;
  for (int p = 1; p <= 6; p++) R(TRISB + (p - 1) * 0x10) = 0xFFFF;
  // IPC reset value: every source at priority 4
  for (int i = 0; i < 36; i++) R(IPC0 + 2 * i) = 0x4444;
  irq_dirty = true;
  cpu.reset(0x4080);
  if (fast_forward) install_fast_forward();
  install_stem_taps();
  bends.install(*this);
  last_sync = cpu.cycles; cpu.next_event = 0; cpu.attention();
}

// ---------------------------------------------------------------- interrupts
void Board::recompute_irq() {
  best_prio = 0; best_vec = 0;
  for (int w = 0; w < 9; w++) {
    uint16_t act = R(IFS0 + 2 * w) & R(IEC0 + 2 * w);
    while (act) {
      int b = __builtin_ctz(act); act &= act - 1;
      int irq = w * 16 + b;
      int pr = (R(IPC0 + 2 * (irq / 4)) >> (4 * (irq & 3))) & 7;
      if (pr > best_prio) { best_prio = pr; best_vec = ivt[irq]; }
    }
  }
  irq_dirty = false;
  cpu.irq_prio = best_prio; cpu.irq_vec = best_vec; cpu.attention();
}
// ---------------------------------------------------------------- SFR access
// A read needs the peripherals brought up to date (sync) only if their state can have changed since the last sync in
// a way the read sees. Between two syncs, before the next planned event, advancing them only moves counters
// (advance is additive), and the only SFRs that can change are the timers' counts and the interrupt flags of timers
// without an interrupt enabled. Other reads skip the sync; the next one catches up, with the same result.
uint16_t Board::sfr_read(uint16_t a) {
  sfr_rd_count[a>>1]++;
  if (cpu.cycles >= cpu.next_event || irq_dirty || sfr_moves[a >> 1]) sync();
  switch (a) {
  case OSCCON: return (R(a) & ~0x7021) | 0x3020;
  case U2STA: {
    uint16_t v = R(a) & ~0x0301; v |= 0x0100;       // TRMT=1, UTXBF=0
    if (rx_n) v |= 1;
    return v; }
  case U2RXREG: {
    if (!rx_n) return 0;
    uint16_t v = rx_fifo[0]; memmove(rx_fifo, rx_fifo + 1, 3); rx_n--;
    return v; }
  case DMA1STAL: return (uint16_t)(R(a) + 2 * dma1_pos);
  case NVMCON: return R(a) & ~0x8000;
  }
  if (a >= 0xE10 && a < 0xE70) {
    int port = (a - 0xE10) >> 4, reg = (a >> 1) & 7;
    if (reg == 1) { uint16_t tris = R(0xE10 + port * 16), lat = R(0xE14 + port * 16);
      uint16_t in = port_in[port + 1];
      if (port == 4) {                                   // PORTF: button rows of the selected columns
        while (!btn_q.empty() && btn_q.front().first <= cpu.cycles) {
          const uint16_t e = btn_q.front().second; set_button(e & 0x1F, (e & 0x100) != 0); btn_q.pop_front();
        }
        uint8_t rows = 0;
        for (int r = 0; r < 4; r++) if (col_latch >> r & 1) rows |= (uint8_t)(buttons >> (8 * r));
        in = (uint16_t)((in & ~0x3F) | (rows & 0x3F));
      } else if (port == 0) {                            // PORTB: encoder A/B on RB14/RB15
        while (!enc_q.empty() && enc_q.front().first <= cpu.cycles) { enc_state = enc_q.front().second; enc_q.pop_front(); }
        in = (uint16_t)((in & 0x3FFF) | (enc_state << 14));
      }
      return (lat & ~tris) | (in & tris); }
  }
  for (auto& t : timers) if (a == t.a_tmr) return R(a);
  return R(a);
}

void Board::sfr_write(uint16_t a, uint16_t v, uint16_t mask) {
  sfr_wr_count[a>>1]++;
  sync();
  sfr_write_inner(a, v, mask);
  if (irq_dirty) recompute_irq();
  plan(cpu.cycles);                               // re-plan events after any SFR change
}
void Board::sfr_write_inner(uint16_t a, uint16_t v, uint16_t mask) {
  uint16_t old = R(a);
  uint16_t nv = (old & ~mask) | (v & mask);
  if (a >= IFS0 && a < 0x8D0) { R(a) = nv; irq_dirty = true; return; }
  switch (a) {
  case U2TXREG: midi_out.push_back((uint8_t)v); midi_out_cycles.push_back(cpu.cycles); set_if(IRQ_U2TX); return;
  case U2RXREG: return;
  case U2STA: R(a) = (nv & ~0x0311) | (old & 0x0010); return;
  case DMA1STAL: R(a) = nv; dma1_pos = 0; return;
  case DMA1CON: R(a) = nv; if (!(old & 0x8000) && (nv & 0x8000)) dma1_pos = 0; return;
  case NVMCON: R(a) = nv; if (nv & 0x8000) nvm_exec(); return;
  }
  if (a >= 0xE10 && a < 0xE70) {
    int reg = (a >> 1) & 7, port = (a - 0xE10) >> 4;
    if (reg == 1 || reg == 2) {                       // PORT/LAT write -> LAT
      uint16_t prev = R(0xE14 + port * 16);
      R(0xE14 + port * 16) = nv;
      if (port == 5) {                                   // RG2 / RG3 rising edges latch PORTD into the panel drivers
        uint8_t d = (uint8_t)R(0xE34);
        if (!(prev & 4) && (nv & 4)) { panel_account(); col_latch = d; }
        if (!(prev & 8) && (nv & 8)) { panel_account(); data_latch = d; }
      }
      return;
    }
  }
  R(a) = nv;
}

// ---------------------------------------------------------------- MIDI in
void Board::midi_in_at(const uint8_t* bytes, size_t n, uint64_t cycle) {
  for (size_t i = 0; i < n; i++) {
    const uint64_t start = std::max(cycle, rx_line_free);
    rx_line_free = start + 2496;                       // 10 bits at 31250 baud
    rxq.push_back({rx_line_free, bytes[i]});
  }
  cpu.next_event = std::min(cpu.next_event, std::max(cpu.cycles, rxq.front().first)); cpu.attention();
}

// ---------------------------------------------------------------- NVM / flash
void Board::tblwt(uint32_t paddr, uint16_t v, bool high, bool byte) {
  if ((paddr >> 16) == 0xFA) {
    int i = (paddr >> 1) & 1;
    if (high) latch[i][1] = v & 0xFF; else latch[i][0] = v;
  }
}
void Board::nvm_exec() {
  uint16_t con = R(NVMCON);
  uint32_t adr = ((uint32_t)R(NVMADRH) << 16) | R(NVMADRL);
  int op = con & 0xF;
  char b[96];
  if (op == 0x3) {                       // page erase: 1024 instructions
    uint32_t base = adr & ~0x7FFu;
    for (uint32_t p = base; p < base + 0x800; p += 2) { if ((p >> 1) < cpu.prog.size()) cpu.prog[p >> 1] = 0xFFFFFF; cpu.invalidate(p); }
    snprintf(b, sizeof b, "NVM erase page %06x\n", base); log += b;
  } else if (op == 0x1) {                // double-word program
    for (int i = 0; i < 2; i++) {
      uint32_t p = (adr & ~3u) + 2 * i;
      uint32_t w = latch[i][0] | (latch[i][1] << 16);
      if ((p >> 1) < cpu.prog.size()) cpu.prog[p >> 1] &= w;
      cpu.invalidate(p);
    }
    latch[0][0] = latch[1][0] = 0xFFFF; latch[0][1] = latch[1][1] = 0xFF;
  } else { snprintf(b, sizeof b, "NVM op %x at %06x (unsupported)\n", op, adr); log += b; }
  R(NVMCON) &= ~0x8000;
}

// ---------------------------------------------------------------- time
void Board::sync() {
  uint64_t now = cpu.cycles;
  if (now > last_sync) { advance((uint32_t)(now - last_sync)); last_sync = now; }
  if (irq_dirty) recompute_irq();
  plan(now);
}

// The next event (a timer interrupt, a converter frame, a MIDI byte, an ADC block), from the state at `now`
// (= last_sync).
void Board::plan(uint64_t now) {
  static const uint32_t ps[4] = {1, 8, 64, 256};
  uint64_t nxt = 1 << 20;
  for (auto& t : timers) {
    uint16_t con = R(t.a_con); if (!(con & 0x8000)) continue;
    if (!(R(IEC0 + 2 * (t.irq / 16)) & (1 << (t.irq & 15)))) continue;   // no interrupt: no event needed
    uint32_t p = ps[(con >> 4) & 3];
    uint64_t rem = ((uint64_t)R(t.a_pr) - R(t.a_tmr) + 1) * p - t.pacc;
    if (rem < nxt) nxt = rem;
  }
  if ((R(DCICON1) & 0x8000) && (R(DMA1CON) & 0x8000)) nxt = std::min<uint64_t>(nxt, frame_acc < frame_cycles ? frame_cycles - frame_acc : 1);
  if (!rxq.empty()) nxt = std::min<uint64_t>(nxt, rxq.front().first > now ? rxq.front().first - now : 1);
  if (adc_running()) { uint32_t p = adc_block_cycles(); nxt = std::min<uint64_t>(nxt, adc_acc < p ? p - adc_acc : 1); }
  if (nxt < 1) nxt = 1;
  cpu.next_event = now + nxt; cpu.attention();
}

void Board::advance(uint32_t n) {
  // timers
  static const uint32_t ps[4] = {1, 8, 64, 256};
  for (auto& t : timers) {
    uint16_t con = R(t.a_con);
    if (!(con & 0x8000)) continue;
    uint32_t p = ps[(con >> 4) & 3];
    t.pacc += n;
    if (t.pacc < p) continue;
    uint32_t ticks = t.pacc / p; t.pacc -= ticks * p;
    uint32_t pr = R(t.a_pr), tm = R(t.a_tmr);
    tm += ticks;
    if (tm > pr) { uint32_t per = pr + 1; tm = (tm - per) % per; set_if(t.irq); }
    R(t.a_tmr) = (uint16_t)tm;
  }
  // UART2 RX at 31250 baud (10 bits/byte)
  {
    const uint64_t now = last_sync + n;
    while (!rxq.empty() && rxq.front().first <= now) {
      if (R(U2MODE) & 0x8000) {
        if (rx_n < 4) { rx_fifo[rx_n++] = rxq.front().second; set_if(IRQ_U2RX); }
        else R(U2STA) |= 0x0002;           // OERR
      }
      rxq.pop_front();
    }
  }
  // ADC1 scan -> DMA0 ping-pong block -> DMA0 interrupt (trigger front-end)
  if (adc_running()) {
    adc_acc += n;
    uint32_t p = adc_block_cycles();
    while (adc_acc >= p) { adc_acc -= p; adc_block(); }
  }
  // DCI + DMA1: one stereo frame (4 words) every 2048 cycles
  if ((R(DCICON1) & 0x8000) && (R(DMA1CON) & 0x8000)) {
    frame_acc += n;
    while (frame_acc >= frame_cycles) {
      frame_acc -= frame_cycles;
      uint32_t cnt = R(DMA1CNT) + 1, base = R(DMA1STAL);
      uint16_t w[4];
      const uint32_t slot = ((base + 2 * dma1_pos - 0xDF00u) >> 3) & 15u;
      for (int i = 0; i < 4; i++) {
        uint16_t a = base + 2 * dma1_pos;
        w[i] = cpu.ram[a] | (cpu.ram[a + 1] << 8);
        dma1_pos = (dma1_pos + 1) % cnt;
      }
      if (capture_raw) raw_frames.insert(raw_frames.end(), w, w + 4);
      audio.push_back((int32_t)(((uint32_t)w[0] << 16) | w[1]));
      audio.push_back((int32_t)(((uint32_t)w[2] << 16) | w[3]));
      if (stems_on) stems.insert(stems.end(), stem_ring[slot], stem_ring[slot] + kStems);
      frames++;
    }
  }
}

// ---------------------------------------------------------------- front panel
int Board::lit_col() const {
  uint8_t s = col_latch;
  for (int k = 0; k < 4; k++) if (s == (0xF0 | (1 << k))) return k;          // digits: active-high select
  for (int k = 0; k < 4; k++) if (s == (uint8_t)(0xF0 & ~(0x10 << k))) return 4 + k;   // LEDs: active-low select
  return -1;
}
void Board::panel_account() {
  uint64_t now = cpu.cycles, dt = now - pan_last;
  pan_last = now; pan_total += dt;
  int col = lit_col();
  if (col < 0) return;
  // digits: segments active-low; LED columns: active-high
  uint8_t lit = col < 4 ? (uint8_t)~data_latch : data_latch;
  for (int b = 0; b < 8; b++) if (lit >> b & 1) seg_on[col][b] += (double)dt;
}
Board::PanelState Board::panel(bool reset) {
  panel_account();
  PanelState p{};
  double tot = pan_total ? (double)pan_total / 8.0 : 1.0;    // full duty = lit for 1/8 of the time
  static const char* digits = " 0123456789ABCDEFHLPSUbcdnortu-_";
  for (int d = 0; d < 4; d++) {
    uint8_t seg = 0xFF;
    for (int b = 0; b < 8; b++) if (seg_on[d][b] / tot > 0.25) seg &= ~(1 << b);
    p.seg[d] = seg;
    char ch = '?';
    for (const char* c = digits; *c; c++) if ((cpu.progword(0x38000 + 0x7CE8 + (*c & ~1)) >> ((*c & 1) * 8) & 0x7F) == (seg & 0x7F)) { ch = *c; break; }
    p.display += ch;
    if (!(seg & 0x80)) p.display += '.';
  }
  for (int c = 4; c < 8; c++) for (int b = 0; b < 8; b++) p.led_duty[(c - 4) * 8 + b] = seg_on[c][b] / tot;
  if (reset) { memset(seg_on, 0, sizeof seg_on); pan_total = 0; }
  return p;
}
void Board::encoder_turn(int detents, double transition_s) {
  // detent rest state = 3 (A and B high); one detent forward = 3 -> 1 -> 0 -> 2 -> 3 (the OS's decode table)
  static const uint8_t gray[4] = {3, 1, 0, 2};
  uint64_t t = std::max<uint64_t>(cpu.cycles, enc_q.empty() ? 0 : enc_q.back().first);
  int pos = 0; for (int i = 0; i < 4; i++) if (gray[i] == (enc_q.empty() ? enc_state : enc_q.back().second)) pos = i;
  for (int s = 0; s < 4 * std::abs(detents); s++) {
    pos = (pos + (detents > 0 ? 1 : 3)) & 3;
    t += (uint64_t)(transition_s * FCY);
    enc_q.push_back({t, gray[pos]});
  }
}

// ---------------------------------------------------------------- ADC1 + DMA0
bool Board::adc_running() {
  return (R(AD1CON1) & 0x8000) && (R(DMA0CON) & 0x8000) && (R(AD1CON4) & 0x0100);
}
uint32_t Board::adc_block_cycles() {
  // conversion = (SAMC + 14 TAD) for 12-bit (12 for 10-bit); TAD = (ADCS + 1) Tcy; block = SMPI + 1 conversions
  uint16_t c3 = R(AD1CON3), c1 = R(AD1CON1), c2 = R(AD1CON2);
  uint32_t tad = (c3 & 0xFF) + 1, samc = (c3 >> 8) & 0x1F, conv = (c1 & 0x0400) ? 14 : 12;
  uint32_t n = ((c2 >> 2) & 0x1F) + 1;
  return std::max<uint32_t>(64, (samc + conv) * tad * n);
}
void Board::piezo_hit(int an, double amp, double freq, double decay_ms) {
  hits.push_back({an, cpu.cycles, amp, 2 * M_PI * freq / FCY, decay_ms * 1e-3 * FCY});
}
void Board::adc_block() {
  // scan order: ascending AN of the selected inputs (CSSL = AN0..15, CSSH = AN16..31)
  uint32_t sel = R(AD1CSSL) | ((uint32_t)R(AD1CSSH) << 16);
  uint16_t base = dma0_next ? R(DMA0STBL) : R(DMA0STAL);
  uint32_t cnt = R(DMA0CNT) + 1, i = 0;
  uint64_t now = cpu.cycles;
  double live[32] = {};
  if (adc_live) adc_live->next_block(live);
  for (int an = 0; an < 32 && i < cnt; an++) {
    if (!(sel >> an & 1)) continue;
    double v = adc_static[an] + live[an];
    for (auto& h : hits) if (h.an == an && now >= h.start) {
      double t = (double)(now - h.start);
      v += h.amp * std::sin(h.w * t) * std::exp(-t / h.tau);
    }
    int code = (int)std::lround(v * 2048.0);
    code = std::max(-2048, std::min(2047, code));
    uint16_t word = (uint16_t)(int16_t)(code * 16);          // 12-bit signed fractional, left-justified
    uint16_t a = base + 2 * i++;
    cpu.ram[a] = (uint8_t)word; cpu.ram[a + 1] = (uint8_t)(word >> 8);
  }
  // DMAPPS.PPST0 reports the buffer that has just been filled; the ISR reads that one
  R(DMAPPS) = (R(DMAPPS) & ~1) | (uint16_t)dma0_next;
  dma0_next ^= 1;
  set_if(IRQ_DMA0);
  adc_blocks++;
  // drop finished hits (after 12 time constants)
  hits.erase(std::remove_if(hits.begin(), hits.end(), [&](const Hit& h) { return now > h.start && (double)(now - h.start) > 12 * h.tau; }), hits.end());
}

// ---------------------------------------------------------------- project state
void Board::saveState(StateWriter& w) const {
  w.section("CPU ", [&](StateWriter& s) { cpu.saveState(s); });
  w.section("FLSH", [&](StateWriter& s) {             // preset/settings flash 0x40000..0x557FF
    s.put(uint32_t(0x40000)); s.put(uint32_t(0x55800));
    for (uint32_t p = 0x40000; p < 0x55800; p += 2) s.put(cpu.prog[p >> 1]);
  });
  w.section("SFR ", [&](StateWriter& s) { s.raw(sfr, sizeof sfr); });
  w.section("PERI", [&](StateWriter& s) {
    s.put(uint32_t(1));
    for (auto& t : timers) s.put(t.pacc);
    s.deq(rxq); s.put(rx_line_free); s.raw(rx_fifo, sizeof rx_fifo); s.put(int32_t(rx_n));
    s.put(frame_acc); s.put(dma1_pos); s.put(adc_acc); s.put(int32_t(dma0_next));
    s.raw(adc_static, sizeof adc_static);
    s.put(uint64_t(hits.size())); for (auto& h : hits) { s.put(int32_t(h.an)); s.put(h.start); s.put(h.amp); s.put(h.w); s.put(h.tau); }
    s.put(buttons); s.put(col_latch); s.put(data_latch); s.put(pan_last); s.put(pan_total); s.raw(seg_on, sizeof seg_on);
    s.deq(enc_q); s.put(enc_state); s.deq(btn_q);
    s.raw(latch, sizeof latch);
    s.put(last_sync); s.raw(port_in, sizeof port_in); s.put(frames); s.put(adc_blocks);
    s.vec(audio); s.vec(midi_out); s.vec(midi_out_cycles);
  });
  w.section("STEM", [&](StateWriter& s) {
    s.put(uint32_t(1));
    s.raw(stem_term, sizeof stem_term); s.raw(stem_ring, sizeof stem_ring); s.vec(stems);
  });
}

bool Board::loadState(StateReader& r) {
  std::string tag; StateReader b;
  bool cpuOk = false, peri = false;
  while (r.nextSection(tag, b)) {
    if (tag == "CPU ") cpuOk = cpu.loadState(b) && b.ok();
    else if (tag == "FLSH") {
      uint32_t lo = b.get<uint32_t>(), hi = b.get<uint32_t>();
      if (lo != 0x40000 || hi != 0x55800) return false;
      for (uint32_t p = lo; p < hi; p += 2) { cpu.prog[p >> 1] = b.get<uint32_t>(); cpu.invalidate(p); }
    } else if (tag == "SFR ") b.raw(sfr, sizeof sfr);
    else if (tag == "PERI") {
      if (b.get<uint32_t>() != 1) return false;
      for (auto& t : timers) b.get(t.pacc);
      b.deq(rxq); b.get(rx_line_free); b.raw(rx_fifo, sizeof rx_fifo); rx_n = b.get<int32_t>();
      b.get(frame_acc); b.get(dma1_pos); b.get(adc_acc); dma0_next = b.get<int32_t>();
      b.raw(adc_static, sizeof adc_static);
      uint64_t nh = b.get<uint64_t>(); if (nh > 4096) return false;
      hits.clear(); for (uint64_t i = 0; i < nh && b.ok(); i++) { Hit h; h.an = b.get<int32_t>(); b.get(h.start); b.get(h.amp); b.get(h.w); b.get(h.tau); hits.push_back(h); }
      b.get(buttons); b.get(col_latch); b.get(data_latch); b.get(pan_last); b.get(pan_total); b.raw(seg_on, sizeof seg_on);
      b.deq(enc_q); b.get(enc_state); b.deq(btn_q);
      b.raw(latch, sizeof latch);
      b.get(last_sync); b.raw(port_in, sizeof port_in); b.get(frames); b.get(adc_blocks);
      b.vec(audio); b.vec(midi_out); b.vec(midi_out_cycles);
      peri = b.ok();
    } else if (tag == "STEM") {
      if (b.get<uint32_t>() != 1) return false;
      b.raw(stem_term, sizeof stem_term); b.raw(stem_ring, sizeof stem_ring); b.vec(stems);
    }
    if (!b.ok()) return false;
  }
  if (!r.ok() || !cpuOk || !peri) return false;
  const uint64_t planned = cpu.next_event;            // the saved plan is valid for the identical restored state
  irq_dirty = true; recompute_irq();
  cpu.next_event = std::min(cpu.next_event, planned); cpu.attention();
  return true;
}

}  // namespace nd2
