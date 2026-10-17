#include "ProgramEntry.h"

namespace {
constexpr int kTickMs = 125, kBlinkTicks = 2;    // blink at 4 Hz
constexpr juce::uint32 kTimeoutMs = 5000;
// Segments lit, bit 0 = a ... bit 6 = g (the display is active low: the bytes are inverted when shown).
constexpr uint8_t kDigit[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
constexpr uint8_t kP = 0x73, kUnderscore = 0x08, kDp = 0x80;
}

std::array<uint8_t, 4> ProgramEntry::render(int bank, const juce::String& program, bool blinkOn) {
    std::array<uint8_t, 4> lit{kP, 0, 0, 0};
    const uint8_t cursor = blinkOn ? kUnderscore : 0;
    lit[1] = bank > 0 ? uint8_t(kDigit[bank] | kDp) : cursor;
    for (int i = 0; i < 2; ++i) {
        if (i < program.length()) lit[size_t(2 + i)] = kDigit[program[i] - '0'];
        else lit[size_t(2 + i)] = bank > 0 ? cursor : 0;
    }
    for (auto& b : lit) b = uint8_t(~b);
    return lit;
}

void ProgramEntry::start() {
    active_ = true;
    bank_ = 0;
    program_.clear();
    blinkOn_ = true;
    ticks_ = 0;
    touch();
    show();
    startTimer(kTickMs);
}

void ProgramEntry::cancel() {
    if (!active_) return;
    active_ = false;
    stopTimer();
    proc_.setEntryDisplay(false, {});
}

void ProgramEntry::show() { proc_.setEntryDisplay(true, render(bank_, program_, blinkOn_)); }

void ProgramEntry::timerCallback() {
    if (juce::Time::getMillisecondCounter() - lastKeyMs_ > kTimeoutMs) { cancel(); return; }
    if (++ticks_ % kBlinkTicks == 0) { blinkOn_ = !blinkOn_; show(); }
}

bool ProgramEntry::keyPressed(const juce::KeyPress& k) {
    if (!active_) return false;
    touch();
    const juce::juce_wchar c = k.getTextCharacter();
    if (k == juce::KeyPress::escapeKey) { cancel(); return true; }
    if (k == juce::KeyPress::returnKey) {
        const int program = program_.getIntValue();
        if (bank_ > 0 && program >= 1 && program <= 50) {
            proc_.engine().selectProgram((bank_ - 1) * 50 + program - 1);
            cancel();
        }
        return true;                               // incomplete: nothing happens, the entry stays
    }
    if (k == juce::KeyPress::backspaceKey) {
        if (program_.isNotEmpty()) program_ = program_.dropLastCharacters(1);
        else bank_ = 0;
    } else if (c >= '0' && c <= '9') {
        const int d = int(c - '0');
        if (bank_ == 0) {
            if (d >= 1 && d <= 8) bank_ = d;           // banks 1-8
        } else if (program_.length() < 2) {
            const juce::String next = program_ + juce::String::charToString(c);
            const int v = next.getIntValue();
            if (next.length() == 1 ? d <= 5 : (v >= 1 && v <= 50)) program_ = next;   // programs 1-50
        }
    }
    // "p" / "P", dot and space are accepted and change nothing; anything else is ignored too
    blinkOn_ = true;
    ticks_ = 0;
    show();
    return true;
}
