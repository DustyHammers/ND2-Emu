// The message banners across the top of the plugin window (approved mockup): dark, red outline, white text, red
// buttons on the right and an optional plain "Not now". Drawn with JUCE over the skin.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

struct BannerButton {
    juce::String label;
    std::function<void()> action;
    bool plain = false;                                // a quiet text link ("Not now") instead of a red button
};

struct Banner {
    juce::String title, text;                          // title in bold, then the text
    std::vector<BannerButton> buttons;
    bool operator==(const Banner& o) const {
        if (title != o.title || text != o.text || buttons.size() != o.buttons.size()) return false;
        for (size_t i = 0; i < buttons.size(); ++i) if (buttons[i].label != o.buttons[i].label) return false;
        return true;
    }
    bool operator!=(const Banner& o) const { return !(*this == o); }
};

namespace banner {

constexpr int kHeight = 48, kMargin = 9, kGap = 6;

inline juce::Font font(float size, bool bold) { return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain)); }

// Draws the banners from the top of `area` down; returns each button's rectangle (banner index, button index).
struct Hit { int banner, button; juce::Rectangle<int> r; };
inline std::vector<Hit> draw(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<Banner>& banners) {
    std::vector<Hit> hits;
    int y = area.getY() + kMargin;
    for (size_t b = 0; b < banners.size(); ++b) {
        const Banner& ban = banners[b];
        auto r = juce::Rectangle<int>(area.getX() + kMargin, y, area.getWidth() - 2 * kMargin, kHeight);
        y += kHeight + kGap;
        g.setColour(juce::Colour(0xe0181818));
        g.fillRoundedRectangle(r.toFloat(), 6);
        g.setColour(juce::Colour(0xffff3020));
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 6, 1);
        auto inner = r.reduced(14, 0);
        for (int i = int(ban.buttons.size()) - 1; i >= 0; --i) {     // buttons from the right
            const BannerButton& bt = ban.buttons[size_t(i)];
            const auto f = font(bt.plain ? 12.0f : 12.5f, !bt.plain);
            const int w = juce::GlyphArrangement::getStringWidthInt(f, bt.label) + (bt.plain ? 8 : 24);
            auto br = inner.removeFromRight(w).withSizeKeepingCentre(w, 28);
            inner.removeFromRight(8);
            if (!bt.plain) {
                g.setColour(juce::Colour(0xffe3262d));
                g.fillRoundedRectangle(br.toFloat(), 4);
                g.setColour(juce::Colours::white);
            } else {
                g.setColour(juce::Colour(0xffaaaaaa));
            }
            g.setFont(f);
            g.drawText(bt.label, br, juce::Justification::centred);
            hits.push_back({int(b), i, br});
        }
        juce::AttributedString s;
        s.append(ban.title + (ban.title.isNotEmpty() ? " " : ""), font(13.0f, true), juce::Colours::white);
        s.append(ban.text, font(13.0f, false), juce::Colours::white);
        s.setJustification(juce::Justification::centredLeft);
        s.setWordWrap(juce::AttributedString::byWord);
        s.draw(g, inner.reduced(0, 4).toFloat());
    }
    return hits;
}

inline int height(size_t count) { return count == 0 ? 0 : kMargin + int(count) * (kHeight + kGap); }

}  // namespace banner
