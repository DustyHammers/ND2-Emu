// Global plugin settings (all instances and projects; not saved in projects and not synth parameters): the chosen
// skin and the window size. Stored in ~/Library/Application Support/ND2/ND2.settings.
#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include "WindowScale.h"

namespace SkinSettings {

inline const juce::String kDefault = "Default";       // the skin built into the plugin

inline juce::PropertiesFile::Options options() {
    juce::PropertiesFile::Options o;
    o.applicationName = "ND2";
    o.filenameSuffix = "settings";
    o.folderName = "ND2";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    return o;
}
inline juce::String get(const char* key) { return juce::PropertiesFile(options()).getValue(key); }
inline void set(const char* key, const juce::var& v) {
    juce::PropertiesFile f(options());
    f.setValue(key, v);
    f.saveIfNeeded();
}

// Empty when never chosen: the Default skin.
inline juce::String skin() { const juce::String s = get("skin"); return s.isEmpty() ? kDefault : s; }
inline void setSkin(const juce::String& name) { set("skin", name); }

// The last window size (winscale: 0.5-2.0 of the skin's 100 % size), for instances without a size of their own.
inline float scale() {
    const juce::String s = get("windowScale");
    const float v = s.isNotEmpty() ? s.getFloatValue() : 1.0f;
    return v >= winscale::kMin && v <= winscale::kMax ? v : 1.0f;
}
inline void setScale(float v) { set("windowScale", juce::String(v, 4)); }

// Developer > Write skin log (default off): ~/Documents/ND2/skin-log.txt is written only while this is on. Kept in
// memory after the first read (the log is written from the GL thread), shared by all instances in the process.
inline std::atomic<int> skinLogCache{-1};
inline bool skinLog() {
    int v = skinLogCache.load();
    if (v < 0) { v = get("skinLog").getIntValue() != 0 || std::getenv("ND2_SKIN_LOG") != nullptr ? 1 : 0; skinLogCache = v; }
    return v != 0;
}
inline void setSkinLog(bool on) { skinLogCache = on ? 1 : 0; set("skinLog", on ? 1 : 0); }

}  // namespace SkinSettings
