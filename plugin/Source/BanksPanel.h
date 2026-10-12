// The Banks window (approved mockup): over the panel, opened by the skin's BANKS label (or the right-click menu).
//   left:   the bank files in ~/Documents/ND2/Library (factory banks first), Add files..., Show folder
//   right:  the 8 banks in ND2's memory: what was loaded into each, the program playing, programs stored on the panel
//           since loading; Load... (or drag a file onto a bank: "Bank N will be overwritten") and Save as file...
//   footer: progress with Cancel, the last result, "Use these banks for new instances"
// The machine keeps running underneath. Laid out in skin dp (1725 x 1035 body) and scaled with the window; the top
// strip of the panel (where the BANKS label is) stays uncovered.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>
#include "BankFiles.h"
#include "PluginProcessor.h"

class BanksPanel : public juce::Component, public juce::DragAndDropContainer, private juce::Timer {
public:
    static constexpr float kTopDp = 76;               // the panel strip left uncovered (the BANKS label, TRIG INPUTS)
    explicit BanksPanel(ND2Processor& p);
    ~BanksPanel() override;

    std::function<void()> onClose;
    void place(juce::Rectangle<int> parentBounds, float pxPerDp);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

    // ---- used by the rows
    const std::vector<bankfiles::BankFile>& library() const { return library_; }
    void askLoad(int bank, const bankfiles::BankFile& file);   // confirmation in the footer
    void loadNow(int bank, const bankfiles::BankFile& file);
    void loadFromFinder(int bank, const juce::File& f);
    void showLoadMenu(int bank, juce::Component* from);
    void save(int bank);
    bool busy() const;
    juce::String bankTitle(int bank, bool& dim) const;
    juce::String bankDetail(int bank) const;
    bool playing(int bank) const;
    bool canSave(int bank) const;
    void setMessage(const juce::String& m, bool error);

    class LookAndFeel;
    class FileRow;
    class BankRow;

private:
    void timerCallback() override;
    void rescan();
    static juce::String librarySignature();          // the library's files and their times (rescan when it changes)
    void refresh();
    void addFiles();
    void useForNew();
    void layoutFooter();
    juce::Rectangle<int> windowDp() const { return {129, 1, 1467, 880}; }

    ND2Processor& proc_;
    std::unique_ptr<LookAndFeel> lnf_;
    std::vector<bankfiles::BankFile> library_;
    juce::Viewport libView_;
    juce::Component libList_;
    std::vector<std::unique_ptr<FileRow>> fileRows_;
    std::vector<std::unique_ptr<BankRow>> bankRows_;
    juce::TextButton addBtn_{"Add files..."}, folderBtn_{"Show folder"}, cancelBtn_{"Cancel"}, newBtn_{"Use these banks for new instances"};
    juce::TextButton yesBtn_, noBtn_{"Cancel"};
    juce::TextButton closeBtn_;
    std::function<void()> pendingYes_;                // the confirmation shown in the footer
    juce::String pendingText_;
    juce::String message_;
    bool messageError_ = false;
    uint32_t serial_ = 0;
    juce::String signature_;
    int current_ = -2;
    std::unique_ptr<juce::FileChooser> chooser_;
};
