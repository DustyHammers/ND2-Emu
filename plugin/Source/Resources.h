// What every ND2 instance in the process shares: the data folder, the OS (from ~/Documents/ND2/os/) and the factory
// memory (built once from the user's factory bank files, cached in ~/Documents/ND2/cache/). Message thread only,
// except where noted.
//
//   ~/Documents/ND2/os/       the user's OS update (the original os.cab, copied there when it was located)
//   ~/Documents/ND2/skins/    the user's skins: <name>/<name>.rml
//   ~/Documents/ND2/cache/    the factory memory made from the user's bank files; the unpacked Default skin
#pragma once
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>
#include "banks.h"
#include "rom.h"

class Resources {
public:
    static Resources& get();                         // created on first use, lives for the process

    juce::File dataFolder() const { return data_; }
    juce::File osFolder() const { return data_.getChildFile("os"); }
    juce::File skinsFolder() const { return data_.getChildFile("skins"); }
    juce::File cacheFolder() const { return data_.getChildFile("cache"); }

    // ---- the OS
    bool hasOs() const { return osOk_; }
    const nd2::OsImage& os() const { return os_; }
    // Looks for the OS in what the user chose (any form, see osfiles::locateOs) on a worker thread; on success the
    // original os.cab is copied to the os folder and every instance starts. done(ok, message) on the message thread.
    void locateOs(const juce::File& chosen, std::function<void(bool, juce::String)> done);
    bool locating() const { return locating_.load(); }

    // ---- the factory memory
    bool hasFactory() const { return !factory_.empty(); }
    const std::vector<uint8_t>& factoryFlash() const { return factory_; }
    // Reads the chosen bank files and stores their programs in a fresh machine through the OS (worker thread); the
    // result is cached for this OS. done(ok, message) on the message thread.
    void buildFactory(const juce::Array<juce::File>& chosen, std::function<void(bool, juce::String)> done);
    bool building() const { return building_.load(); }
    void buildProgress(int& done, int& total) const { done = done_.load(); total = total_.load(); }
    bool factoryDeclined = false;                    // "Not now" (this session)

    // Changes whenever the OS or the factory memory changes (instances react to it).
    uint32_t serial() const { return serial_.load(); }

    // The Default skin unpacked from the plugin into the cache folder (once per build of the plugin). Returns its
    // folder (empty on failure).
    juce::File defaultSkinFolder();

private:
    Resources();
    ~Resources();
    void scanOs();
    void loadFactoryCache();
    juce::File factoryCacheFile() const;
    template <class F> void runWorker(F&& f);

    juce::File data_;
    nd2::OsImage os_;
    bool osOk_ = false;
    std::vector<uint8_t> factory_;
    std::atomic<uint32_t> serial_{1};
    std::atomic<bool> locating_{false}, building_{false}, stop_{false};
    std::atomic<int> done_{0}, total_{0};
    juce::File skinFolder_;
};
