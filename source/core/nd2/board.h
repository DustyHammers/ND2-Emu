// ND2 board model: dsPIC33EP512 peripherals as used by the original OS v3.00
#pragma once
#include "dspic/cpu.h"
#include "bends.h"
#include "util/state_io.h"
#include <deque>
#include <vector>
#include <string>

namespace nd2 {

constexpr double FCY = 78e6;
constexpr int    FRAME_CYCLES = 2048;            // DCI frame: Fs = Fcy/2048

struct Timer {
  uint16_t a_tmr, a_pr, a_con; int irq;          // SFR addresses + interrupt number
  uint32_t pacc = 0;                             // prescaler accumulator
};

class Board : public dspic::Bus {
public:
  Board();
  dspic::CPU cpu;
  void load_app(const std::vector<uint8_t>& img); // decrypted image, loads at program address 0x4000
  void boot();                                   // reset into crt0
  bool fast_forward = true;                      // skip busy-wait loops (cycle-exact)
  void install_fast_forward();

  // Channel outputs (an emulator extension; the hardware has only the stereo pair). The OS mixes six mono
  // channels as L = sum ch*gainL, R = sum ch*gainR, gains = level x pan x master. Read-only hooks take each
  // channel's two terms from that mix, so stem pair k is exactly channel k's share of the main output and the six
  // pairs add up to it (until the summed mix saturates). They travel through a copy of the output ring buffer,
  // so they leave in the same frame as the mix they belong to.
  static constexpr int kStems = 12;              // L1,R1 .. L6,R6
  bool stems_on = false;                         // collect `stems` alongside `audio`
  std::vector<int32_t> stems;                    // kStems words per frame, same format as `audio`
  bool stems_available() const { return stem_hooks_ok; }
  void install_stem_taps();

  // Circuit-bent mode (an emulator extension, see bends.h): the hooks are installed by boot(), all bends Off.
  Bends bends;
  // CPU cycles per codec frame (CPU Starve and Brownouts): FRAME_CYCLES unless a bend is on. Not part of the state.
  uint32_t frame_cycles = FRAME_CYCLES;

  // Bus
  uint16_t sfr_read(uint16_t a) override;
  void sfr_write(uint16_t a, uint16_t v, uint16_t mask) override;
  void tblwt(uint32_t paddr, uint16_t v, bool high, bool byte) override;
  void sync() override;
  void irq_taken(uint32_t vec) override { for (int i = 0; i < 128; i++) if (ivt[i] == vec) { irq_count[i]++; break; } }

  // MIDI in (UART2 RX at 31250 baud): bytes start arriving at `cycle` (or when the line is free), one per 2496 cycles
  void midi_in_at(const uint8_t* bytes, size_t n, uint64_t cycle);
  void midi_in(const std::vector<uint8_t>& bytes) { midi_in_at(bytes.data(), bytes.size(), cpu.cycles); }
  uint64_t midi_line_free() const { return rx_line_free; }
  std::vector<uint8_t> midi_out;                 // bytes the OS sent (UART2 TX) ...
  std::vector<uint64_t> midi_out_cycles;         // ... and the cycle each was written

  // project state (everything except the OS image, which the caller must load first and identify)
  void saveState(StateWriter& w) const;
  bool loadState(StateReader& r);

  // ADC1 inputs (trigger pads etc.): value in [-1, 1) relative to mid-scale, as seen by the 12-bit ADC
  void set_adc(int an, double v) { if (an >= 0 && an < 32) adc_static[an] = v; }
  // piezo-like hit on input `an`: damped sine (amp 0..1, freq Hz, decay time constant ms), starting now
  void piezo_hit(int an, double amp, double freq = 180.0, double decay_ms = 6.0);
  uint64_t adc_blocks = 0;
  // live ADC source (host audio for the trigger inputs): called once per ADC scan block, adds to `add[an]`
  struct AdcSource { virtual ~AdcSource() {} virtual void next_block(double* add) = 0; };
  AdcSource* adc_live = nullptr;
  uint32_t adc_block_period() { return adc_block_cycles(); }
  uint64_t adc_next_block_cycle() { sync(); uint32_t p = adc_block_cycles(); return cpu.cycles + (adc_acc < p ? p - adc_acc : 0); }
  bool adc_active() { return adc_running(); }

  // front panel: 8-column multiplex on PORTD, column select latched by RG2, data latched by RG3.
  // Columns 0-3 = the 4 seven-segment digits (active-low, bit 7 = DP), columns 4-7 = LEDs.
  // Buttons: rows 0-3 (= selected columns 0-3) read on RF0..RF5; index = row * 8 + bit.
  void set_button(int idx, bool down) { if (down) buttons |= 1u << idx; else buttons &= ~(1u << idx); }
  // timed edge (applied in order, each no earlier than `cycle`)
  void button_at(int idx, bool down, uint64_t cycle) { btn_q.push_back({cycle, uint16_t((idx & 0x1F) | (down ? 0x100 : 0))}); }
  // +/- detents; schedules the quadrature transitions. Slow turns (default 10 ms per transition) avoid the OS's acceleration.
  void encoder_turn(int detents, double transition_s = 0.010);
  struct PanelState { std::string display; uint8_t seg[4]; double led_duty[32]; };   // led index = (col-4)*8 + bit
  PanelState panel(bool reset = true);           // lit-time averages since the previous call

  // audio
  std::vector<int32_t> audio;                    // interleaved L,R (32-bit as sent to the codec)
  std::vector<uint16_t> raw_frames;              // 4 raw DCI words per frame (debug)
  int64_t stem_term[kStems] = {};                // this render pass's mix terms (accumulator values)
  int32_t stem_ring[16][kStems] = {};            // per output-ring slot (16 frames of 4 words)
  bool stem_hooks_ok = false;
  struct StemTapCtx { Board* b; int acc; int stem; };
  std::vector<StemTapCtx> stem_ctx;              // sized once before the hooks are added (stable addresses)
  bool capture_raw = false;
  uint64_t frames = 0;

  // pins
  uint16_t port_in[7];                           // external pin levels, B..G at index 1..6

  // stats
  uint64_t irq_count[128] = {};
  std::string log;
  uint16_t sfr[0x800];
  uint64_t sfr_rd_count[0x800] = {}, sfr_wr_count[0x800] = {};                           // 0x0000..0x0FFF word storage (index = addr/2)

private:
  std::vector<Timer> timers;
  std::deque<std::pair<uint64_t, uint8_t>> rxq;   // (cycle the byte has fully arrived, byte)
  uint64_t rx_line_free = 0; uint8_t rx_fifo[4] = {}; int rx_n = 0;
  uint32_t frame_acc = 0; uint32_t dma1_pos = 0;  // words
  uint32_t adc_acc = 0; int dma0_next = 0;        // next ping-pong buffer to fill (0 = A, 1 = B)
  double adc_static[32] = {};
  struct Hit { int an; uint64_t start; double amp, w, tau; };
  std::vector<Hit> hits;
  bool adc_running();
  // panel
  uint32_t buttons = 0;
  uint8_t col_latch = 0, data_latch = 0xFF;
  uint64_t pan_last = 0, pan_total = 0; double seg_on[8][8] = {};
  std::deque<std::pair<uint64_t, uint8_t>> enc_q; uint8_t enc_state = 3;
  std::deque<std::pair<uint64_t, uint16_t>> btn_q;
  void panel_account();
  int lit_col() const;
  uint32_t adc_block_cycles();
  void adc_block();
  // NVM
  uint32_t latch[2][2] = {{0xFFFF, 0xFF}, {0xFFFF, 0xFF}};
  void nvm_exec();
  // IRQ
  int best_prio = 0; uint32_t best_vec = 0; bool irq_dirty = true;
  uint32_t ivt[128];
  void set_if(int irq) { sfr[(0x800 >> 1) + irq / 16] |= 1 << (irq & 15); irq_dirty = true; }
  void recompute_irq();
  void sfr_write_inner(uint16_t a, uint16_t v, uint16_t mask);
  void advance(uint32_t n);
  void plan(uint64_t now);
  uint64_t last_sync = 0;
  bool sfr_moves[0x800] = {};                    // SFRs whose value changes with time alone (timer counts, flags)
  uint16_t& R(uint16_t a) { return sfr[a >> 1]; }
};

}  // namespace nd2
