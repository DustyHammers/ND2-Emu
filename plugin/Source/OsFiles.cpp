#include "OsFiles.h"
#include <map>
#include "rom.h"

namespace osfiles {
namespace {

constexpr juce::int64 kMaxFileBytes = juce::int64(256) << 20;   // anything larger is not an update file
constexpr int kMaxDepth = 6;                                       // folders and zips inside zips

bool cabIn(const juce::MemoryBlock& data, std::vector<uint8_t>& cab) {
    const auto* p = static_cast<const uint8_t*>(data.getData());
    return nd2::extractOsCab(std::vector<uint8_t>(p, p + data.getSize()), cab);
}

bool osInZip(juce::InputStream* source, std::vector<uint8_t>& cab, int depth);

bool osInData(const juce::MemoryBlock& data, const juce::String& name, std::vector<uint8_t>& cab, int depth) {
    if (cabIn(data, cab)) return true;
    if (depth < kMaxDepth && name.endsWithIgnoreCase(".zip")) {
        juce::MemoryInputStream in(data, false);
        return osInZip(&in, cab, depth + 1);
    }
    return false;
}

bool osInZip(juce::InputStream* source, std::vector<uint8_t>& cab, int depth) {
    juce::ZipFile zip(source, false);
    for (int i = 0; i < zip.getNumEntries(); ++i) {
        const auto* e = zip.getEntry(i);
        if (e == nullptr || e->isSymbolicLink || e->uncompressedSize > kMaxFileBytes || e->filename.endsWithChar('/')) continue;
        std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(i));
        if (!in) continue;
        juce::MemoryBlock data;
        in->readIntoMemoryBlock(data);
        if (osInData(data, e->filename, cab, depth)) return true;
    }
    return false;
}

bool osInFile(const juce::File& f, std::vector<uint8_t>& cab, int depth) {
    if (f.isDirectory()) {
        if (depth >= kMaxDepth) return false;
        // The Mac updater app keeps it at Contents/Resources/os.cab; look there first, then everywhere.
        const juce::File direct = f.getChildFile("Contents/Resources/os.cab");
        if (direct.existsAsFile() && osInFile(direct, cab, depth + 1)) return true;
        for (const auto& entry : juce::RangedDirectoryIterator(f, false, "*", juce::File::findFilesAndDirectories))
            if (!entry.isHidden() && osInFile(entry.getFile(), cab, depth + 1)) return true;
        return false;
    }
    if (!f.existsAsFile() || f.getSize() > kMaxFileBytes) return false;
    juce::MemoryBlock data;
    if (!f.loadFileAsData(data)) return false;
    return osInData(data, f.getFileName(), cab, depth);
}

#if JUCE_MAC
// Opens a .dmg read-only without showing it, looks inside, and closes it again.
bool osInDmg(const juce::File& dmg, std::vector<uint8_t>& cab, juce::String& error) {
    juce::ChildProcess attach;
    if (!attach.start(juce::StringArray{"/usr/bin/hdiutil", "attach", "-nobrowse", "-readonly", "-noautoopen", "-plist",
                                        dmg.getFullPathName()})) {
        error = "the disk image could not be opened here; open it in Finder and choose the updater app on it";
        return false;
    }
    const juce::String out = attach.readAllProcessOutput();
    attach.waitForProcessToFinish(60000);
    juce::StringArray mounts;
    for (int pos = out.indexOf("<key>mount-point</key>"); pos >= 0; pos = out.indexOf(pos + 1, "<key>mount-point</key>")) {
        const int s = out.indexOf(pos, "<string>"), e = out.indexOf(pos, "</string>");
        if (s > 0 && e > s) mounts.add(out.substring(s + 8, e));
    }
    if (mounts.isEmpty()) {
        error = "the disk image could not be opened here; open it in Finder and choose the updater app on it";
        return false;
    }
    bool found = false;
    for (const juce::String& m : mounts) if (!found) found = osInFile(juce::File(m), cab, 1);
    for (const juce::String& m : mounts) {
        juce::ChildProcess detach;
        if (detach.start(juce::StringArray{"/usr/bin/hdiutil", "detach", m, "-quiet"})) detach.waitForProcessToFinish(30000);
    }
    return found;
}
#endif

}  // namespace

bool locateOs(const juce::File& chosen, std::vector<uint8_t>& cab, juce::String& error) {
    error.clear();
#if JUCE_MAC
    if (chosen.hasFileExtension("dmg")) {
        if (osInDmg(chosen, cab, error)) return true;
        if (error.isEmpty()) error = "there is no OS v3.00 update inside this disk image";
        return false;
    }
#endif
    if (osInFile(chosen, cab, 0)) return true;
    error = chosen.isDirectory() ? "there is no OS v3.00 update in this folder"
                                 : "this is not the OS v3.00 update file (or a file that contains it)";
    return false;
}

// ------------------------------------------------------------------ factory banks

namespace {

void programsInData(const juce::MemoryBlock& data, const juce::String& name, std::map<int, nd2::BankProgram>& out,
                    int& bad, int depth);

void programsInZip(juce::InputStream* source, std::map<int, nd2::BankProgram>& out, int& bad, int depth) {
    juce::ZipFile zip(source, false);
    // Entries in name order, so a bank's programs (and banks in a folder) go in their natural order.
    juce::StringArray names;
    for (int i = 0; i < zip.getNumEntries(); ++i) names.add(zip.getEntry(i)->filename);
    names.sortNatural();
    for (const juce::String& n : names) {
        const int i = zip.getIndexOfFileName(n);
        const auto* e = zip.getEntry(i);
        if (e == nullptr || e->uncompressedSize > kMaxFileBytes || n.endsWithChar('/')) continue;
        std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(i));
        if (!in) continue;
        juce::MemoryBlock data;
        in->readIntoMemoryBlock(data);
        programsInData(data, n, out, bad, depth + 1);
    }
}

void programsInData(const juce::MemoryBlock& data, const juce::String& name, std::map<int, nd2::BankProgram>& out,
                    int& bad, int depth) {
    if (name.endsWithIgnoreCase(".nd2p")) {
        nd2::BankProgram p;
        std::string err;
        if (nd2::parseProgramFile(static_cast<const uint8_t*>(data.getData()), data.getSize(), p, err)) out[p.bank * 50 + p.program] = p;
        else ++bad;
    } else if (depth < kMaxDepth && (name.endsWithIgnoreCase(".nd2_bank") || name.endsWithIgnoreCase(".zip"))) {
        juce::MemoryInputStream in(data, false);
        programsInZip(&in, out, bad, depth);
    }
}

void programsInFile(const juce::File& f, std::map<int, nd2::BankProgram>& out, int& bad, int depth) {
    if (f.isDirectory()) {
        if (depth >= kMaxDepth) return;
        juce::Array<juce::File> kids = f.findChildFiles(juce::File::findFilesAndDirectories, false);
        kids.sort();
        for (const juce::File& k : kids) if (!k.isHidden()) programsInFile(k, out, bad, depth + 1);
        return;
    }
    if (!f.existsAsFile() || f.getSize() > kMaxFileBytes) return;
    juce::MemoryBlock data;
    if (f.loadFileAsData(data)) programsInData(data, f.getFileName(), out, bad, depth);
}

}  // namespace

bool readPrograms(const juce::Array<juce::File>& chosen, std::vector<nd2::BankProgram>& out, juce::String& error) {
    std::map<int, nd2::BankProgram> found;
    int bad = 0;
    for (const juce::File& f : chosen) programsInFile(f, found, bad, 0);
    out.clear();
    for (auto& kv : found) out.push_back(kv.second);
    if (out.empty()) {
        error = bad > 0 ? "the program files in it are damaged (checksum)" : "there are no bank or program files in it";
        return false;
    }
    error = bad > 0 ? juce::String(bad) + " damaged program file(s) were skipped" : juce::String();
    return true;
}

}  // namespace osfiles
