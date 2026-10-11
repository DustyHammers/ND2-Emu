#include "Resources.h"
#include "OsFiles.h"

namespace {
constexpr const char* kFactoryMagic = "ND2FACT1";
juce::String hex64(uint64_t v) { return juce::String::toHexString(juce::int64(v)).paddedLeft('0', 16); }
}  // namespace

Resources& Resources::get() {
    static Resources* r = new Resources();           // never destroyed: instances may outlive static destruction order
    return *r;
}

Resources::Resources() {
    data_ = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("ND2");
    for (const juce::File& f : {osFolder(), skinsFolder(), cacheFolder()}) f.createDirectory();
    scanOs();
    loadFactoryCache();
}

Resources::~Resources() { stop_ = true; }

// The work ends with a callAsync to the message thread; Resources lives for the process, so nothing joins it.
template <class F> void Resources::runWorker(F&& f) { std::thread(std::forward<F>(f)).detach(); }

// The first file in the os folder that holds a supported OS.
void Resources::scanOs() {
    osOk_ = false;
    for (const auto& entry : juce::RangedDirectoryIterator(osFolder(), false, "*", juce::File::findFiles)) {
        std::string err;
        nd2::OsImage os;
        if (entry.isHidden() || !nd2::loadOsUpdateFile(entry.getFile().getFullPathName().toStdString(), os, err)) continue;
        if (!nd2::isSupportedOs(os)) continue;
        os_ = std::move(os);
        osOk_ = true;
        break;
    }
}

juce::File Resources::factoryCacheFile() const { return cacheFolder().getChildFile("factory-" + hex64(os_.hash) + ".flash"); }

// Cache file: magic, OS hash (8 bytes), flash size (4), the flash.
void Resources::loadFactoryCache() {
    factory_.clear();
    if (!osOk_) return;
    juce::MemoryBlock d;
    if (!factoryCacheFile().loadFileAsData(d) || d.getSize() < 20) return;
    juce::MemoryInputStream in(d, false);
    char magic[8];
    in.read(magic, 8);
    const uint64_t hash = uint64_t(in.readInt64());
    const int size = in.readInt();
    if (std::memcmp(magic, kFactoryMagic, 8) != 0 || hash != os_.hash || size <= 0 || size != int(in.getNumBytesRemaining())) return;
    factory_.resize(size_t(size));
    in.read(factory_.data(), size);
}

void Resources::locateOs(const juce::File& chosen, std::function<void(bool, juce::String)> done) {
    if (locating_.exchange(true)) return;
    runWorker([this, chosen, done] {
        std::vector<uint8_t> cab;
        juce::String error;
        const bool found = osfiles::locateOs(chosen, cab, error);
        juce::MessageManager::callAsync([this, found, cab = std::move(cab), error, done]() mutable {
            locating_ = false;
            if (!found) { done(false, "No OS found: " + error + "."); return; }
            const juce::File dest = osFolder().getChildFile("os.cab");
            if (!dest.replaceWithData(cab.data(), cab.size())) { done(false, "The OS could not be copied to " + dest.getFullPathName() + "."); return; }
            scanOs();
            loadFactoryCache();
            ++serial_;
            done(osOk_, osOk_ ? juce::String("OS v3.00 found and copied to ~/Documents/ND2/os/.") : juce::String("The OS could not be read back."));
        });
    });
}

void Resources::buildFactory(const juce::Array<juce::File>& chosen, std::function<void(bool, juce::String)> done) {
    if (!osOk_ || building_.exchange(true)) return;
    done_ = 0; total_ = 0;
    const nd2::OsImage os = os_;
    runWorker([this, chosen, done, os] {
        std::vector<nd2::BankProgram> programs;
        juce::String note;
        if (!osfiles::readPrograms(chosen, programs, note)) {
            juce::MessageManager::callAsync([this, note, done] { building_ = false; done(false, "No factory presets loaded: " + note + "."); });
            return;
        }
        total_ = int(programs.size());
        nd2::FactoryBuild r = nd2::buildFactoryFlash(os, programs, [this](int d, int) { done_ = d; }, &stop_);
        juce::MessageManager::callAsync([this, r = std::move(r), note, done, hash = os.hash]() mutable {
            building_ = false;
            if (!r.error.empty()) { done(false, "Factory presets were not loaded: " + juce::String(r.error) + "."); return; }
            if (hash != os_.hash) { done(false, "The OS changed while the presets were loading; load them again."); return; }
            factory_ = std::move(r.flash);
            juce::MemoryOutputStream out;
            out.write(kFactoryMagic, 8);
            out.writeInt64(juce::int64(hash));
            out.writeInt(int(factory_.size()));
            out.write(factory_.data(), factory_.size());
            factoryCacheFile().replaceWithData(out.getData(), out.getDataSize());
            ++serial_;
            juce::String msg = juce::String(r.stored) + " factory programs loaded";
            if (r.rejected > 0) msg << " (" << r.rejected << " not accepted by the OS)";
            if (note.isNotEmpty()) msg << "; " << note;
            done(true, msg + ".");
        });
    });
}

juce::File Resources::defaultSkinFolder() {
    if (skinFolder_.isDirectory()) return skinFolder_;
    // The plugin's own bundle: <bundle>/Contents/MacOS/<binary> -> <bundle>/Contents/Resources/nd2-default
    // (stored once per bundle, so a universal binary does not carry it twice).
    const juce::File exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File folder = exe.getParentDirectory().getSiblingFile("Resources").getChildFile("nd2-default");
   #ifdef ND2_DEFAULT_SKIN_DIR
    if (!folder.getChildFile("nd2-default.rml").existsAsFile()) folder = juce::File(ND2_DEFAULT_SKIN_DIR);   // tests
   #endif
    if (!folder.getChildFile("nd2-default.rml").existsAsFile()) return {};
    // Versions up to 0.1.4 unpacked the skin into cache/skin-default-<id>/; those copies are no longer used.
    for (const auto& old : cacheFolder().findChildFiles(juce::File::findDirectories, false, "skin-default-*"))
        old.deleteRecursively();
    skinFolder_ = folder;
    return folder;
}
