#include "BanksPanel.h"
#include "Banner.h"
#include "Resources.h"

namespace {
const juce::Colour kText = ui::colours::text, kMuted = ui::colours::muted, kHeading = ui::colours::heading, kLine = ui::colours::line,
                   kRed(0xffe3262d), kRedEdge(0xfff2555b), kBtn = ui::colours::button, kBtnEdge = ui::colours::buttonEdge,
                   kRow = ui::colours::row, kRowEdge = ui::colours::rowEdge, kSel = ui::colours::selection, kSelEdge = ui::colours::selectionEdge;
constexpr int kHint = 34, kLibW = 520, kFileRowH = 45, kBankRowH = 72, kBankGap = 10;

juce::Font font(float size, bool bold = false) { return banner::font(size, bold); }
juce::String bankName(int b) { return "Bank " + juce::String(b + 1); }
juce::String quoted(const juce::String& s) { return juce::String::fromUTF8("\xe2\x80\x9c") + s + juce::String::fromUTF8("\xe2\x80\x9d"); }
const juce::String kDragPrefix = "nd2bankfile:";
}  // namespace

// ================================================================== look

class BanksPanel::LookAndFeel : public ui::MenuLookAndFeel {   // the window's buttons; its menus (Load...) in the menu look
public:
    explicit LookAndFeel(const ui::Theme& t) : ui::MenuLookAndFeel(t) {
        setScale(1.15f);
        setColour(juce::ScrollBar::thumbColourId, ui::colours::muted.withAlpha(0.6f));   // the library list's scroll bar
    }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override {
        const bool primary = b.getProperties()["primary"];
        auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        juce::Colour fill = primary ? kRed : kBtn, edge = primary ? kRedEdge : kBtnEdge;
        if (!b.isEnabled()) fill = fill.withMultipliedAlpha(0.45f), edge = edge.withMultipliedAlpha(0.45f);
        else if (down) fill = fill.darker(0.25f);
        else if (over) fill = fill.brighter(0.12f);
        const float radius = b.getProperties()["round"] ? r.getHeight() / 2 : 8.0f;
        g.setColour(fill);
        g.fillRoundedRectangle(r, radius);
        g.setColour(edge);
        g.drawRoundedRectangle(r, radius, 1.0f);
    }
    juce::Font getTextButtonFont(juce::TextButton& b, int) override {
        return b.getProperties()["round"] ? font(24.0f) : font(b.getHeight() <= 34 ? 15.5f : 17.0f);
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        g.setFont(getTextButtonFont(b, b.getHeight()));
        g.setColour(kText.withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.45f));
        g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
    }
};

static void style(juce::TextButton& b, bool primary = false) {
    b.getProperties().set("primary", primary);
    b.setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

// ================================================================== a bank file (left column)

class BanksPanel::FileRow : public juce::Component {
public:
    FileRow(BanksPanel& p, int index) : panel_(p), index_(index) { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }
    const bankfiles::BankFile& file() const { return panel_.library()[size_t(index_)]; }
    bool selected = false;

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        if (selected) {
            g.setColour(kSel);
            g.fillRoundedRectangle(r, 8.0f);
            g.setColour(kSelEdge);
            g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
        }
        paintContent(g, getWidth());
    }
    void paintContent(juce::Graphics& g, int width) {
        auto a = juce::Rectangle<int>(0, 0, width, getHeight()).reduced(13, 0);
        g.setColour(kRed.withAlpha(0.85f));
        g.fillRoundedRectangle(a.removeFromLeft(24).withSizeKeepingCentre(24, 19).toFloat(), 3.0f);
        a.removeFromLeft(13);
        g.setFont(font(16.0f));
        g.setColour(kMuted);
        const juce::String count = juce::String(file().count);
        g.drawText(count, a.removeFromRight(44), juce::Justification::centredRight);
        g.setFont(font(19.0f));
        g.setColour(juce::Colour(0xffd8d8dc));
        g.drawText(file().name, a, juce::Justification::centredLeft, true);
    }
    void mouseDown(const juce::MouseEvent&) override {
        for (auto& r : panel_.fileRows_) { r->selected = r.get() == this; r->repaint(); }
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (e.getDistanceFromDragStart() < 5 || panel_.isDragAndDropActive() || panel_.busy()) return;
        // a card with the file's name, beside the pointer (so the bank's "will be overwritten" stays readable)
        const int w = juce::jmin(getWidth(), 260), m = 28;   // the card sits m dp right of and below the pointer
        const float scale = 2.0f;                       // sharp on high-resolution screens
        juce::Image img(juce::Image::ARGB, juce::roundToInt(float(w + m) * scale), juce::roundToInt(float(getHeight() + m) * scale), true);
        {
            juce::Graphics g(img);
            g.addTransform(juce::AffineTransform::scale(scale).translated(float(m) * scale, float(m) * scale));
            const auto card = juce::Rectangle<int>(0, 0, w, getHeight()).toFloat();
            g.setColour(kSel.withAlpha(0.94f));
            g.fillRoundedRectangle(card, 8.0f);
            g.setColour(kSelEdge);
            g.drawRoundedRectangle(card.reduced(0.5f), 8.0f, 1.0f);
            paintContent(g, w);
        }
        juce::ScaledImage si(img, scale);
        const juce::Point<int> offset(0, 0);           // the pointer at the image's (transparent) top left
        panel_.startDragging(kDragPrefix + file().file.getFullPathName(), this, si, false, &offset);
    }

private:
    BanksPanel& panel_;
    int index_;
};

// ================================================================== a bank (right column)

class BanksPanel::BankRow : public juce::Component, public juce::DragAndDropTarget, public juce::FileDragAndDropTarget {
public:
    BankRow(BanksPanel& p, int bank) : panel_(p), bank_(bank) {
        style(load_);
        style(save_);
        load_.onClick = [this] { panel_.showLoadMenu(bank_, &load_); };
        save_.onClick = [this] { panel_.save(bank_); };
        addAndMakeVisible(load_);
        addAndMakeVisible(save_);
    }
    void refresh() {
        load_.setEnabled(!panel_.busy());
        save_.setEnabled(!panel_.busy() && panel_.canSave(bank_));
        repaint();
    }
    void resized() override {
        auto a = getLocalBounds().reduced(16, 0);
        save_.setBounds(a.removeFromRight(150).withSizeKeepingCentre(150, 37));
        a.removeFromRight(8);
        load_.setBounds(a.removeFromRight(90).withSizeKeepingCentre(90, 37));
    }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        const bool target = hover_.isNotEmpty();
        g.setColour(target ? kSel : kRow);
        g.fillRoundedRectangle(r, 10.0f);
        if (target) {
            juce::Path p, dashed;
            p.addRoundedRectangle(r, 10.0f);
            const float dash[] = {6.0f, 4.0f};
            juce::PathStrokeType(1.3f).createDashedStroke(dashed, p, dash, 2);
            g.setColour(kSelEdge);
            g.fillPath(dashed);
        } else {
            g.setColour(kRowEdge);
            g.drawRoundedRectangle(r, 10.0f, 1.0f);
        }
        auto a = getLocalBounds().reduced(16, 0);
        auto led = a.removeFromLeft(12).withSizeKeepingCentre(12, 12).toFloat();
        const bool on = panel_.playing(bank_);
        if (on) { g.setColour(juce::Colour(0x66ff3b2f)); g.fillEllipse(led.expanded(3.0f)); }
        g.setColour(on ? juce::Colour(0xffff3b2f) : juce::Colour(0xff555555));
        g.fillEllipse(led);
        a.removeFromLeft(16);
        g.setColour(kText);
        g.setFont(font(20.0f, true));
        g.drawText(bankName(bank_), a.removeFromLeft(99), juce::Justification::centredLeft);
        a.removeFromRight(load_.getWidth() + save_.getWidth() + 24);
        bool dim = false;
        juce::String title = panel_.bankTitle(bank_, dim), detail = panel_.bankDetail(bank_);
        if (target) { title = "Drop to replace: " + hover_; detail = bankName(bank_) + " will be overwritten"; dim = false; }
        const juce::Colour targetText(0xffffd9d6);
        auto top = a.withTrimmedBottom(a.getHeight() / 2).withTrimmedTop(4);
        auto bottom = a.withTrimmedTop(a.getHeight() / 2).withTrimmedBottom(6);
        g.setFont(font(19.0f));
        g.setColour(target ? targetText : dim ? kMuted : kText);
        g.drawText(title, detail.isEmpty() ? a : top, detail.isEmpty() ? juce::Justification::centredLeft : juce::Justification::bottomLeft, true);
        if (detail.isNotEmpty()) {
            g.setFont(font(16.0f));
            g.setColour(target ? targetText : kMuted);
            g.drawText(detail, bottom, juce::Justification::topLeft, true);
        }
    }
    // a bank file dragged from the list
    bool isInterestedInDragSource(const SourceDetails& d) override {
        return !panel_.busy() && d.description.toString().startsWith(kDragPrefix);
    }
    void itemDragEnter(const SourceDetails& d) override {
        hover_ = juce::File(d.description.toString().fromFirstOccurrenceOf(kDragPrefix, false, false)).getFileNameWithoutExtension();
        repaint();
    }
    void itemDragExit(const SourceDetails&) override { hover_.clear(); repaint(); }
    void itemDropped(const SourceDetails& d) override {
        hover_.clear();
        repaint();
        const juce::File f(d.description.toString().fromFirstOccurrenceOf(kDragPrefix, false, false));
        for (const auto& b : panel_.library())
            if (b.file == f) { panel_.loadNow(bank_, b); return; }
    }
    // a bank file dragged from the Finder
    bool isInterestedInFileDrag(const juce::StringArray& files) override {
        return !panel_.busy() && files.size() == 1 && files[0].endsWithIgnoreCase(".nd2_bank");
    }
    void fileDragEnter(const juce::StringArray& files, int, int) override { hover_ = juce::File(files[0]).getFileNameWithoutExtension(); repaint(); }
    void fileDragExit(const juce::StringArray&) override { hover_.clear(); repaint(); }
    void filesDropped(const juce::StringArray& files, int, int) override {
        hover_.clear();
        repaint();
        panel_.loadFromFinder(bank_, juce::File(files[0]));
    }

private:
    BanksPanel& panel_;
    int bank_;
    juce::String hover_;
    juce::TextButton load_{"Load..."}, save_{"Save as file..."};
};

// ================================================================== page

BanksPanel::BanksPanel(ND2Processor& p, const ui::Theme& theme) : proc_(p), theme_(theme), lnf_(std::make_unique<LookAndFeel>(theme)) {
    setLookAndFeel(lnf_.get());
    libView_.setViewedComponent(&libList_, false);
    libView_.setScrollBarsShown(true, false);
    libView_.setScrollBarThickness(8);
    addAndMakeVisible(libView_);
    for (int b = 0; b < nd2::kBanks; ++b) {
        bankRows_.push_back(std::make_unique<BankRow>(*this, b));
        addAndMakeVisible(*bankRows_.back());
    }
    for (juce::TextButton* b : {&addBtn_, &folderBtn_}) {
        style(*b);
        addAndMakeVisible(*b);
    }
    addBtn_.onClick = [this] { addFiles(); };
    folderBtn_.onClick = [] { bankfiles::libraryFolder().createDirectory(); bankfiles::libraryFolder().startAsProcess(); };
    rescan();
    proc_.banks().makeFactoryFiles();                 // Library/Factory, once, if it is empty
    proc_.banks().check();                            // what the banks hold (skipped if nothing changed)
    refresh();
}

BanksPanel::~BanksPanel() { setLookAndFeel(nullptr); }

juce::String BanksPanel::librarySignature() {
    juce::String sig;
    for (const auto& f : bankfiles::libraryFolder().findChildFiles(juce::File::findFiles, true, "*.nd2_bank"))
        sig << f.getFullPathName() << f.getLastModificationTime().toMilliseconds() << ";";
    return sig;
}

void BanksPanel::rescan() {
    signature_ = librarySignature();
    juce::String selected;
    for (auto& r : fileRows_) if (r->selected) selected = r->file().file.getFullPathName();
    library_ = bankfiles::scanLibrary();
    fileRows_.clear();
    for (int i = 0; i < int(library_.size()); ++i) {
        fileRows_.push_back(std::make_unique<FileRow>(*this, i));
        fileRows_.back()->selected = library_[size_t(i)].file.getFullPathName() == selected;
        libList_.addAndMakeVisible(*fileRows_.back());
    }
    resized();
}

bool BanksPanel::busy() const { return proc_.banks().busy(); }

// ------------------------------------------------------------------ what a bank row shows

juce::String BanksPanel::bankTitle(int bank, bool& dim) const {
    const BankService& s = proc_.banks();
    const auto& st = s.status(bank);
    dim = false;
    if (st.known && st.count == 0) { dim = true; return "Empty"; }
    const auto rec = s.records()[size_t(bank)];
    if (rec.set && rec.name.isNotEmpty()) return rec.name;
    if (st.known) {
        for (const auto& f : library_) if (f.crcs == st.crcs) return f.name;   // the same programs as a library file
        dim = true;
        return "Unnamed bank (" + juce::String(st.count) + " programs)";
    }
    dim = true;
    return juce::String::fromUTF8("\xe2\x80\xa6");   // not known yet: being checked
}

juce::String BanksPanel::bankDetail(int bank) const {
    juce::StringArray parts;
    const int cur = proc_.engine().currentProgram();
    if (cur >= 0 && cur / nd2::kBankPrograms == bank)
        parts.add("playing P" + juce::String(bank + 1) + "." + juce::String(cur % nd2::kBankPrograms + 1));
    const int changed = proc_.banks().changedSinceLoading(bank);
    if (changed == 1) parts.add("1 program stored on the panel since loading");
    else if (changed > 1) parts.add(juce::String(changed) + " programs stored on the panel since loading");
    return parts.joinIntoString(juce::String::fromUTF8(" \xc2\xb7 "));
}

bool BanksPanel::playing(int bank) const {
    const int cur = proc_.engine().currentProgram();
    return cur >= 0 && cur / nd2::kBankPrograms == bank;
}

bool BanksPanel::canSave(int bank) const {
    const auto& st = proc_.banks().status(bank);
    return !(st.known && st.count == 0);
}

// ------------------------------------------------------------------ actions

void BanksPanel::setMessage(const juce::String& m, bool error) {
    message_ = m;
    messageError_ = error;
    if (onFooterChanged) onFooterChanged();
}

void BanksPanel::askLoad(int bank, const bankfiles::BankFile& file) {
    pendingText_ = "Load " + quoted(file.name) + " into " + bankName(bank) + "? " + bankName(bank) + " will be overwritten.";
    pendingLabel_ = "Load";
    pendingYes_ = [this, bank, file] { loadNow(bank, file); };
    refresh();
}

void BanksPanel::loadNow(int bank, const bankfiles::BankFile& file) {
    if (busy()) return;
    pendingYes_ = nullptr;
    pendingText_.clear();
    message_.clear();
    proc_.banks().load(bank, file);
    refresh();
}

void BanksPanel::loadFromFinder(int bank, const juce::File& f) {
    juce::String err;
    const juce::File copy = bankfiles::addToLibrary(f, false, err);
    bankfiles::BankFile b;
    if (copy == juce::File() || !bankfiles::readBankFile(copy, b, err)) {
        setMessage(f.getFileName() + " was not loaded: " + err + ".", true);
        return;
    }
    rescan();
    loadNow(bank, b);
}

void BanksPanel::showLoadMenu(int bank, juce::Component* from) {
    if (busy()) return;
    juce::PopupMenu m;
    m.addSectionHeader("Load into " + bankName(bank));
    bool factory = true;
    for (size_t i = 0; i < library_.size(); ++i) {
        const auto& f = library_[i];
        if (factory && !f.factory && i > 0) m.addSeparator();
        factory = f.factory;
        m.addItem(f.name, [this, bank, f] { askLoad(bank, f); });
    }
    if (!library_.empty()) m.addSeparator();
    m.addItem("Other file...", [this, bank] {
        chooser_ = std::make_unique<juce::FileChooser>("Load a bank file into " + bankName(bank),
                                                       juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.nd2_bank");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [sp = juce::Component::SafePointer<BanksPanel>(this), bank](const juce::FileChooser& fc) {
                                  const juce::File f = fc.getResult();
                                  if (!sp) return;
                                  sp->chooser_.reset();
                                  if (f == juce::File()) return;
                                  juce::String err;
                                  const juce::File copy = bankfiles::addToLibrary(f, false, err);
                                  bankfiles::BankFile b;
                                  if (copy == juce::File() || !bankfiles::readBankFile(copy, b, err)) {
                                      sp->setMessage(f.getFileName() + " was not loaded: " + err + ".", true);
                                      return;
                                  }
                                  sp->rescan();
                                  sp->askLoad(bank, b);
                              });
    });
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(from));
}

void BanksPanel::save(int bank) {
    if (busy()) return;
    message_.clear();
    proc_.banks().save(bank);
    refresh();
}

void BanksPanel::addFiles() {
    chooser_ = std::make_unique<juce::FileChooser>("Add bank files to the Library", juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                                                   "*.nd2_bank");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::canSelectMultipleItems,
                          [sp = juce::Component::SafePointer<BanksPanel>(this)](const juce::FileChooser& fc) {
                              const juce::Array<juce::File> files = fc.getResults();
                              if (!sp) return;
                              sp->chooser_.reset();
                              int added = 0;
                              juce::StringArray bad;
                              for (const juce::File& f : files) {
                                  bankfiles::BankFile b;
                                  juce::String err;
                                  if (!bankfiles::readBankFile(f, b, err)) { bad.add(f.getFileName() + " (" + err + ")"); continue; }
                                  if (bankfiles::addToLibrary(f, false, err) != juce::File()) ++added; else bad.add(f.getFileName() + " (" + err + ")");
                              }
                              sp->rescan();
                              if (!bad.isEmpty()) sp->setMessage("Not added: " + bad.joinIntoString(", ") + ".", true);
                              else if (added > 0) sp->setMessage(juce::String(added) + (added == 1 ? " file" : " files") + " added to the Library.", false);
                          });
}

void BanksPanel::useForNew() {
    if (busy()) return;
    pendingText_ = "New instances of ND2 start with these 8 banks from now on (projects keep their own).";
    pendingLabel_ = "Use these banks";
    pendingYes_ = [this] {
        std::array<juce::String, nd2::kBanks> names;
        for (int b = 0; b < nd2::kBanks; ++b) {
            bool dim = false;
            const juce::String t = bankTitle(b, dim);
            names[size_t(b)] = dim ? juce::String() : t;
        }
        message_.clear();
        proc_.banks().useForNewInstances(names);
    };
    refresh();
}

// ------------------------------------------------------------------ updates

void BanksPanel::tick() {
    const uint32_t s = proc_.banks().serial();
    const int cur = proc_.engine().currentProgram();
    if (s != serial_) {
        serial_ = s;
        if (librarySignature() != signature_) rescan();   // a saved file or new factory files
        const juce::String m = proc_.banks().message();
        if (m.isNotEmpty()) { message_ = m; messageError_ = proc_.banks().messageIsError(); }
        refresh();
    } else if (cur != current_) {
        current_ = cur;
        for (auto& r : bankRows_) r->repaint();
    }
    if (busy() || proc_.banks().checking()) if (onFooterChanged) onFooterChanged();   // progress
}

void BanksPanel::refresh() {
    for (auto& r : bankRows_) r->refresh();
    addBtn_.setEnabled(!busy());
    repaint();
    if (onFooterChanged) onFooterChanged();
}

bool BanksPanel::cancelQuestion() {
    if (pendingYes_ == nullptr) return false;
    pendingYes_ = nullptr;
    pendingText_.clear();
    refresh();
    return true;
}

BanksPanel::Line BanksPanel::footerLine() {
    Line l;
    BankService& s = proc_.banks();
    if (s.busy()) {
        int done = 0, total = 0;
        s.progress(done, total);
        juce::String t = s.busyText();
        if (total > 0) t << ": program " << juce::jmin(done + 1, total) << " of " << total;
        l.parts.push_back({t});
        l.actions.push_back({"Cancel", [this] { proc_.banks().cancel(); }});
        return l;
    }
    if (pendingYes_ != nullptr) {
        l.parts.push_back({pendingText_});
        l.actions.push_back({pendingLabel_, [this] {
                                 auto f = std::move(pendingYes_);
                                 pendingYes_ = nullptr;
                                 pendingText_.clear();
                                 if (f) f();
                                 refresh();
                             }, true});
        l.actions.push_back({"Cancel", [this] { cancelQuestion(); }});
        return l;
    }
    if (message_.isNotEmpty()) l.parts.push_back({message_ + " ", false, false});
    else if (s.checking()) {
        const int b = s.checkingBank();
        l.parts.push_back({juce::String::fromUTF8("Checking what the banks hold\xe2\x80\xa6") + (b >= 0 ? " (" + bankName(b) + ")" : juce::String()) + " ",
                           false, true});
    }
    l.parts.push_back({"The 8 banks are saved with this project.", false, true});
    l.actions.push_back({"Use these banks for new instances", [this] { useForNew(); }});
    return l;
}

void BanksPanel::resized() {
    auto body = getLocalBounds().withTrimmedTop(kHint);
    auto lib = body.removeFromLeft(kLibW);
    lib.removeFromTop(30);                             // heading
    auto buttons = lib.removeFromBottom(40);
    addBtn_.setBounds(buttons.removeFromLeft(130));
    buttons.removeFromLeft(10);
    folderBtn_.setBounds(buttons.removeFromLeft(140));
    lib.removeFromBottom(14);
    libView_.setBounds(lib);
    int y = 0;
    for (size_t i = 0; i < fileRows_.size(); ++i) {
        if (i > 0 && library_[i - 1].factory && !library_[i].factory) y += 27;   // the separator line
        fileRows_[i]->setBounds(0, y, lib.getWidth() - 10, kFileRowH);
        y += kFileRowH;
    }
    libList_.setSize(lib.getWidth() - 10, y);
    auto slots = body.withTrimmedLeft(int(ui::dp::gap) * 2);
    slots.removeFromTop(30);
    for (auto& r : bankRows_) {
        r->setBounds(slots.removeFromTop(kBankRowH));
        slots.removeFromTop(kBankGap);
    }
}

void BanksPanel::paint(juce::Graphics& g) {
    const auto w = getLocalBounds();
    g.setColour(kMuted);
    g.setFont(font(17.0f));
    g.drawText("Load any bank file into one of the 8 banks, or save a bank as a file.", w.withHeight(24), juce::Justification::centredLeft, true);
    g.setColour(kLine);
    g.fillRect(kLibW + int(ui::dp::gap), kHint, 1, w.getHeight() - kHint);
    // headings
    g.setColour(kHeading);
    g.setFont(font(16.0f, true).withExtraKerningFactor(0.12f));
    g.drawText(juce::String::fromUTF8("BANK FILES \xc2\xb7 ") + bankfiles::shownFolder("Library"), 0, kHint, kLibW, 24,
               juce::Justification::centredLeft, true);
    g.drawText(juce::String::fromUTF8("ND2 MEMORY \xc2\xb7 8 BANKS \xc3\x97 50 PROGRAMS"), kLibW + int(ui::dp::gap) * 2, kHint, 600, 24,
               juce::Justification::centredLeft, true);
    // separator between the factory banks and the others
    for (size_t i = 1; i < fileRows_.size(); ++i)
        if (library_[i - 1].factory && !library_[i].factory) {
            const auto r = fileRows_[i]->getBoundsInParent();
            const int y = libView_.getY() + r.getY() - libView_.getViewPositionY() - 14;
            if (y > libView_.getY() && y < libView_.getBottom()) { g.setColour(kLine); g.fillRect(libView_.getX(), y, libView_.getWidth() - 10, 1); }
        }
    if (library_.empty()) {
        g.setColour(kMuted);
        g.setFont(font(17.0f));
        g.drawFittedText("No bank files yet. Add files... copies .nd2_bank files here; Save as file... puts a bank here.",
                         libView_.getBounds().withHeight(80), juce::Justification::topLeft, 3);
    }
}
