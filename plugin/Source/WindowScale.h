// Window size (no JUCE, so it can be tested alone). The size is a scale of the skin's 100 % size, 50-200 %, aspect
// locked. For skins 100 % is 3/4 of the body's dp size in points (1725 x 1035 dp -> 1294 x 776 points), so
// 133 % (4/3) shows the art at its native resolution on a Retina screen ("full size"); above that it is enlarged
// beyond its native resolution. Drawing always follows the dp ratio (no bitmap stretching). Built-in's 100 % is its
// own size in points.
//
// The scale used to be relative to the full body size (old 75 % = new 100 %): sizes saved under the old scale are
// converted with fromLegacy (new = old x 4/3) for file skins.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace winscale {

constexpr float kMin = 0.5f, kMax = 2.0f;
constexpr float kFileSkinUnit = 0.75f;         // points per dp at 100 % (file skins)
constexpr float kFullSize = 4.0f / 3.0f;       // file skins: one point per dp, the art's native resolution
constexpr int kSteps[] = {50, 75, 100, 125, 133, 150, 175, 200};

inline float clamp(float s) { return std::isfinite(s) ? std::min(kMax, std::max(kMin, s)) : 1.0f; }
// Points per dp (file skins) or per Built-in pixel at 100 %.
inline float unit(bool fileSkin) { return fileSkin ? kFileSkinUnit : 1.0f; }
// The scale of a Zoom menu step (133 % is exactly 4/3, the full size).
inline float stepScale(int percent) { return percent == 133 ? kFullSize : float(percent) / 100.0f; }
// True when `scale` is the menu step `percent` (ticked in the menu).
inline bool isStep(float scale, int percent) { return std::abs(scale - stepScale(percent)) < 0.004f; }
inline std::string stepLabel(int percent, bool fileSkin) {
    return std::to_string(percent) + " %" + (percent == 133 && fileSkin ? " (full size)" : "");
}
// Window size in points for a base size (dp or Built-in pixels).
inline int windowLength(int base, float scale, bool fileSkin) { return int(std::lround(double(base) * unit(fileSkin) * clamp(scale))); }
// The scale a window width corresponds to (a drag or a host resize), clamped to 50-200 %.
inline float scaleForWidth(int width, int baseWidth, bool fileSkin) {
    return clamp(float(width) / (float(baseWidth) * unit(fileSkin)));
}
// A file skin size saved under the old scale (1.0 = the full body size) on the new scale.
inline float fromLegacy(float oldScale) { return clamp(oldScale * 4.0f / 3.0f); }

// ---- the window fits the screen (F-32)
// Room left for the host's own window frame and title bar above the plugin window, in points.
constexpr int kHostFramePt = 32;
// The largest scale whose window fits the usable area of the display (availW x availH points; the host's frame is
// taken off the height), within 50-200 %.
inline float fitScale(int availW, int availH, int baseW, int baseH, bool fileSkin) {
    const float u = unit(fileSkin);
    const float s = std::min(float(availW) / (float(baseW) * u), float(std::max(1, availH - kHostFramePt)) / (float(baseH) * u));
    return std::min(kMax, std::max(kMin, s));
}
// Whether a Zoom step fits under the limit (its window is no larger than the limit's).
inline bool stepFits(int percent, float limit) { return stepScale(percent) <= limit + 1e-4f; }
// A scale that does not fit steps down to the largest Zoom step that does (50 % if none); one that fits is kept.
inline float fitted(float scale, float limit) {
    scale = clamp(scale);
    if (scale <= limit + 1e-4f) return scale;
    float best = kMin;
    for (int z : kSteps) if (stepFits(z, limit)) best = stepScale(z);
    return best;
}

}  // namespace winscale
