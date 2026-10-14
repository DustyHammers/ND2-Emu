#include "BankFiles.h"
#include "bankio.h"
#include <cstring>

namespace bankfiles {

namespace {
constexpr juce::int64 kMaxBankFile = 1 << 20;        // a bank file is about 12 KB
}

// "BNK1", then per bank: set (byte), name (UTF-8 string), 50 CRCs (int32, -1 = empty)
void writeRecords(juce::OutputStream& out, const BankRecords& r) {
    out.write("BNK1", 4);
    for (const BankRecord& b : r) {
        out.writeByte(b.set ? 1 : 0);
        out.writeString(b.name);
        for (int c : b.crcs) out.writeInt(c);
    }
}

bool readRecords(juce::InputStream& in, BankRecords& r) {
    char magic[4] = {};
    if (in.read(magic, 4) != 4 || std::memcmp(magic, "BNK1", 4) != 0) return false;
    BankRecords got;
    for (BankRecord& b : got) {
        b.set = in.readByte() != 0;
        b.name = in.readString();
        for (int& c : b.crcs) c = in.readInt();
        if (in.isExhausted() && &b != &got.back()) return false;
    }
    r = got;
    return true;
}

juce::File libraryFolder() {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("ND2").getChildFile("Library");
}

juce::String shownFolder(const juce::String& sub) {
   #if JUCE_WINDOWS
    return ("Documents/ND2/" + sub).replaceCharacter('/', '\\');
   #else
    return "~/Documents/ND2/" + sub;
   #endif
}

juce::File factoryFolder() { return libraryFolder().getChildFile("Factory"); }

bool readBankFile(const juce::File& f, BankFile& out, juce::String& error) {
    out = BankFile();
    out.file = f;
    out.name = f.getFileNameWithoutExtension();
    out.crcs.fill(kEmpty);
    if (!f.existsAsFile() || f.getSize() > kMaxBankFile) { error = "not a bank file"; return false; }
    juce::ZipFile zip(f);
    if (zip.getNumEntries() == 0) { error = "not a bank file (no programs in it)"; return false; }
    int bad = 0, bank = -2;
    for (int i = 0; i < zip.getNumEntries(); ++i) {
        const auto* e = zip.getEntry(i);
        if (e == nullptr || !e->filename.endsWithIgnoreCase(".nd2p") || e->uncompressedSize != 201) continue;
        std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(i));
        if (!in) { ++bad; continue; }
        juce::MemoryBlock d;
        in->readIntoMemoryBlock(d);
        nd2::BankProgram p;
        std::string err;
        if (!nd2::parseProgramFile(static_cast<const uint8_t*>(d.getData()), d.getSize(), p, err)) { ++bad; continue; }
        const size_t slot = size_t(p.program);
        if (out.crcs[slot] == kEmpty) ++out.count;
        out.programs[slot] = p;
        out.crcs[slot] = nd2::programCrc(p);
        bank = bank == -2 ? p.bank : (bank == p.bank ? bank : -1);
    }
    out.sourceBank = bank < 0 ? -1 : bank;
    if (out.count == 0) { error = bad > 0 ? "its programs are damaged (checksum)" : "there are no programs in it"; return false; }
    return true;
}

bool writeBankFile(const juce::File& f, const std::vector<nd2::BankProgram>& programs, int bank, juce::String& error) {
    juce::ZipFile::Builder zip;
    const juce::Time now = juce::Time::getCurrentTime();
    const uint32_t id = uint32_t(now.toMilliseconds() / 1000);
    for (const nd2::BankProgram& src : programs) {
        nd2::BankProgram p = src;
        p.bank = bank;
        const std::vector<uint8_t> file = nd2::programFile(p, id + uint32_t(p.program));
        zip.addEntry(new juce::MemoryInputStream(file.data(), file.size(), true), 0,
                     "Program " + juce::String(p.program).paddedLeft('0', 3) + ".nd2p", now);
    }
    juce::TemporaryFile tmp(f);
    {
        std::unique_ptr<juce::FileOutputStream> out(tmp.getFile().createOutputStream());
        if (!out || !zip.writeToStream(*out, nullptr)) { error = "the file could not be written"; return false; }
    }
    if (f.exists() || !tmp.overwriteTargetFileWithTemporary()) { error = "the file could not be written"; return false; }
    return true;
}

std::vector<BankFile> scanLibrary() {
    std::vector<BankFile> out;
    const juce::File lib = libraryFolder(), fac = factoryFolder();
    auto addFrom = [&](const juce::File& folder, bool recursive, bool factory) {
        juce::Array<juce::File> files = folder.findChildFiles(juce::File::findFiles, recursive, "*.nd2_bank");
        juce::StringArray paths;
        for (const auto& f : files)
            if (!f.isHidden() && (factory || !f.isAChildOf(fac))) paths.add(f.getFullPathName());
        paths.sortNatural();
        for (const auto& p : paths) {
            BankFile b;
            juce::String err;
            if (!readBankFile(juce::File(p), b, err)) continue;
            b.factory = factory;
            out.push_back(std::move(b));
        }
    };
    addFrom(fac, false, true);
    addFrom(lib, true, false);
    return out;
}

juce::File findInLibrary(const std::array<int, nd2::kBankPrograms>& crcs) {
    for (const auto& f : libraryFolder().findChildFiles(juce::File::findFiles, true, "*.nd2_bank")) {
        BankFile b;
        juce::String err;
        if (readBankFile(f, b, err) && b.crcs == crcs) return f;
    }
    return {};
}

juce::File freeFile(const juce::File& folder, const juce::String& base, const juce::String& ext) {
    juce::File f = folder.getChildFile(base + ext);
    for (int n = 2; f.exists(); ++n) f = folder.getChildFile(base + " " + juce::String(n) + ext);
    return f;
}

bool inLibrary(const juce::File& f) { return f.isAChildOf(libraryFolder()); }

juce::File addToLibrary(const juce::File& f, bool factory, juce::String& error) {
    if (inLibrary(f)) return f;
    const juce::File folder = factory ? factoryFolder() : libraryFolder();
    if (!folder.createDirectory()) { error = "the Library folder could not be made"; return {}; }
    const juce::File to = freeFile(folder, f.getFileNameWithoutExtension(), ".nd2_bank");
    if (!f.copyFileTo(to)) { error = "the file could not be copied into the Library folder"; return {}; }
    return to;
}

}  // namespace bankfiles
