#include "ND2Window.h"
#include "BanksPanel.h"
#include "Banner.h"

namespace {
juce::Font font(float size, bool bold = false) { return banner::font(size, bold); }

void heading(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r) {
    ui::drawText(g, text, font(ui::dp::headingText, true).withExtraKerningFactor(0.12f), r, ui::colours::heading);
}

// The panel's settings and their CCs (docs/MIDI_CC.md, measured in the emulator): each button's setting and, under
// it, the one with LOWER PARAM held (the red label).
struct Cc { const char* name; const char* cc; };
const Cc kUpper[16] = {{"FILTER", "14"}, {"FILT TYPE", "15"}, {"DYN FILTER", "16"}, {"FILT RES", "17"}, {"ATK/RATE", "18"},
                       {"ENV MODE", "19"}, {"DECAY (noise)", "21"}, {"TYPE/DYN", "22"}, {"DISTORT", "23"}, {"TYPE", "24"},
                       {"EQ FREQ", "25"}, {"GAIN", "26"}, {"REPEAT", "27"}, {"AMOUNT", "28"}, {"LEVEL", "7"}, {"PAN", "10"}};
const Cc kLower[16] = {{"SPECTRA", "30"}, {"WAVE", "46"}, {"DYN TIMB", "53"}, {"TIMBRE", "52"}, {"HI DECAY", "47"},
                       {"PUNCH", "48"}, {"DECAY (tone)", "50"}, {"TYPE/DYN", "51"}, {"PITCH", "31 (+63)"}, {"", ""},
                       {"BEND", "54"}, {"TIME", "55"}, {"CLICK LEVEL", "56"}, {"TYPE", "57"}, {"MIX", "58"}, {"", ""}};
constexpr float kLeftW = 640;
}  // namespace

// ================================================================== MIDI tab

class ND2Window::MidiPage : public juce::Component {
public:
    MidiPage(ND2Processor& p, const ui::Theme& theme) : proc_(p), out(theme) {
        addAndMakeVisible(out);
        out.setOn(proc_.midiToHost());
        out.onChange = [this](bool on) { proc_.setMidiToHost(on); if (onChanged) onChanged(); };
    }
    std::function<void()> onChanged;
    ui::Switch out;

    // the OS's channels, as text; true if they changed since the last call
    bool update() {
        const int g = proc_.engine().globalMidiChannel();
        juce::String global = g >= 0 ? juce::String(g + 1) : juce::String::fromUTF8("\xe2\x80\xa6");
        juce::StringArray chans;
        bool sequential = true, known = true;
        for (int c = 0; c < nd2::Engine::kPads; ++c) {
            const int m = proc_.engine().channelMidiChannel(c);
            known = known && m >= 0;
            sequential = sequential && m == c;
            chans.add(m >= 0 ? juce::String(m + 1) : "-");
        }
        juce::String drum = !known ? juce::String::fromUTF8("\xe2\x80\xa6") : sequential ? juce::String("MIDI channels 1-6") : "MIDI channels " + chans.joinIntoString(", ");
        const bool changed = global != global_ || drum != drum_;
        global_ = global;
        drum_ = drum;
        if (changed) repaint();
        return changed;
    }
    juce::String channelsText() const { return "global " + global_ + "; " + drum_; }   // tests

    void resized() override { out.setBounds(0, 66, 40, 22); }

    void paint(juce::Graphics& g) override {
        const float gx = kLeftW + ui::dp::gap * 2;
        heading(g, "MIDI OUT", {0, 30, kLeftW, 24});
        ui::drawText(g, "Send ND2's MIDI out to the host", font(19), {54, 63, kLeftW - 54, 28}, ui::colours::text);
        ui::drawWrapped(g, "Panel moves go out as CCs, pad hits as notes, program changes made on the panel as bank select and "
                           "program change, so the host can record them. Turn it off if your host routes ND2's MIDI out back into it.",
                        font(16), {54, 100, kLeftW - 54, 80}, ui::colours::muted);
        heading(g, "MIDI CHANNELS", {0, 210, kLeftW, 24});
        const juce::String rows[2][2] = {{"Global channel", global_ + ": program changes, CCs for the selected drum channel"},
                                         {"Drum channels 1-6", drum_ + ": each channel's notes and CCs"}};
        for (int i = 0; i < 2; ++i) {
            const juce::Rectangle<float> r(0, 244 + float(i) * (ui::dp::fileRow + 8), kLeftW, ui::dp::fileRow);
            ui::drawRow(g, r, ui::dp::fileRadius, false);
            ui::drawText(g, rows[i][0], font(17, true), r.withTrimmedLeft(16).withWidth(190), ui::colours::text);
            ui::drawText(g, rows[i][1], font(16), r.withTrimmedLeft(210).withTrimmedRight(12), ui::colours::text);
        }
        ui::drawText(g, "As set in the OS (Shift + MIDI on the panel).", font(16), {0, 350, kLeftW, 24}, ui::colours::muted);
        g.setColour(ui::colours::line);
        g.fillRect(kLeftW + ui::dp::gap, 0.0f, 1.0f, float(getHeight()));
        heading(g, "CC CHART", {gx, 30, 400, 24});
        const float cw = (float(getWidth()) - gx) / 2;
        for (int c = 0; c < 2; ++c) {
            ui::drawText(g, c ? "LOWER ROW" : "UPPER ROW", font(14, true), {gx + float(c) * cw, 60, cw, 22}, ui::colours::muted);
            for (int i = 0; i < 16; ++i) {
                const Cc& e = c ? kLower[i] : kUpper[i];
                if (i % 2 == 0) ui::drawRow(g, {gx + float(c) * cw, 88 + float(i) * 34, cw - 16, 64}, 6, false);
                if (!*e.name) continue;
                const juce::Rectangle<float> r(gx + float(c) * cw, 88 + float(i) * 34, cw - 16, 30);
                ui::drawText(g, e.name, font(16, i % 2 == 0), r.withTrimmedLeft(12), i % 2 ? ui::colours::muted : ui::colours::text);
                ui::drawText(g, juce::String("CC ") + e.cc, font(16), r.withTrimmedRight(12), ui::colours::muted, juce::Justification::centredRight);
            }
        }
    }

private:
    ND2Processor& proc_;
    juce::String global_, drum_;
};

// ================================================================== window

ND2Window::ND2Window(ND2Processor& p, const ui::Theme& theme)
    : proc_(p), theme_(theme), frame_(theme, "ND2", {"BANKS", "MIDI"}) {
    setWantsKeyboardFocus(true);
    addAndMakeVisible(frame_);
    banks_ = std::make_unique<BanksPanel>(p, theme);
    midi_ = std::make_unique<MidiPage>(p, theme);
    frame_.addChildComponent(*banks_);
    frame_.addChildComponent(*midi_);
    banks_->onFooterChanged = [this] { if (tab_ == kBanks) showFooter(); };
    midi_->onChanged = [this] { if (tab_ == kMidi) showFooter(); };
    midi_->update();
    frame_.tabs().onSelect = [this](int t) { showTab(t); };
    frame_.closeButton().onClick = [this] { if (onClose) onClose(); };
    frame_.onBodyResized = [this] {
        banks_->setBounds(frame_.bodyBounds());
        midi_->setBounds(frame_.bodyBounds());
    };
    showTab(kBanks);
    startTimerHz(10);
}

ui::Switch& ND2Window::midiSwitch() { return midi_->out; }
juce::String ND2Window::midiChannelsText() const { return midi_->channelsText(); }

ND2Window::~ND2Window() {
    stopTimer();
    proc_.banksOpen = false;
}

void ND2Window::showTab(int tab) {
    tab_ = tab == kMidi ? kMidi : kBanks;
    frame_.tabs().setSelected(tab_);
    banks_->setVisible(tab_ == kBanks);
    midi_->setVisible(tab_ == kMidi);
    proc_.banksOpen = tab_ == kBanks;                 // the BANKS label lit
    showFooter();
}

void ND2Window::showFooter() {
    if (tab_ == kBanks) {
        BanksPanel::Line l = banks_->footerLine();
        frame_.footer().show(std::move(l.parts), std::move(l.actions));
    } else {
        frame_.footer().show({{proc_.midiToHost() ? "MIDI out to the host is on. " : "MIDI out to the host is off. ", false, false},
                              {"Every sound setting has a CC (chart on the right).", false, true}});
    }
}

void ND2Window::place(juce::Rectangle<int> parent, float z) {
    setTransform(juce::AffineTransform::scale(z).translated(float(parent.getX()), float(parent.getY())));
    setBounds(0, 0, juce::roundToInt(float(parent.getWidth()) / z), juce::roundToInt(float(parent.getHeight()) / z));
}

void ND2Window::resized() { frame_.setBounds(getLocalBounds()); }

bool ND2Window::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) {
        if (tab_ == kBanks && banks_->cancelQuestion()) return true;
        if (onClose) onClose();
        return true;
    }
    return false;
}

void ND2Window::timerCallback() {
    banks_->tick();
    midi_->update();
    if (midi_->out.isOn() != proc_.midiToHost()) { midi_->out.setOn(proc_.midiToHost()); if (tab_ == kMidi) showFooter(); }
}
