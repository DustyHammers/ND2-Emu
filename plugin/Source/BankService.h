// The Banks window's work for one instance: loading a bank file into one of the 8 banks, saving a bank as a file,
// checking what the banks hold, and making the factory files and the banks new instances start with.
//
// Every job works on a copy of the running machine (Engine::getState) restored in an offline machine
// (nd2::BankMachine), driven only through the OS's own program dump messages, never on the running machine itself.
// A load ends by handing the result back to the running machine (Engine::setState) on the message thread.
// One job at a time on this instance's worker thread; a check gives way to the others.
//
// Message thread only, except where noted.
#pragma once
#include <juce_events/juce_events.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include "BankFiles.h"
#include "bankio.h"
#include "engine.h"

class BankService {
public:
    using Record = bankfiles::BankRecord;
    using Records = bankfiles::BankRecords;
    // What a bank holds, from the last check.
    struct Status {
        bool known = false;
        int count = 0;
        std::array<int, nd2::kBankPrograms> crcs{};
    };

    explicit BankService(nd2::Engine& engine);
    ~BankService();

    // ---- records: saved with the project (any thread)
    Records records() const;
    void setRecords(const Records& r);

    // ---- what the banks hold
    const Status& status(int bank) const { return status_[size_t(bank)]; }
    int changedSinceLoading(int bank) const;          // programs stored on the panel since the bank was loaded; -1 unknown

    // ---- jobs
    void check();                                     // reads the 8 banks (skipped if nothing was stored since)
    void load(int bank, const bankfiles::BankFile& file);
    void save(int bank);                              // into the Library folder as "Bank N (11 Oct).nd2_bank"
                                                      // (an unnamed bank takes that name)
    // The 8 banks become the factory memory of new instances; names: what the window shows for each bank.
    void useForNewInstances(const std::array<juce::String, nd2::kBanks>& names);
    void makeFactoryFiles();                          // Library/Factory from the factory memory (when it is empty)
    void cancel();                                    // the running load or save stops; nothing is changed

    bool busy() const { return busy_.load(); }        // a load, save, "use for new instances" or factory job
    bool checking() const { return checking_.load(); }
    int checkingBank() const { return checkBank_.load(); }
    juce::String busyText() const { return busyText_; }
    void progress(int& done, int& total) const { done = done_.load(); total = total_.load(); }
    // Changes when records, status or the message change (the window redraws).
    uint32_t serial() const { return serial_.load(); }
    juce::String message() const { return message_; }
    bool messageIsError() const { return messageError_; }
    juce::File lastSaved() const { return lastSaved_; }

private:
    enum class Kind { Check, Factory, User };          // Check and Factory give way to User jobs
    struct Job { Kind kind = Kind::User; std::function<void()> run; };
    void post(Job j);
    void runWorker();
    void done(std::function<void()> onMessageThread);
    void setMessage(const juce::String& m, bool error) { message_ = m; messageError_ = error; ++serial_; }
    bool restoreRunning(nd2::BankMachine& m, juce::String& error);
    static uint64_t hashOf(const std::vector<uint8_t>& v);

    nd2::Engine& engine_;
    mutable std::mutex recMtx_;
    Records records_;
    std::array<Status, nd2::kBanks> status_{};
    uint64_t checkedFlash_ = 0;                       // the flash the status belongs to (0: none)

    std::thread worker_;
    std::mutex qMtx_;
    std::condition_variable qCv_;
    std::deque<Job> queue_;
    bool stop_ = false;
    std::atomic<bool> cancel_{false}, cancelCheck_{false}, busy_{false}, checking_{false};
    std::atomic<int> done_{0}, total_{0}, checkBank_{-1};
    std::atomic<uint32_t> serial_{1};
    juce::String busyText_, message_;
    bool messageError_ = false;
    juce::File lastSaved_;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};
