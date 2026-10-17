// The BANKS tab of the ND2 window (ND2Window; approved mockup):
//   top:    one line on what the tab does
//   left:   the bank files in ~/Documents/ND2/Library (factory banks first), Add files..., Show folder
//   right:  the 8 banks in ND2's memory: what was loaded into each, the program playing, programs stored on the panel
//           since loading; Load... (or drag a file onto a bank: "Bank N will be overwritten") and Save as file...
// Its line in the window's footer (footerLine): progress with Cancel, a confirmation, the last result, "Use these banks
// for new instances". The machine keeps running underneath. Laid out in dp inside the window's body.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>
#include "BankFiles.h"
#include "PluginProcessor.h"
#include "ui/Widgets.h"

class BanksPanel : public juce::Component, public juce::DragAndDropContainer {
public:
    BanksPanel(ND2Processor& p, const ui::Theme& theme);
    ~BanksPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // ---- the window
    void tick();                                     // 10 times a second: the banks' state, the library folder
    std::function<void()> onFooterChanged;            // the footer line changed (a message, a question, progress)
    struct Line { std::vector<ui::Footer::Part> parts; std::vector<ui::Footer::Action> actions; };
    Line footerLine();
    bool cancelQuestion();                           // Esc: a question in the footer closed (false: there was none)

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
    void rescan();
    static juce::String librarySignature();          // the library's files and their times (rescan when it changes)
    void refresh();
    void addFiles();
    void useForNew();

    ND2Processor& proc_;
    const ui::Theme& theme_;
    std::unique_ptr<LookAndFeel> lnf_;
    std::vector<bankfiles::BankFile> library_;
    juce::Viewport libView_;
    juce::Component libList_;
    std::vector<std::unique_ptr<FileRow>> fileRows_;
    std::vector<std::unique_ptr<BankRow>> bankRows_;
    juce::TextButton addBtn_{"Add files..."}, folderBtn_{"Show folder"};
    std::function<void()> pendingYes_;                // the question shown in the footer
    juce::String pendingText_, pendingLabel_;
    juce::String message_;
    bool messageError_ = false;
    uint32_t serial_ = 0;
    juce::String signature_;
    int current_ = -2;
    std::unique_ptr<juce::FileChooser> chooser_;
};
