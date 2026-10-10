#include "rate_converter.h"
#include <algorithm>
#include <cmath>

namespace nd2 {

namespace {
double besselI0(double x) {
  double sum = 1, term = 1, q = x * x / 4;
  for (int k = 1; k < 64; ++k) { term *= q / (double(k) * k); sum += term; if (term < sum * 1e-17) break; }
  return sum;
}
}  // namespace

void RateConverter::setup(int channels, const Design& d) {
  ch_ = std::max(1, channels);
  inStep_ = std::max<int64_t>(1, d.inStep);
  outStep_ = std::max<int64_t>(1, d.outStep);
  table_.clear();
  if (bypass()) { taps_ = half_ = 0; reset(); return; }
  const double scale = std::min(1.0, d.outHz / d.inHz);                    // < 1 when converting down
  taps_ = int(std::ceil(d.taps / scale / 2.0)) * 2;                        // kernel in input frames
  half_ = taps_ / 2;
  const double lower = std::min(d.inHz, d.outHz);
  const double passEdge = d.passHz > 0 ? std::min(d.passHz, 0.49 * lower) : 0.45 * lower;
  const double stopEdge = 0.5 * lower;
  const double fc = 0.5 * (passEdge + stopEdge) / d.inHz;                  // cycles per input frame
  const double i0b = besselI0(d.beta);
  table_.assign(size_t(kPhases + 1) * size_t(taps_), 0.0f);
  std::vector<double> row(static_cast<size_t>(taps_));
  for (int ph = 0; ph <= kPhases; ++ph) {
    const double t = double(ph) / kPhases;
    double sum = 0;
    for (int j = 0; j < taps_; ++j) {
      const double dist = t + half_ - 1 - j;
      const double x = dist / half_;
      const double w = std::fabs(x) >= 1.0 ? 0.0 : besselI0(d.beta * std::sqrt(1.0 - x * x)) / i0b;
      const double a = 2.0 * fc * dist * M_PI;
      const double s = std::fabs(a) < 1e-12 ? 1.0 : std::sin(a) / a;
      row[size_t(j)] = 2.0 * fc * s * w;
      sum += row[size_t(j)];
    }
    for (int j = 0; j < taps_; ++j) table_[size_t(ph) * size_t(taps_) + size_t(j)] = float(row[size_t(j)] / sum);
  }
  reset();
}

void RateConverter::reset() {
  ip_ = frac_ = 0;
  pushed_ = 0;
  base_ = -int64_t(taps_);
  hist_.assign(size_t(ch_), std::vector<float>(size_t(taps_), 0.0f));
  for (auto& h : hist_) h.reserve(size_t(taps_) + 65536);
}

int64_t RateConverter::inputNeeded(int nOut) const {
  if (nOut <= 0) return 0;
  int64_t last;
  if (bypass()) last = ip_ + nOut - 1;
  else last = ip_ + (frac_ + int64_t(nOut - 1) * inStep_) / outStep_ + half_;
  return std::max<int64_t>(0, last + 1 - pushed_);
}

int64_t RateConverter::available() const {
  if (bypass()) return std::max<int64_t>(0, pushed_ - ip_);
  const int64_t m = pushed_ - 1 - half_ - ip_;
  if (m < 0) return 0;
  int64_t k = ((m + 1) * outStep_ - 1 - frac_) / inStep_ + 1;
  while (k > 0 && inputNeeded(int(k)) > 0) --k;
  return k;
}

void RateConverter::push(const float* const* in, int n) {
  for (int c = 0; c < ch_; ++c) {
    auto& h = hist_[size_t(c)];
    if (in && in[c]) h.insert(h.end(), in[c], in[c] + n);
    else h.insert(h.end(), size_t(n), 0.0f);
  }
  pushed_ += n;
}

void RateConverter::produce(float* const* out, int nOut) {
  if (bypass()) {
    for (int k = 0; k < nOut; ++k, ++ip_) for (int c = 0; c < ch_; ++c) out[c][k] = at(c, ip_);
  } else {
    for (int k = 0; k < nOut; ++k) {
      const int64_t first = ip_ - half_ + 1;
      if (first < base_ || ip_ + half_ >= pushed_) {
        for (int c = 0; c < ch_; ++c) out[c][k] = 0.0f;
      } else {
        const double pos = double(frac_) / double(outStep_) * kPhases;
        const int ph = std::min(kPhases - 1, int(pos));
        const float a = float(pos - ph), b = 1.0f - a;
        const float* c0 = &table_[size_t(ph) * size_t(taps_)];
        const float* c1 = c0 + taps_;
        for (int c = 0; c < ch_; ++c) {
          const float* x = &hist_[size_t(c)][size_t(first - base_)];
          float s = 0;
          for (int j = 0; j < taps_; ++j) s += x[j] * (c0[j] * b + c1[j] * a);
          out[c][k] = s;
        }
      }
      frac_ += inStep_;
      ip_ += frac_ / outStep_;
      frac_ %= outStep_;
    }
  }
  const int64_t keepFrom = ip_ - half_ - 1;
  if (keepFrom - base_ > 8192) {
    const size_t drop = size_t(keepFrom - base_);
    for (auto& h : hist_) h.erase(h.begin(), h.begin() + long(drop));
    base_ += int64_t(drop);
  }
}

}  // namespace nd2
