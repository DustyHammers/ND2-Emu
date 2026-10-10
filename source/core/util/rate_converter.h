// Multi-channel sample-rate converter: windowed-sinc (Kaiser) polyphase filter with linear interpolation between
// 512 phases, exact rational stepping (rates given as integer ratios, so 38085.9375 Hz = 609375/16 stays exact).
// Same design as the owner's other emulator project's resampler, generalised to N channels, non-integer rates and
// a chosen kernel length / passband (the drum's rates sit well below 44.1 kHz).
//
// Pull model: inputNeeded(n) -> push(n frames) -> produce(n). Output frame k is the input at position
// k * inRate / outRate: no delay of its own, it only reads lookahead() input frames beyond that position.
#pragma once
#include <cstdint>
#include <vector>

namespace nd2 {

class RateConverter {
public:
  struct Design {
    double inHz = 48000, outHz = 48000;   // real rates (filter design)
    int64_t inStep = 1, outStep = 1;      // exact ratio inHz:outHz as integers (stepping)
    int taps = 64;                        // kernel length at the lower of the two rates
    double passHz = 0;                    // passband edge; 0 = 0.45 x the lower rate
    double beta = 8.0;                    // Kaiser window (8 ~ 80 dB, 5 ~ 50 dB)
  };
  void setup(int channels, const Design& d);
  void reset();
  bool bypass() const { return inStep_ == outStep_; }
  int lookahead() const { return bypass() ? 0 : half_; }       // in input frames
  int channels() const { return ch_; }

  int64_t inputNeeded(int nOut) const;
  int64_t available() const;
  void push(const float* const* in, int n);                     // in[ch][i]; nullptr channel = silence
  void produce(float* const* out, int nOut);

private:
  int ch_ = 0;
  int64_t inStep_ = 1, outStep_ = 1;
  int taps_ = 0, half_ = 0;
  static constexpr int kPhases = 512;
  std::vector<float> table_;
  int64_t ip_ = 0, frac_ = 0;
  std::vector<std::vector<float>> hist_;
  int64_t base_ = 0, pushed_ = 0;
  float at(int c, int64_t i) const { return (i < base_ || i >= pushed_) ? 0.0f : hist_[c][size_t(i - base_)]; }
};

}  // namespace nd2
