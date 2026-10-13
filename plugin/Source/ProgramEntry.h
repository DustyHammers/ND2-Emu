// Typed program entry on the display (approved mockup): a click on the display starts it; the display shows "P" and
// blinks the places still to type; the bank (1-8) and the program (1-50) are typed ("p" or "P" first, a dot or space
// between, all optional: "p312", "P3.12", "3 12"); Return selects it (bank select + program change on the OS's global
// MIDI channel, as from a controller). Backspace deletes; Esc, a click elsewhere or 5 s without typing cancel.
// While it runs, the display shows the entry instead of the OS's display (ND2Processor::Port::segments).
// Message thread only.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class ProgramEntry : private juce::Timer {
public:
    explicit ProgramEntry(ND2Processor& p) : proc_(p) {}
    ~ProgramEntry() override { cancel(); }

    bool active() const { return active_; }
    void start();
    void cancel();
    bool keyPressed(const juce::KeyPress& k);   // true if used (always while active)

    // Display bytes for a typed text (active low, bit 7 = dp): "P" + bank + up to two program digits.
    static std::array<uint8_t, 4> render(int bank, const juce::String& program, bool blinkOn);

private:
    void timerCallback() override;
    void show();
    void touch() { lastKeyMs_ = juce::Time::getMillisecondCounter(); }

    ND2Processor& proc_;
    bool active_ = false, blinkOn_ = true;
    int bank_ = 0;                      // 1-8, 0 = not typed yet
    juce::String program_;              // 0-2 digits
    juce::uint32 lastKeyMs_ = 0;
    int ticks_ = 0;
};
