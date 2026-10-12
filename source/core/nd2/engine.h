// Engine: what the plugin and standalone run (no JUCE). Same architecture as the owner's other emulator project:
//  - the machine runs per host block on the audio thread; boot and restore happen on a worker thread and the new
//    machine is swapped in under a short lock;
//  - host MIDI is delivered sample-accurately at a constant reported latency: event at host sample H reaches the
//    emulated MIDI input at machine cycle c0 + H * Fcy / hostRate + D;
//  - the six trigger inputs (optional host audio bus) are converted to the ADC scan rate and reach the emulated ADC
//    at the same machine time as MIDI would (so audio pads and MIDI notes line up);
//  - audio out: native 38085.9375 Hz -> host rate (windowed-sinc converter);
//  - panel controls from any thread are lock-free; display/LEDs are published as atomics with a change serial;
//  - getState asks the audio thread for a snapshot at a block boundary.
#pragma once
#include "board.h"
#include "rom.h"
#include "panel_map.h"
#include "util/midi_stream.h"
#include "util/rate_converter.h"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nd2 {

class Engine {
public:
  enum class State { NoOs, Booting, Running, Stopped };
  struct MidiEvent { int offset; const uint8_t* data; int size; };       // offset in host samples within the block
  struct MidiOutEvent { int offset; uint32_t start, size; };
  static constexpr int kPads = 6;

  Engine();
  ~Engine();

  // ---- message thread
  bool setOs(const OsImage& os, std::string& err);                      // boots on the worker
  void setBootFlash(std::vector<uint8_t> flash);                          // flash for fresh boots (empty = erased)
  std::vector<uint8_t> getState();                                        // empty if nothing is running
  bool setState(const std::vector<uint8_t>& blob, std::string& err);     // restores on the worker
  State state() const { return state_.load(); }
  std::string message() const { std::lock_guard<std::mutex> l(msgMtx_); return message_; }
  void waitForJob();                                                      // tests and offline tools
  // State blobs outside the engine (bank transfers on an offline machine): the same format as getState/setState.
  static std::vector<uint8_t> stateOf(const Board& b, uint64_t osHash);
  static std::unique_ptr<Board> boardFromState(const std::vector<uint8_t>& osImage, uint64_t osHash,
                                               const std::vector<uint8_t>& blob, std::string& err);

  // ---- panel, any thread (lock-free)
  void pressButton(int idx, bool down);
  void turnEncoder(int detents) { encoder_.fetch_add(detents); }
  void setMasterLevel(float pos) { master_.store(pos); masterDirty_.store(true); }
  void setTrigGain(int pad, float gain) { if (pad >= 0 && pad < kPads) trigGain_[pad].store(gain); }
  struct PanelView { std::array<uint8_t, 4> seg; uint32_t leds; };      // seg active-low, led bit = LED index
  PanelView panel() const;
  // LED brightness as the OS drives it (its PWM duty), in eighths: 0 = off ... 8 = full. Index = LED number.
  int ledLevel(int led) const { return led >= 0 && led < 32 ? ledLevel_[led].load(std::memory_order_relaxed) : 0; }
  float masterLevel() const { return master_.load(); }
  uint32_t uiSerial() const { return uiSerial_.load(); }
  // The program the OS has loaded: bank * 50 + program (bank 0-7, program 0-49), -1 before the machine runs.
  int currentProgram() const { return current_.load(std::memory_order_relaxed); }
  bool trigClip(int pad) const { return pad >= 0 && pad < kPads && clipHold_[pad].load() > 0; }

  // ---- audio thread
  // stems: also produce the six channel outputs (stereo pairs, exact shares of the main output; see Board)
  void prepare(double hostRate, int maxBlock, bool trigIn, bool stems = false);
  // trig: kPads channel pointers (nullptr entries or nullptr array = silence)
  // stemsOut: kStemChannels pointers L1,R1..L6,R6 (used only when prepared with stems; nullptr entries are skipped)
  void process(float* left, float* right, int n, const MidiEvent* events, int numEvents, const float* const* trig = nullptr,
               float* const* stemsOut = nullptr);
  static constexpr int kStemChannels = Board::kStems;
  int latencySamples() const { return latency_; }

  // ---- MIDI out (to the host: offsets in the block just processed) and the virtual port
  void setMidiToHost(bool on) { hostOutOn_.store(on); }
  const std::vector<MidiOutEvent>& hostMidiEvents() const { return hostEvents_; }
  const uint8_t* hostMidiBytes() const { return hostBytes_.data(); }
  void virtualMidiIn(const uint8_t* bytes, size_t n);                    // ONE producer thread
  MessageRing& virtualMidiOut() { return vout_; }                         // ONE consumer thread
  void setVirtualPort(bool on) { virtualOn_.store(on); }

  // ---- stats
  uint64_t lateMidi() const { return lateMidi_; }
  uint64_t trigUnderruns() const { return trigUnder_; }
  uint64_t cyclesForTest();
  uint64_t lastMidiCycleForTest() const { return lastMidiCycle_; }
  uint64_t blockOriginForTest() const { return c0_; }

private:
  struct TrigFifo;
  // machine + job
  mutable std::mutex mtx_;                 // guards live_ (audio thread vs install)
  std::unique_ptr<Board> live_;
  std::atomic<State> state_{State::NoOs};
  std::thread worker_;
  std::atomic<bool> cancel_{false};
  std::vector<uint8_t> os_;
  uint64_t osHash_ = 0;
  std::vector<uint8_t> bootFlash_;
  mutable std::mutex msgMtx_;
  std::string message_;
  void setMessage(const std::string& m) { std::lock_guard<std::mutex> l(msgMtx_); message_ = m; }
  void startJob(std::vector<uint8_t> restore);
  void runJob(std::vector<uint8_t> restore);
  void install(std::unique_ptr<Board> b);

  // timing
  double hostRate_ = 48000, cps_ = 0;      // machine cycles per host sample
  int maxBlock_ = 512;
  bool trigOn_ = false;
  bool retime_ = true;
  uint64_t c0_ = 0, D_ = 0;                // block-origin cycle, injection delay (cycles)
  int64_t hostPos_ = 0;
  int latency_ = 0;
  RateConverter outConv_, trigConv_;
  size_t audioRead_ = 0;
  std::vector<float> nativeL_, nativeR_, trigTmp_[kPads];
  bool stemsOn_ = false;
  size_t stemRead_ = 0;
  std::vector<float> nativeS_[kStemChannels], stemScratch_[kStemChannels];
  std::unique_ptr<TrigFifo> fifo_;
  uint64_t lateMidi_ = 0, trigUnder_ = 0, lastMidiCycle_ = 0;
  uint64_t cycleOfHost(double h) const { return c0_ + uint64_t(h * cps_ + 0.5) + D_; }
  void retime(Board& b);
  bool runFrames(Board& b, size_t frames);

  // panel inputs
  static constexpr uint32_t kButtonRing = 1024;
  struct ButtonCell { std::atomic<uint32_t> seq; uint32_t value; };
  ButtonCell buttons_[kButtonRing];
  std::atomic<uint32_t> btnEnqueue_{0};
  uint32_t btnDequeue_ = 0;
  std::atomic<int> encoder_{0};
  std::atomic<float> master_{1.0f};
  std::atomic<bool> masterDirty_{true};
  std::atomic<float> trigGain_[kPads];
  std::atomic<int> clipHold_[kPads];
  void applyPanel(Board& b);

  // panel read-out
  std::atomic<uint32_t> segWord_{0xFFFFFFFF}, leds_{0}, uiSerial_{0};
  std::atomic<uint8_t> ledLevel_[32] = {};
  std::atomic<int> current_{-1};
  uint64_t panelSince_ = 0;
  void publishPanel(Board& b);

  // MIDI out
  MidiParser outParser_{size_t(1) << 20};
  std::atomic<bool> hostOutOn_{false}, virtualOn_{true};
  std::vector<MidiOutEvent> hostEvents_;
  std::vector<uint8_t> hostBytes_;
  size_t hostUsed_ = 0;
  void midiOut(Board& b, int n);
  // virtual MIDI in/out
  MidiParser vinParser_{size_t(512) << 10};
  MessageRing vin_{size_t(1) << 20}, vout_{size_t(1) << 20};

  // state snapshots
  std::atomic<uint32_t> snapRequest_{0}, snapDone_{0};
  std::vector<uint8_t> snap_;
  std::atomic<int64_t> lastAudioMs_{0};
  std::mutex getStateMtx_;
  void serveSnapshot(Board& b);
};

}  // namespace nd2
