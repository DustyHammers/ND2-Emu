#include "BankService.h"
#include "Diag.h"
#include "Resources.h"
#include <functional>
#include <string>

namespace {
std::atomic<bool> factoryFilesRunning{false};        // process-wide: one instance makes the factory files
juce::String bankName(int bank) { return "Bank " + juce::String(bank + 1); }
}  // namespace

BankService::BankService(nd2::Engine& engine) : engine_(engine) {
    for (auto& r : records_) r.crcs.fill(bankfiles::kEmpty);
    worker_ = std::thread([this] { runWorker(); });
}

BankService::~BankService() {
    {
        std::lock_guard<std::mutex> l(qMtx_);
        stop_ = true;
        queue_.clear();
    }
    cancel_ = true;
    cancelCheck_ = true;
    qCv_.notify_all();
    if (worker_.joinable()) worker_.join();
    alive_.reset();
}

// ------------------------------------------------------------------ records

BankService::Records BankService::records() const {
    std::lock_guard<std::mutex> l(recMtx_);
    return records_;
}

void BankService::setRecords(const Records& r) {
    {
        std::lock_guard<std::mutex> l(recMtx_);
        records_ = r;
    }
    ++serial_;
}

int BankService::changedSinceLoading(int bank) const {
    const Status& s = status_[size_t(bank)];
    const Record r = records()[size_t(bank)];
    if (!s.known || !r.set) return -1;
    int n = 0;
    for (int i = 0; i < nd2::kBankPrograms; ++i) n += s.crcs[size_t(i)] != r.crcs[size_t(i)] && s.crcs[size_t(i)] != bankfiles::kEmpty;
    return n;
}

// ------------------------------------------------------------------ worker

void BankService::post(Job j) {
    {
        std::lock_guard<std::mutex> l(qMtx_);
        if (j.kind == Kind::User) {
            cancelCheck_ = true;                         // a running check gives way; it runs again afterwards
            queue_.push_front(std::move(j));
        } else {
            for (const Job& q : queue_) if (q.kind == j.kind) return;
            queue_.push_back(std::move(j));
        }
    }
    qCv_.notify_all();
}

void BankService::runWorker() {
    for (;;) {
        Job j;
        {
            std::unique_lock<std::mutex> l(qMtx_);
            qCv_.wait(l, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            j = std::move(queue_.front());
            queue_.pop_front();
            if (j.kind == Kind::User) cancel_ = false;
            else cancelCheck_ = false;
        }
        j.run();
    }
}

// Runs f on the message thread if this service still exists.
void BankService::done(std::function<void()> f) {
    std::weak_ptr<int> alive = alive_;
    juce::MessageManager::callAsync([alive, f = std::move(f)] { if (alive.lock()) f(); });
}

bool BankService::restoreRunning(nd2::BankMachine& m, juce::String& error) {
    Resources& res = Resources::get();
    if (!res.hasOs()) { error = "the OS update file is missing"; return false; }
    const std::vector<uint8_t> state = engine_.getState();
    if (state.empty()) { error = "ND2 is not running yet"; return false; }
    std::string err;
    if (!m.restore(res.os().image, res.os().hash, state, err)) { error = juce::String(err); return false; }
    return true;
}

uint64_t BankService::hashOf(const std::vector<uint8_t>& v) {
    uint64_t h = 1469598103934665603ull;                 // FNV-1a
    for (uint8_t b : v) { h ^= b; h *= 1099511628211ull; }
    return h | 1;
}

void BankService::cancel() { cancel_ = true; }

// ------------------------------------------------------------------ check

void BankService::check() {
    bool all = true;
    for (const Status& s : status_) all = all && s.known;
    const uint64_t checked = all ? checkedFlash_ : 0;
    post({Kind::Check, [this, checked] {
        checking_ = true;
        nd2::BankMachine m;
        juce::String error;
        if (!restoreRunning(m, error)) { checking_ = false; return; }
        const uint64_t flash = hashOf(m.flash());
        if (flash == checked) { checking_ = false; return; }   // nothing was stored since the last check
        for (int bank = 0; bank < nd2::kBanks; ++bank) {
            checkBank_ = bank;
            Status s;
            s.crcs.fill(bankfiles::kEmpty);
            for (int p = 0; p < nd2::kBankPrograms; ++p) {
                if (cancelCheck_) { checkBank_ = -1; checking_ = false; done([this] { check(); }); return; }   // again after the other job
                nd2::BankProgram prog;
                std::string err;
                const auto r = m.readProgram(bank, p, prog, err);
                if (r == nd2::BankMachine::Read::Failed) {
                    nd2diag::log("bank check: bank " + juce::String(bank + 1) + " program " + juce::String(p + 1) + ": " + juce::String(err));
                    checkBank_ = -1; checking_ = false; return;
                }
                if (r == nd2::BankMachine::Read::Stored) { s.crcs[size_t(p)] = nd2::programCrc(prog); ++s.count; }
            }
            s.known = true;
            done([this, bank, s] { status_[size_t(bank)] = s; ++serial_; });
        }
        checkBank_ = -1;
        checking_ = false;
        done([this, flash] { checkedFlash_ = flash; ++serial_; });
    }});
}

// ------------------------------------------------------------------ load

void BankService::load(int bank, const bankfiles::BankFile& file) {
    if (busy_.exchange(true)) return;
    busyText_ = "Loading " + file.name + " into " + bankName(bank);
    done_ = 0; total_ = file.count;
    ++serial_;
    post({Kind::User, [this, bank, file] {
        nd2::BankMachine m;
        juce::String error;
        bool ok = restoreRunning(m, error);
        int written = 0;
        for (int p = 0; ok && p < nd2::kBankPrograms; ++p) {
            if (file.crcs[size_t(p)] == bankfiles::kEmpty) continue;
            if (cancel_) { ok = false; error = "cancelled; " + bankName(bank) + " is unchanged"; break; }
            std::string err;
            if (!m.writeProgram(file.programs[size_t(p)], bank, err)) { ok = false; error = juce::String(err); break; }
            done_ = ++written;
        }
        std::vector<uint8_t> state;
        uint64_t flash = 0;
        if (ok) {
            // The program playing now is in this bank: the OS loads it again, so the new sound plays.
            const int cur = m.currentProgram();
            if (cur >= 0 && cur / nd2::kBankPrograms == bank) m.selectProgram(cur);
            state = m.state();
            flash = hashOf(m.flash());
        }
        done([this, ok, error, bank, file, state = std::move(state), flash, written] {
            busy_ = false;
            if (!ok) { setMessage(bankName(bank) + ": " + error + ".", error.startsWith("cancelled") ? false : true); return; }
            std::string err;
            if (!engine_.setState(state, err)) { setMessage(bankName(bank) + " was not loaded: " + juce::String(err) + ".", true); return; }
            Records r = records();
            r[size_t(bank)] = {true, file.name, file.crcs};
            // What the bank holds now: the file's programs where it has them; other slots as they were.
            Status& s = status_[size_t(bank)];
            const Status before = s;
            s.known = before.known || file.count == nd2::kBankPrograms;
            s.count = 0;
            for (int p = 0; p < nd2::kBankPrograms; ++p) {
                const int c = file.crcs[size_t(p)] != bankfiles::kEmpty ? file.crcs[size_t(p)] : (before.known ? before.crcs[size_t(p)] : bankfiles::kEmpty);
                s.crcs[size_t(p)] = c;
                s.count += c != bankfiles::kEmpty;
                if (c != bankfiles::kEmpty && file.crcs[size_t(p)] == bankfiles::kEmpty) r[size_t(bank)].crcs[size_t(p)] = c;   // kept, not changed
            }
            bool all = true;
            for (const Status& t : status_) all = all && t.known;
            checkedFlash_ = all ? flash : 0;
            setRecords(r);
            juce::String msg = file.name + " loaded into " + bankName(bank) + " (" + juce::String(written) + " programs)";
            if (file.count < nd2::kBankPrograms) msg << "; the file has " << file.count << ", the other programs of " << bankName(bank) << " stay as they were";
            setMessage(msg + ".", false);
            nd2diag::log("bank loaded: " + msg);
        });
    }});
}

// ------------------------------------------------------------------ save

void BankService::save(int bank) {
    if (busy_.exchange(true)) return;
    busyText_ = "Saving " + bankName(bank);
    done_ = 0; total_ = nd2::kBankPrograms;
    ++serial_;
    post({Kind::User, [this, bank] {
        nd2::BankMachine m;
        juce::String error;
        bool ok = restoreRunning(m, error);
        std::vector<nd2::BankProgram> programs;
        Status s;
        s.crcs.fill(bankfiles::kEmpty);
        for (int p = 0; ok && p < nd2::kBankPrograms; ++p) {
            if (cancel_) { ok = false; error = "cancelled; nothing was saved"; break; }
            nd2::BankProgram prog;
            std::string err;
            const auto r = m.readProgram(bank, p, prog, err);
            if (r == nd2::BankMachine::Read::Failed) { ok = false; error = juce::String(err); break; }
            if (r == nd2::BankMachine::Read::Stored) { programs.push_back(prog); s.crcs[size_t(p)] = nd2::programCrc(prog); ++s.count; }
            done_ = p + 1;
        }
        s.known = ok;
        juce::File file;
        if (ok && programs.empty()) { ok = false; error = "it is empty; nothing was saved"; }
        if (ok) {
            const juce::File lib = bankfiles::libraryFolder();
            lib.createDirectory();
            file = bankfiles::freeFile(lib, bankName(bank) + " (" + juce::Time::getCurrentTime().formatted("%d %b").trimCharactersAtStart("0") + ")", ".nd2_bank");
            if (!bankfiles::writeBankFile(file, programs, bank, error)) ok = false;
        }
        done([this, ok, error, bank, s, file, n = int(programs.size())] {
            busy_ = false;
            if (!ok) { setMessage(bankName(bank) + ": " + error + ".", !error.startsWith("cancelled")); return; }
            status_[size_t(bank)] = s;
            Records r = records();
            if (!r[size_t(bank)].set) {                  // an unnamed bank is now known by its file's name
                r[size_t(bank)] = {true, file.getFileNameWithoutExtension(), s.crcs};
                setRecords(r);
            }
            lastSaved_ = file;
            setMessage(bankName(bank) + " saved as " + file.getFileName() + " (" + juce::String(n) + " programs).", false);
        });
    }});
}

// ------------------------------------------------------------------ new instances

void BankService::useForNewInstances(const std::array<juce::String, nd2::kBanks>& names) {
    if (busy_.exchange(true)) return;
    busyText_ = "Making these banks the banks of new instances";
    done_ = 0; total_ = 0;
    ++serial_;
    Records recs = records();
    for (int b = 0; b < nd2::kBanks; ++b) {
        Record& r = recs[size_t(b)];
        const Status& s = status_[size_t(b)];
        if (s.known) r.crcs = s.crcs;
        r.name = names[size_t(b)];
        r.set = r.name.isNotEmpty() && (s.known || r.set);
    }
    post({Kind::User, [this, recs] {
        nd2::BankMachine m;
        juce::String error;
        const bool ok = restoreRunning(m, error);
        std::vector<uint8_t> flash = ok ? m.flash() : std::vector<uint8_t>();
        done([this, ok, error, recs, flash = std::move(flash)]() mutable {
            busy_ = false;
            if (!ok || !Resources::get().replaceFactory(std::move(flash), recs)) {
                setMessage("The banks were not kept for new instances: " + (error.isNotEmpty() ? error : juce::String("no OS")) + ".", true);
                return;
            }
            setMessage("New instances of ND2 will start with these 8 banks.", false);
        });
    }});
}

// ------------------------------------------------------------------ factory files

// Library/Factory from the factory memory, for those who loaded the factory banks before the Library existed:
// one "Factory Bank N" file per bank that holds programs, unless a file with the same programs is already anywhere in
// the Library (the user's own copies of the factory files). Done once per factory memory (a marker in the cache), so
// files the user deletes are not made again.
static juce::File factoryFilesMarker() {
    const Resources& res = Resources::get();
    return res.cacheFolder().getChildFile("factory-" + juce::String::toHexString(juce::int64(res.os().hash)).paddedLeft('0', 16)
                                          + "-" + juce::String::toHexString(juce::int64(std::hash<std::string>()(
                                                std::string(res.factoryFlash().begin(), res.factoryFlash().end())))) + ".library");
}

void BankService::makeFactoryFiles() {
    Resources& res = Resources::get();
    if (!res.hasOs() || !res.hasFactory() || factoryFilesRunning.exchange(true)) return;
    if (factoryFilesMarker().existsAsFile()
        || !bankfiles::factoryFolder().findChildFiles(juce::File::findFiles, false, "*.nd2_bank").isEmpty()) { factoryFilesRunning = false; return; }
    const juce::File marker = factoryFilesMarker();
    const nd2::OsImage os = res.os();
    const std::vector<uint8_t> flash = res.factoryFlash();
    post({Kind::Factory, [this, os, flash, marker] {
        checking_ = true;
        nd2::BankMachine m;
        std::string err;
        bool ok = m.boot(os, flash, err);
        std::vector<std::vector<nd2::BankProgram>> banks(nd2::kBanks);
        for (int b = 0; ok && b < nd2::kBanks; ++b) {
            checkBank_ = b;
            for (int p = 0; ok && p < nd2::kBankPrograms; ++p) {
                if (cancelCheck_) ok = false;
                nd2::BankProgram prog;
                const auto r = ok ? m.readProgram(b, p, prog, err) : nd2::BankMachine::Read::Failed;
                if (r == nd2::BankMachine::Read::Failed) ok = false;
                else if (r == nd2::BankMachine::Read::Stored) banks[size_t(b)].push_back(prog);
                if (r == nd2::BankMachine::Read::Empty && p == 0) break;   // the factory banks are whole banks
            }
        }
        checkBank_ = -1;
        checking_ = false;
        if (!ok) {
            factoryFilesRunning = false;
            if (cancelCheck_) { done([this] { makeFactoryFiles(); }); }   // again after the other job
            return;
        }
        const juce::File folder = bankfiles::factoryFolder();
        int made = 0, present = 0;
        for (int b = 0; b < nd2::kBanks; ++b) {
            if (banks[size_t(b)].empty()) continue;
            std::array<int, nd2::kBankPrograms> crcs;
            crcs.fill(bankfiles::kEmpty);
            for (const auto& p : banks[size_t(b)]) crcs[size_t(p.program)] = nd2::programCrc(p);
            if (bankfiles::findInLibrary(crcs) != juce::File()) { ++present; continue; }   // the user has it already
            folder.createDirectory();
            juce::String e;
            if (bankfiles::writeBankFile(bankfiles::freeFile(folder, "Factory Bank " + juce::String(b + 1), ".nd2_bank"), banks[size_t(b)], b, e)) ++made;
        }
        marker.replaceWithText("done\n");
        factoryFilesRunning = false;
        nd2diag::log("factory bank files: " + juce::String(made) + " made, " + juce::String(present) + " already in the Library");
        done([this] { ++serial_; });
    }});
}
