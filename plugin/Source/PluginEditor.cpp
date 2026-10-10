#include "PluginEditor.h"
#include "Resources.h"
#include "RmlSkinComponent.h"
#include "SkinSettings.h"
#include "skin_discovery.h"
#include "skin_view.h"

namespace {
const juce::String kFolderNote = " (from ~/Documents/ND2/skins)";
}

// ================================================================== fallback view

void FallbackView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff2b2b2e));
    g.setColour(juce::Colours::white);
    g.setFont(banner::font(15.0f, true));
    auto area = getLocalBounds().reduced(30);
    area.removeFromTop(banner::height(banners_.size()));
    g.drawFittedText("ND2", area.removeFromTop(40), juce::Justification::centredLeft, 1);
    g.setFont(banner::font(13.0f, false));
    g.drawFittedText(reason_ + "\n\nRight-click for the menu.", area, juce::Justification::topLeft, 8);
    hits_ = banner::draw(g, getLocalBounds(), banners_);
}

void FallbackView::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(e); return; }
    for (const banner::Hit& h : hits_)
        if (h.r.contains(e.getPosition()))
            if (auto f = banners_[size_t(h.banner)].buttons[size_t(h.button)].action) { juce::MessageManager::callAsync(f); return; }
}

// ================================================================== editor

ND2Editor::ND2Editor(ND2Processor& p) : AudioProcessorEditor(&p), proc(p) {
    logFile_ = Resources::get().dataFolder().getChildFile("skin-log.txt");
    setConstrainer(&constrainer_);
    setResizable(true, false);                      // our own corner drag (JUCE's corner would sit under the OpenGL view)
    const float projectScale = proc.editorScale.load();
    scale_ = projectScale >= winscale::kMin && projectScale <= winscale::kMax ? projectScale : SkinSettings::scale();
    applying_ = true;
    setSize(winscale::windowLength(int(skin::kBodyWidthDp), scale_, true), winscale::windowLength(int(skin::kBodyHeightDp), scale_, true));
    applying_ = false;
    showSkin(SkinSettings::skin());
    startTimer(250);
}

ND2Editor::~ND2Editor() {
    stopTimer();
    if (scaleDirty_) SkinSettings::setScale(scale_);
    rml_.reset();                                    // closes its OpenGL context first
}

juce::StringArray ND2Editor::skinNames() const {
    juce::StringArray names;
    for (const auto& s : skin::discoverSkins(Resources::get().skinsFolder().getFullPathName().toStdString()))
        names.add(juce::String::fromUTF8(s.name.c_str()));
    return names;
}

void ND2Editor::showSkin(const juce::String& name, const juce::String& notice) {
    juce::File folder;
    juce::String file = name;
    if (name == SkinSettings::kDefault) {
        folder = Resources::get().defaultSkinFolder();
        file = "nd2-default";
        if (!folder.isDirectory()) { showFallback("The Default skin could not be unpacked to ~/Documents/ND2/cache/."); return; }
    } else {
        if (!skinNames().contains(name)) {
            skinFailed(name, "it is not in ~/Documents/ND2/skins/ (a skin is a folder <name> containing <name>.rml)");
            return;
        }
        folder = Resources::get().skinsFolder().getChildFile(name);
    }
    fallback_.reset();
    if (!rml_) {
        rml_ = std::make_unique<RmlSkinComponent>(proc, logFile_);
        rml_->onRightClick = [this](const juce::MouseEvent&) { showMenu(); };
        rml_->onCornerResize = [this](int phase, juce::Point<int> d) { cornerResize(phase, d); };
        addAndMakeVisible(*rml_);
    }
    current_ = name;
    rml_->onLoaded = [this, name, notice](bool ok, const juce::String& error) {
        if (!ok) { skinFailed(name, error); return; }
        if (notice.isNotEmpty() && rml_) rml_->showMessage(notice);
    };
    rml_->setDebugger(debugger_);
    rml_->setBanners(banners());
    rml_->loadSkin(folder, file);
    applyScale(scale_);
}

// A skin that is missing or fails to load: the Default skin, saying why; if that fails too, the fallback view.
// (Deferred: this can be called from inside the failed skin's own callback.)
void ND2Editor::skinFailed(const juce::String& name, const juce::String& reason) {
    const juce::String why = "The skin \"" + name + "\" could not be shown: " + reason.trim();
    juce::MessageManager::callAsync([sp = juce::Component::SafePointer<ND2Editor>(this), name, why] {
        if (!sp) return;
        if (name != SkinSettings::kDefault) sp->showSkin(SkinSettings::kDefault, why + juce::String::fromUTF8(" \xe2\x80\x94 showing the Default skin."));
        else sp->showFallback(why);
    });
}

void ND2Editor::showFallback(const juce::String& reason) {
    rml_.reset();
    if (!fallback_) {
        fallback_ = std::make_unique<FallbackView>();
        fallback_->onRightClick = [this](const juce::MouseEvent&) { showMenu(); };
        addAndMakeVisible(*fallback_);
    }
    current_.clear();
    fallback_->setReason(reason);
    fallback_->setBanners(banners());
    applyScale(scale_);
}

void ND2Editor::message(const juce::String& text) {
    if (rml_) rml_->showMessage(text, 8);
    else if (fallback_) fallback_->setReason(text);
}

// The banners (approved mockup): the OS update file needed, factory presets not loaded, and work under way.
std::vector<Banner> ND2Editor::banners() {
    Resources& r = Resources::get();
    std::vector<Banner> b;
    if (r.locating()) {
        b.push_back({"Looking for the OS update...", "", {}});
    } else if (!r.hasOs()) {
        b.push_back({"The OS update file is needed.",
                     "It isn't included with ND2: use the update file for your own hardware (the .dmg, the updater app, .zip, .exe or os.cab).",
                     {{"Locate OS update file...", [this] { chooseOs(); }}}});
    } else if (r.building()) {
        int done = 0, total = 0;
        r.buildProgress(done, total);
        b.push_back({"Loading factory banks...", total > 0 ? juce::String(done) + " of " + juce::String(total) + " programs" : juce::String(), {}});
    } else if (proc.bootedWithoutFactory() && !r.hasFactory() && !r.factoryDeclined) {
        b.push_back({"No factory presets loaded.",
                     juce::String::fromUTF8("Load your factory bank files (the factory banks .zip or .nd2_bank files) to fill banks 1\xe2\x80\x93" "4."),
                     {{"Load factory banks...", [this] { chooseBanks(); }},
                      {"Not now", [] { Resources::get().factoryDeclined = true; }, true}}});
    }
    if (proc.engine().state() == nd2::Engine::State::Stopped)
        b.push_back({"The OS stopped.", juce::String(proc.engine().message()), {}});
    return b;
}

void ND2Editor::timerCallback() {
    const std::vector<Banner> b = banners();
    if (rml_) rml_->setBanners(b);
    if (fallback_) fallback_->setBanners(b);
    if (scaleDirty_) { scaleDirty_ = false; SkinSettings::setScale(scale_); }   // not on every drag step
}

void ND2Editor::chooseOs() {
    if (chooser_ || Resources::get().locating()) return;
    chooser_ = std::make_unique<juce::FileChooser>("Locate the OS update file (v3.00)",
                                                   juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Downloads"), "*");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::canSelectDirectories,
                          [sp = juce::Component::SafePointer<ND2Editor>(this)](const juce::FileChooser& fc) {
                              const juce::File f = fc.getResult();
                              if (!sp) return;
                              sp->chooser_.reset();
                              if (f == juce::File()) return;
                              Resources::get().locateOs(f, [sp](bool, juce::String msg) { if (sp) sp->message(msg); });
                              sp->timerCallback();
                          });
}

void ND2Editor::chooseBanks() {
    if (chooser_ || Resources::get().building() || !Resources::get().hasOs()) return;
    chooser_ = std::make_unique<juce::FileChooser>("Load factory banks (.zip, .nd2_bank or .nd2p files)",
                                                   juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Downloads"),
                                                   "*.zip;*.nd2_bank;*.nd2p");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectMultipleItems,
                          [sp = juce::Component::SafePointer<ND2Editor>(this)](const juce::FileChooser& fc) {
                              const juce::Array<juce::File> files = fc.getResults();
                              if (!sp) return;
                              sp->chooser_.reset();
                              if (files.isEmpty()) return;
                              Resources::get().buildFactory(files, [sp](bool, juce::String msg) { if (sp) sp->message(msg); });
                              sp->timerCallback();
                          });
}

void ND2Editor::showMenu() {
    const juce::StringArray names = skinNames();
    juce::PopupMenu skins, zoom, dev, menu;
    skins.addItem(SkinSettings::kDefault, true, current_ == SkinSettings::kDefault, [this] {
        SkinSettings::setSkin(SkinSettings::kDefault);
        showSkin(SkinSettings::kDefault);
    });
    if (!names.isEmpty()) skins.addSeparator();
    for (const juce::String& n : names)
        skins.addItem(n + kFolderNote, true, current_ == n, [this, n] { SkinSettings::setSkin(n); showSkin(n); });
    skins.addSeparator();
    skins.addItem("Open skins folder", [] { Resources::get().skinsFolder().startAsProcess(); });
    const float limit = maxScale();                  // sizes that would not fit the screen are greyed out
    for (int z : winscale::kSteps)
        zoom.addItem(juce::String(winscale::stepLabel(z, true)), winscale::stepFits(z, limit), winscale::isStep(scale_, z), [this, z] {
            applyScale(winscale::stepScale(z));
            scaleDirty_ = false;
            SkinSettings::setScale(scale_);
        });
    dev.addItem("RmlUi debugger", rml_ != nullptr, rml_ && debugger_, [this] {
        debugger_ = !debugger_;
        if (rml_) rml_->setDebugger(debugger_);
    });
    dev.addItem("Write skin log", true, SkinSettings::skinLog(), [] { SkinSettings::setSkinLog(!SkinSettings::skinLog()); });
    menu.addSubMenu("Skin", skins);
    menu.addItem("Reload skin    F5", rml_ != nullptr, false, [this] { if (rml_) rml_->reload(); });
    menu.addSubMenu("Zoom", zoom);
    menu.addSeparator();
    menu.addItem("OS update file...", !Resources::get().locating(), false, [this] { chooseOs(); });
    menu.addItem("Factory banks...", Resources::get().hasOs() && !Resources::get().building(), false, [this] { chooseBanks(); });
    menu.addSeparator();
    menu.addSubMenu("Developer", dev);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition());
}

// ------------------------------------------------------------------ size

// Dragging the bottom-right corner: follow whichever direction moved further, keeping the aspect ratio.
void ND2Editor::cornerResize(int phase, juce::Point<int> d) {
    if (phase == 0) { dragStartBounds_ = getBounds(); return; }
    const float aspect = skin::kBodyWidthDp / skin::kBodyHeightDp;
    const int dy = juce::roundToInt(float(d.y) * aspect);
    const int dw = std::abs(d.x) >= std::abs(dy) ? d.x : dy;
    auto b = dragStartBounds_.withWidth(dragStartBounds_.getWidth() + dw);
    b.setHeight(juce::roundToInt(float(b.getWidth()) / aspect));
    constrainer_.setBoundsForComponent(this, b, false, false, true, true);   // 50-200 %, aspect ratio
}

// The usable area of the display the window is on, less room for the host's window frame.
float ND2Editor::maxScale() const {
    const juce::Displays& displays = juce::Desktop::getInstance().getDisplays();
    const juce::Displays::Display* d = getPeer() != nullptr || getParentComponent() != nullptr
                                       ? displays.getDisplayForRect(getScreenBounds()) : nullptr;
    if (d == nullptr) d = displays.getPrimaryDisplay();
    if (d == nullptr) return winscale::kMax;
    return winscale::fitScale(d->userArea.getWidth(), d->userArea.getHeight(), int(skin::kBodyWidthDp), int(skin::kBodyHeightDp), true);
}

// Resizable by the corner (or the host), locked to the skin's aspect ratio, 50-200 % of its 100 % size, and never
// larger than the screen: a size that does not fit steps down to the largest Zoom step that does.
void ND2Editor::applyScale(float scale) {
    const float limit = maxScale();
    scale_ = winscale::fitted(scale, limit);
    const int bw = int(skin::kBodyWidthDp), bh = int(skin::kBodyHeightDp);
    const int w = winscale::windowLength(bw, scale_, true), h = winscale::windowLength(bh, scale_, true);
    constrainer_.setFixedAspectRatio(double(w) / double(h));
    const float most = std::max(scale_, std::min(winscale::kMax, limit));
    constrainer_.setSizeLimits(winscale::windowLength(bw, winscale::kMin, true), winscale::windowLength(bh, winscale::kMin, true),
                               winscale::windowLength(bw, most, true), winscale::windowLength(bh, most, true));
    applying_ = true;
    setSize(w, h);
    applying_ = false;
    resized();
}

void ND2Editor::resized() {
    const int bw = int(skin::kBodyWidthDp);
    // A resize by the user or the host: the width sets the scale (not when it is the width of the current scale,
    // so rounding does not move a menu step).
    if (!applying_ && getWidth() > 0 && getWidth() != winscale::windowLength(bw, scale_, true)) {
        const float s = winscale::scaleForWidth(getWidth(), bw, true);
        if (std::abs(s - scale_) > 1e-4f) { scale_ = s; scaleDirty_ = true; }
    }
    proc.editorScale = scale_;                       // saved with the project
    if (rml_) {
        rml_->setZoom(float(getWidth()) / float(bw));   // the dp ratio follows the window: art drawn at its size
        rml_->setBounds(getLocalBounds());
    }
    if (fallback_) fallback_->setBounds(getLocalBounds());
}
