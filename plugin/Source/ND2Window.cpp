#include "ND2Window.h"
#include "BanksPanel.h"
#include "Banner.h"
#include "BankFiles.h"
#include "Resources.h"
#include "WindowScale.h"

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
const juce::String kMid = juce::String::fromUTF8(" \xc2\xb7 ");

// A setting's row: its name, what it is, and a control on the right (a button or a switch).
struct SettingRow : public juce::Component {
    juce::String name, detail;
    juce::Component* control = nullptr;
    float controlW = 0, controlH = 0;
    void setControl(juce::Component* c, float w, float h) { control = c; controlW = w; controlH = h; addAndMakeVisible(c); resized(); }
    void setDetail(const juce::String& d) { if (d != detail) { detail = d; repaint(); } }
    void resized() override {
        if (control != nullptr)
            control->setBounds(juce::Rectangle<float>(float(getWidth()) - 12 - controlW, (float(getHeight()) - controlH) / 2, controlW, controlH).toNearestInt());
    }
    void paint(juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat();
        ui::drawRow(g, r, ui::dp::fileRadius, false);
        ui::drawText(g, name, font(17, true), r.withTrimmedLeft(16).withWidth(210), ui::colours::text);
        ui::drawText(g, detail, font(16), r.withTrimmedLeft(240).withTrimmedRight(controlW + 30), ui::colours::text);
    }
};
}  // namespace


// ================================================================== SYSTEM tab

class ND2Window::SystemPage : public juce::Component {
public:
    SystemPage(ND2Window& w)
        : w_(w), os_(w.theme_, "Choose..."), factory_(w.theme_, "Load..."), show_(w.theme_, "Show"), debugger_(w.theme_) {
        version_.name = "Version";
        osRow_.name = "OS";
        factoryRow_.name = "Factory banks";
        dataRow_.name = "Data folder";
        debuggerRow_.name = "RmlUi debugger";
        for (auto* r : {&version_, &osRow_, &factoryRow_, &dataRow_, &debuggerRow_}) addAndMakeVisible(r);
        const float bh = ui::dp::button - 6;
        osRow_.setControl(&os_, ui::Button::idealWidth(w.theme_, "Choose..."), bh);
        factoryRow_.setControl(&factory_, ui::Button::idealWidth(w.theme_, "Load..."), bh);
        dataRow_.setControl(&show_, ui::Button::idealWidth(w.theme_, "Show"), bh);
        debuggerRow_.setControl(&debugger_, 40, 22);
        os_.onClick = [this] { if (w_.host_.chooseOs) w_.host_.chooseOs(); };
        factory_.onClick = [this] { if (w_.host_.chooseBanks) w_.host_.chooseBanks(); };
        show_.onClick = [] { Resources::get().dataFolder().createDirectory(); Resources::get().dataFolder().startAsProcess(); };
        debugger_.onChange = [this](bool on) { if (w_.host_.setDebugger) w_.host_.setDebugger(on); };
        refresh();
    }
    void refresh() {
        Resources& r = Resources::get();
        version_.setDetail(juce::String("ND2 ") + JucePlugin_VersionString + kMid
                           + juce::AudioProcessor::getWrapperTypeDescription(w_.proc_.wrapperType) + kMid + arch());
        osRow_.setDetail(r.locating() ? juce::String::fromUTF8("reading the update file\xe2\x80\xa6")
                         : r.hasOs() ? "found: OS v3.00 (" + bankfiles::shownFolder("os/os.cab") + ")"
                                     : "missing: choose the OS update file you downloaded");
        os_.setEnabled(!r.locating());
        factoryRow_.setDetail(r.building() ? juce::String::fromUTF8("loading\xe2\x80\xa6")
                              : r.hasFactory() ? "loaded (new instances start with them)"
                                               : r.hasOs() ? "not loaded: choose the factory bank download" : "needs the OS first");
        factory_.setEnabled(r.hasOs() && !r.building());
        dataRow_.setDetail(bankfiles::shownFolder(""));
        const bool dbg = w_.host_.debuggerAvailable && w_.host_.debuggerAvailable();
        debuggerRow_.setDetail(dbg ? "inspect the skin while it runs" : "not available (no skin drawn)");
        debugger_.setEnabled(dbg);
        debugger_.setOn(dbg && w_.host_.debuggerOn && w_.host_.debuggerOn());
    }
    juce::String text() const {
        juce::String s;
        for (const SettingRow* r : {&version_, &osRow_, &factoryRow_, &dataRow_, &debuggerRow_}) s << r->name << ": " << r->detail << "\n";
        return s;
    }
    void resized() override {
        const float w = std::min(float(getWidth()), 960.0f), rh = ui::dp::fileRow, gap = 8;
        float y = 30;
        for (auto* r : {&version_, &osRow_, &factoryRow_, &dataRow_}) { r->setBounds(juce::Rectangle<float>(0, y, w, rh).toNearestInt()); y += rh + gap; }
        y += 10;
        devY_ = y;
        y += 30;
        debuggerRow_.setBounds(juce::Rectangle<float>(0, y, w, rh).toNearestInt());
    }
    void paint(juce::Graphics& g) override {
        heading(g, "THIS INSTANCE", {0, 0, 960, 24});
        heading(g, "DEVELOPER", {0, devY_, 960, 24});
    }

private:
    static juce::String arch() {
       #if defined(__aarch64__) || defined(__arm64__)
        return "arm64";
       #elif defined(__x86_64__) || defined(_M_X64)
        return "x86_64";
       #else
        return "other";
       #endif
    }
    ND2Window& w_;
    SettingRow version_, osRow_, factoryRow_, dataRow_, debuggerRow_;
    ui::Button os_, factory_, show_;
    ui::Switch debugger_;
    float devY_ = 0;
};

// ================================================================== SKIN tab

class ND2Window::SkinPage : public juce::Component {
public:
    explicit SkinPage(ND2Window& w)
        : w_(w), reload_(w.theme_, "Reload skin (F5)"), folder_(w.theme_, "Show skins folder"), zoom_(w.theme_) {
        addAndMakeVisible(reload_);
        addAndMakeVisible(folder_);
        addAndMakeVisible(zoom_);
        juce::StringArray steps;
        for (int z : winscale::kSteps) steps.add(juce::String(z));
        zoom_.setItems(steps);
        reload_.onClick = [this] { if (w_.host_.reloadSkin) w_.host_.reloadSkin(); };
        folder_.onClick = [] { Resources::get().skinsFolder().createDirectory(); Resources::get().skinsFolder().startAsProcess(); };
        zoom_.onSelect = [this](int i) { if (w_.host_.setScale) w_.host_.setScale(winscale::stepScale(winscale::kSteps[i])); };
        refresh();
    }
    void refresh() {
        juce::StringArray items("Default");
        if (w_.host_.skinNames) items.addArray(w_.host_.skinNames());
        names_ = items;
        current_ = w_.host_.currentSkin ? w_.host_.currentSkin() : juce::String();
        const float limit = w_.host_.maxScale ? w_.host_.maxScale() : 2.0f, scale = w_.host_.scale ? w_.host_.scale() : 1.0f;
        int sel = -1, i = 0;
        for (int z : winscale::kSteps) {
            zoom_.setItemEnabled(i, winscale::stepFits(z, limit));
            if (winscale::isStep(scale, z)) sel = i;
            ++i;
        }
        zoom_.setSelected(sel);
        repaint();
    }
    juce::StringArray items() const {
        juce::StringArray a;
        for (const auto& n : names_) a.add((n == current_ ? "* " : "") + n);
        return a;
    }
    ui::Segmented& zoom() { return zoom_; }
    void resized() override {
        const float x = kListW + ui::dp::gap * 2;
        reload_.setBounds(juce::Rectangle<float>(x, 30, ui::Button::idealWidth(w_.theme_, reload_.label()), ui::dp::button).toNearestInt());
        folder_.setBounds(juce::Rectangle<float>(float(reload_.getRight()) + 10, 30, ui::Button::idealWidth(w_.theme_, folder_.label()), ui::dp::button).toNearestInt());
        zoom_.setBounds(juce::Rectangle<float>(x, 140, zoom_.idealWidth(), ui::dp::button).toNearestInt());
    }
    void paint(juce::Graphics& g) override {
        heading(g, "SKINS", {0, 0, kListW, 24});
        for (int i = 0; i < names_.size(); ++i) {
            const auto r = rowBounds(i);
            const bool sel = names_[i] == current_;
            ui::drawRow(g, r, ui::dp::fileRadius, sel, i == hover_);
            if (sel) ui::drawTick(g, r.withWidth(40).reduced(12), ui::colours::text, 2.0f);
            ui::drawText(g, names_[i] + (i > 0 ? " (from " + bankfiles::shownFolder("skins") + ")" : juce::String()), font(17),
                         r.withTrimmedLeft(44).withTrimmedRight(10), ui::colours::text);
        }
        g.setColour(ui::colours::line);
        g.fillRect(kListW + ui::dp::gap, 0.0f, 1.0f, float(getHeight()));
        const float x = kListW + ui::dp::gap * 2;
        heading(g, "SKIN FILES", {x, 0, 600, 24});
        heading(g, "ZOOM (%)", {x, 110, 600, 24});
        ui::drawWrapped(g, "Sizes that don't fit your screen are greyed out. You can also drag the plugin's bottom-right corner.",
                        font(16), {x, 186, 700, 60}, ui::colours::muted);
    }
    void mouseMove(const juce::MouseEvent& e) override { const int h = rowAt(e.position); if (h != hover_) { hover_ = h; repaint(); } }
    void mouseExit(const juce::MouseEvent&) override { hover_ = -1; repaint(); }
    void mouseUp(const juce::MouseEvent& e) override {
        const int i = rowAt(e.position);
        if (i >= 0 && w_.host_.chooseSkin) w_.host_.chooseSkin(names_[i]);
    }

private:
    static constexpr float kListW = 560;
    juce::Rectangle<float> rowBounds(int i) const { return {0, 30 + float(i) * (ui::dp::fileRow + 6), kListW, ui::dp::fileRow}; }
    int rowAt(juce::Point<float> p) const {
        for (int i = 0; i < names_.size(); ++i) if (rowBounds(i).contains(p)) return i;
        return -1;
    }
    ND2Window& w_;
    ui::Button reload_, folder_;
    ui::Segmented zoom_;
    juce::StringArray names_;
    juce::String current_;
    int hover_ = -1;
};

// ================================================================== BANKS without the OS

class ND2Window::NoOsPage : public juce::Component {
public:
    void paint(juce::Graphics& g) override {
        ui::drawWrapped(g, "The banks need the OS: choose the OS update file on the SYSTEM tab.", font(18), getLocalBounds().toFloat().withHeight(60),
                        ui::colours::muted);
    }
};

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

ND2Window::ND2Window(ND2Processor& p, const ui::Theme& theme, Host host)
    : proc_(p), theme_(theme), host_(std::move(host)), frame_(theme, "ND2", {"SYSTEM", "BANKS", "SKIN", "MIDI"}) {
    setWantsKeyboardFocus(true);
    addAndMakeVisible(frame_);
    system_ = std::make_unique<SystemPage>(*this);
    skin_ = std::make_unique<SkinPage>(*this);
    midi_ = std::make_unique<MidiPage>(p, theme);
    if (Resources::get().hasOs()) {
        banks_ = std::make_unique<BanksPanel>(p, theme);
        banks_->onFooterChanged = [this] { if (tab_ == kBanks) showFooter(); };
    } else {
        noOs_ = std::make_unique<NoOsPage>();
    }
    for (int t = 0; t < 4; ++t) frame_.addChildComponent(page(t));
    midi_->onChanged = [this] { if (tab_ == kMidi) showFooter(); };
    midi_->update();
    frame_.tabs().onSelect = [this](int t) { showTab(t); };
    frame_.closeButton().onClick = [this] { if (host_.close) host_.close(); };
    frame_.setBounds(0, 0, int(kLayoutW), int(kLayoutH));
    for (int t = 0; t < 4; ++t) page(t)->setBounds(frame_.bodyBounds());
    showTab(kBanks);
    startTimerHz(10);
}

ND2Window::~ND2Window() {
    stopTimer();
    proc_.banksOpen = false;
}

juce::Component* ND2Window::page(int t) {
    switch (t) {
        case kSystem: return system_.get();
        case kBanks: return banks_ ? static_cast<juce::Component*>(banks_.get()) : noOs_.get();
        case kSkin: return skin_.get();
        default: return midi_.get();
    }
}

ui::Switch& ND2Window::midiSwitch() { return midi_->out; }
juce::String ND2Window::midiChannelsText() const { return midi_->channelsText(); }
juce::String ND2Window::systemText() const { return system_->text(); }
juce::StringArray ND2Window::skinItems() const { return skin_->items(); }
ui::Segmented& ND2Window::zoom() { return skin_->zoom(); }

void ND2Window::showTab(int tab) {
    tab_ = juce::jlimit(0, 3, tab);
    frame_.tabs().setSelected(tab_);
    for (int t = 0; t < 4; ++t) page(t)->setVisible(t == tab_);
    proc_.banksOpen = tab_ == kBanks;                 // a skin's BANKS label, if it has one, lit
    refresh();
}

void ND2Window::refresh() {
    system_->refresh();
    skin_->refresh();
    showFooter();
}

void ND2Window::showFooter() {
    switch (tab_) {
        case kBanks:
            if (banks_) {
                BanksPanel::Line l = banks_->footerLine();
                frame_.footer().show(std::move(l.parts), std::move(l.actions));
            } else {
                frame_.footer().show({{"No OS yet.", false, true}});
            }
            break;
        case kSystem: {
            Resources& r = Resources::get();
            frame_.footer().show({{r.hasOs() ? "OS v3.00 found. " : "No OS yet. ", false, false},
                                  {r.hasFactory() ? "Factory banks loaded." : "No factory banks yet.", false, true}});
            break;
        }
        case kSkin:
            frame_.footer().show({{"Skin: " + (host_.currentSkin ? host_.currentSkin() : juce::String()) + ". ", false, false},
                                  {"The skin is the same in every project; the size is saved with each project too.", false, true}});
            break;
        default:
            frame_.footer().show({{proc_.midiToHost() ? "MIDI out to the host is on. " : "MIDI out to the host is off. ", false, false},
                                  {"Every sound setting has a CC (chart on the right).", false, true}});
    }
}

void ND2Window::place(juce::Rectangle<int> area, float pointsPerDp) {
    pointsPerDp_ = pointsPerDp > 0 ? pointsPerDp : 1.0f;
    setBounds(area);
    placement_ = ui::placeCentred(getLocalBounds(), pointsPerDp_, {kLayoutW, kLayoutH}, kDrawnWidth, ui::dp::inset);
    frame_.setTransform(juce::AffineTransform::scale(placement_.scale).translated(placement_.bounds.getX(), placement_.bounds.getY()));
    repaint();
}

void ND2Window::resized() {}

void ND2Window::paint(juce::Graphics& g) {
    ui::drawBackdrop(g, getLocalBounds().toFloat(), placement_.bounds, pointsPerDp_ * placement_.fit);
}

// The plugin area's bottom-right corner stays with the skin (its resize grip).
bool ND2Window::hitTest(int x, int y) { return !(x >= getWidth() - kGripSize && y >= getHeight() - kGripSize); }

bool ND2Window::cancelQuestion() { return tab_ == kBanks && banks_ && banks_->cancelQuestion(); }

// A click on the dimmed area: as Esc (a question in the footer cancelled first, then the window closed).
void ND2Window::mouseDown(const juce::MouseEvent&) {
    if (cancelQuestion()) grabKeyboardFocus();
    else if (host_.close) host_.close();
}

bool ND2Window::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) {
        if (!cancelQuestion() && host_.close) host_.close();
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::F5Key) { if (host_.reloadSkin) host_.reloadSkin(); return true; }
    return false;
}

void ND2Window::timerCallback() {
    if (banks_) banks_->tick();
    midi_->update();
    system_->refresh();
    if (midi_->out.isOn() != proc_.midiToHost()) { midi_->out.setOn(proc_.midiToHost()); if (tab_ == kMidi) showFooter(); }
}
