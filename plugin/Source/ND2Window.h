// The ND2 window (approved mockup): over the whole plugin, with the tabs BANKS (BanksPanel) and MIDI.
//   MIDI: "Send ND2's MIDI out to the host" (saved with the project), the OS's MIDI channels (read from the OS, shown
//         only), the CC chart of the panel's settings.
// Opened by the skin's BANKS label and the right-click menu (Banks..., MIDI...); closed by the round button or Esc.
// The footer carries the tab's line (messages, questions, progress). Laid out in skin dp (1725 x 1035 body) and scaled
// with the window. The machine keeps running underneath.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include "PluginProcessor.h"
#include "ui/Widgets.h"

class BanksPanel;

class ND2Window : public juce::Component, private juce::Timer {
public:
    enum Tab { kBanks = 0, kMidi = 1 };
    ND2Window(ND2Processor& p, const ui::Theme& theme);
    ~ND2Window() override;

    std::function<void()> onClose;
    void showTab(int tab);
    int tab() const { return tab_; }
    void place(juce::Rectangle<int> parentBounds, float pxPerDp);   // over the whole plugin area

    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    // Tests
    ui::Frame& frame() { return frame_; }
    BanksPanel& banks() { return *banks_; }
    ui::Switch& midiSwitch();                        // MIDI tab: MIDI out to the host
    juce::String midiChannelsText() const;           // MIDI tab: the channels as shown ("global 10; MIDI channels 1-6")

private:
    class MidiPage;
    void timerCallback() override;
    void showFooter();

    ND2Processor& proc_;
    const ui::Theme& theme_;
    ui::Frame frame_;
    std::unique_ptr<BanksPanel> banks_;
    std::unique_ptr<MidiPage> midi_;
    int tab_ = kBanks;
};
