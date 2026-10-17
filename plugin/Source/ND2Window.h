// The ND2 window (approved mockups): everything the right-click menu had, in four tabs.
//   SYSTEM: version and format, the OS (Choose...: the OS update file), the factory banks (Load...), the data folder
//           (Show); Developer: the RmlUi debugger.
//   BANKS:  the bank library and the 8 banks (BanksPanel).
//   SKIN:   the skins (click one), Reload skin (F5), Show skins folder, Zoom.
//   MIDI:   MIDI out to the host (saved with the project), the OS's MIDI channels (shown only), the CC chart.
// Opened by a right-click on the panel (on the tab used last; the first time BANKS). As the owner's other plugin's
// window: laid out at kLayout dp, drawn kDrawnWidth dp of the skin wide, centred in the plugin area over a dimmed
// backdrop with the window's shadow (scaled down only when the plugin is too small for it). Esc, the round button or a
// click on the dimmed area closes it (a question in the footer is cancelled first). The panel gets no mouse events
// while it is open, except the bottom-right corner (the resize grip). The footer carries the tab's line (messages,
// questions, progress). The machine keeps running underneath.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include "PluginProcessor.h"
#include "ui/Widgets.h"

class BanksPanel;

class ND2Window : public juce::Component, private juce::Timer {
public:
    enum Tab { kSystem = 0, kBanks = 1, kSkin = 2, kMidi = 3 };
    static constexpr float kLayoutW = 1500, kLayoutH = 900;   // dp the pages are laid out in
    static constexpr float kDrawnWidth = 1300;              // dp of the skin (1725 wide) the window is drawn
    static constexpr int kGripSize = 24;                     // points: the plugin's bottom-right corner stays with the skin

    // What the window asks of the plugin window (the editor).
    struct Host {
        std::function<void()> close, chooseOs, chooseBanks, reloadSkin;
        std::function<juce::StringArray()> skinNames;          // the user's skins (Default not included)
        std::function<juce::String()> currentSkin;
        std::function<void(const juce::String&)> chooseSkin;
        std::function<float()> scale, maxScale;
        std::function<void(float)> setScale;
        std::function<bool()> debuggerAvailable, debuggerOn;
        std::function<void(bool)> setDebugger;
    };

    ND2Window(ND2Processor& p, const ui::Theme& theme, Host host);
    ~ND2Window() override;

    void showTab(int tab);
    int tab() const { return tab_; }
    void place(juce::Rectangle<int> area, float pointsPerDp);   // the whole plugin area, its points per skin dp
    void refresh();                                  // re-read what the tabs show (a skin, the zoom, the OS changed)

    void resized() override;
    void paint(juce::Graphics&) override;
    bool hitTest(int x, int y) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

    // Tests
    ui::Frame& frame() { return frame_; }
    juce::Rectangle<float> windowBounds() const { return placement_.bounds; }
    ui::Switch& midiSwitch();                        // MIDI tab: MIDI out to the host
    juce::String midiChannelsText() const;           // MIDI tab: the channels as shown ("global 10; MIDI channels 1-6")
    juce::String systemText() const;                 // SYSTEM tab: its rows as shown, one per line
    juce::StringArray skinItems() const;             // SKIN tab: the list as shown
    ui::Segmented& zoom();                           // SKIN tab: the zoom steps

private:
    class SystemPage;
    class SkinPage;
    class MidiPage;
    class NoOsPage;
    void timerCallback() override;
    void showFooter();
    bool cancelQuestion();
    juce::Component* page(int tab);

    ND2Processor& proc_;
    const ui::Theme& theme_;
    Host host_;
    ui::Frame frame_;
    std::unique_ptr<SystemPage> system_;
    std::unique_ptr<BanksPanel> banks_;               // with the OS only
    std::unique_ptr<NoOsPage> noOs_;                  // BANKS without the OS
    std::unique_ptr<SkinPage> skin_;
    std::unique_ptr<MidiPage> midi_;
    int tab_ = kBanks;
    float pointsPerDp_ = 1.0f;
    ui::Placement placement_;
};
