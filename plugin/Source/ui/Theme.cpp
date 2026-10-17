#include "ui/Theme.h"

namespace ui {

juce::Font Theme::font(float px, bool bold) const {
    if (typeface != nullptr) return juce::Font(juce::FontOptions(typeface).withHeight(px));
    return juce::Font(juce::FontOptions(px, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Typeface::Ptr Theme::loadTypeface(const juce::File& file) {
    if (!file.existsAsFile() || file.getSize() <= 0 || file.getSize() > (64 << 20)) return nullptr;
    juce::MemoryBlock data;
    if (!file.loadFileAsData(data) || data.getSize() < 12) return nullptr;
    // TrueType, OpenType (CFF), TrueType collections and Apple's 'true': anything else is not a font file.
    const auto* b = static_cast<const uint8_t*>(data.getData());
    const uint32_t tag = uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
    if (tag != 0x00010000 && tag != 0x4F54544F && tag != 0x74746366 && tag != 0x74727565) return nullptr;
    return juce::Typeface::createSystemTypefaceFor(data.getData(), data.getSize());
}

float textWidth(const juce::Font& font, const juce::String& text) { return juce::GlyphArrangement::getStringWidth(font, text); }

juce::String ellipsize(const juce::String& text, const juce::Font& font, float width) {
    if (textWidth(font, text) <= width) return text;
    const juce::String dots = juce::String::fromUTF8("\xe2\x80\xa6");
    int lo = 0, hi = text.length();                  // the longest prefix that fits with the dots
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (textWidth(font, text.substring(0, mid).trimEnd() + dots) <= width) lo = mid; else hi = mid - 1;
    }
    return lo > 0 ? text.substring(0, lo).trimEnd() + dots : (textWidth(font, dots) <= width ? dots : juce::String());
}

void drawText(juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Rectangle<float> area, juce::Colour colour,
              juce::Justification just) {
    if (area.getWidth() <= 0 || text.isEmpty()) return;
    g.setFont(font);
    g.setColour(colour);
    g.drawText(ellipsize(text, font, area.getWidth()), area, just, false);
}

namespace {
juce::GlyphArrangement wrapped(const juce::String& text, const juce::Font& font, float x, float y, float width) {
    juce::GlyphArrangement ga;
    ga.addJustifiedText(font, text, x, y + font.getAscent(), std::max(1.0f, width), juce::Justification::topLeft, 0.0f);
    return ga;
}
}  // namespace

float wrappedHeight(const juce::String& text, const juce::Font& font, float width) {
    if (text.isEmpty()) return 0;
    const juce::GlyphArrangement ga = wrapped(text, font, 0, 0, width);
    return std::max(font.getHeight(), ga.getBoundingBox(0, -1, true).getBottom());
}

void drawWrapped(juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Rectangle<float> area, juce::Colour colour) {
    if (text.isEmpty() || area.getWidth() <= 0) return;
    g.setColour(colour);
    wrapped(text, font, area.getX(), area.getY(), area.getWidth()).draw(g);
}

juce::Path arrowPath(juce::Rectangle<float> box) {
    const float s = std::min(box.getWidth(), box.getHeight());
    const auto c = box.getCentre();
    juce::Path p;
    p.addTriangle(c.x - s * 0.25f, c.y - s * 0.32f, c.x - s * 0.25f, c.y + s * 0.32f, c.x + s * 0.3f, c.y);
    return p;
}

void drawArrow(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour colour) {
    g.setColour(colour);
    g.fillPath(arrowPath(box));
}

void drawCross(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour colour, float thickness) {
    const float s = std::min(box.getWidth(), box.getHeight()) * 0.5f;
    const auto c = box.getCentre();
    juce::Path p;
    p.startNewSubPath(c.x - s, c.y - s); p.lineTo(c.x + s, c.y + s);
    p.startNewSubPath(c.x + s, c.y - s); p.lineTo(c.x - s, c.y + s);
    g.setColour(colour);
    g.strokePath(p, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void drawTick(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour colour, float thickness) {
    const float s = std::min(box.getWidth(), box.getHeight());
    const auto c = box.getCentre();
    juce::Path p;
    p.startNewSubPath(c.x - s * 0.36f, c.y + s * 0.02f);
    p.lineTo(c.x - s * 0.1f, c.y + s * 0.28f);
    p.lineTo(c.x + s * 0.38f, c.y - s * 0.26f);
    g.setColour(colour);
    g.strokePath(p, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void drawDot(juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour) {
    g.setColour(colour);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2);
}

void drawLed(juce::Graphics& g, juce::Point<float> centre, float radius, bool lit) {
    if (lit) {
        juce::ColourGradient glow(colours::led.withAlpha(0.45f), centre.x, centre.y, colours::led.withAlpha(0.0f), centre.x + radius * 3, centre.y, true);
        g.setGradientFill(glow);
        g.fillEllipse(centre.x - radius * 3, centre.y - radius * 3, radius * 6, radius * 6);
        drawDot(g, centre, radius, colours::led);
    } else {
        drawDot(g, centre, radius, colours::led.withAlpha(0.18f));
        g.setColour(colours::led.withAlpha(0.35f));
        g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2, 1.0f);
    }
}

}  // namespace ui
