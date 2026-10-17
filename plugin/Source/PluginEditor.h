// The plugin window: the chosen skin (the Default skin built into the plugin, or one from ~/Documents/ND2/skins/)
// at the chosen size, the message banners (OS update file, factory presets), the ND2 window that a right-click opens
// (SYSTEM, BANKS, SKIN and MIDI tabs: everything the old right-click menu had; ND2Window) and typed program entry on
// the display (ProgramEntry). Approved mockups.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "Banner.h"
#include "PluginProcessor.h"
#include "ui/Widgets.h"

class RmlSkinComponent;
class ND2Window;
class ProgramEntry;

// Shown only when no skin can be drawn (e.g. OpenGL is not available): why, the banners and the menu.
class FallbackView : public juce::Component {
public:
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void setReason(const juce::String& r) { reason_ = r; repaint(); }
    void setBanners(std::vector<Banner> b) { banners_ = std::move(b); repaint(); }
    std::function<void(const juce::MouseEvent&)> onRightClick;
private:
    juce::String reason_;
    std::vector<Banner> banners_;
    std::vector<banner::Hit> hits_;
};

class ND2Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit ND2Editor(ND2Processor&);
    ~ND2Editor() override;
    void resized() override;
    float maxScale() const;                          // the largest scale that fits the screen (50-200 %)

private:
    juce::StringArray skinNames() const;             // the user's skins in ~/Documents/ND2/skins/
    void showSkin(const juce::String& name, const juce::String& notice = {});
    void skinFailed(const juce::String& name, const juce::String& reason);
    void showFallback(const juce::String& reason);
    void applyScale(float scale);
    void cornerResize(int phase, juce::Point<int> offset);
    void chooseOs();
    void chooseBanks();
    void openWindow(int tab);                        // the ND2 window on a tab (a right-click: the tab used last)
    void closeWindow();
    void placeWindow();
    void message(const juce::String& text);          // a message for a few seconds (in whatever view is shown)
    std::vector<Banner> banners();                   // what is missing or under way right now
    void timerCallback() override;                   // the banners; saves the window size

    ND2Processor& proc;
    std::unique_ptr<RmlSkinComponent> rml_;
    std::unique_ptr<FallbackView> fallback_;
    ui::Theme theme_;                                // the ND2 window's and the menu's look
    std::unique_ptr<ND2Window> window_;
    std::unique_ptr<ProgramEntry> entry_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String current_;                           // the skin being shown
    float scale_ = 1.0f;
    bool applying_ = false, scaleDirty_ = false, debugger_ = false;
    juce::ComponentBoundsConstrainer constrainer_;
    juce::Rectangle<int> dragStartBounds_;
};
