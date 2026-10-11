// Diagnostic log: what happens between the host loading ND2 and its window appearing, for hosts where something goes
// wrong that cannot be seen from outside (e.g. Logic Pro, which runs plugins in its AUHostingService helper process).
// Each line goes to ~/Documents/ND2/ND2-log.txt (kept under 256 KB) and to the macOS system log (Console.app,
// search "ND2:"). Short lines, no audio-thread use.
#pragma once
#include <juce_core/juce_core.h>
#if !JUCE_WINDOWS
#include <unistd.h>
#endif

#if JUCE_MAC
void nd2SystemLog(const char* line);                  // MacWindow.mm (NSLog)
#endif

namespace nd2diag {

inline juce::String arch() {
   #if defined(__aarch64__) || defined(__arm64__)
    return "arm64";
   #elif defined(__x86_64__)
    return "x86_64";
   #else
    return "other";
   #endif
}

inline void log(const juce::String& what) {
    static juce::CriticalSection lock;
    const juce::ScopedLock sl(lock);
    const juce::String line = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S") + " ["
                              + juce::File::getSpecialLocation(juce::File::currentApplicationFile).getFileNameWithoutExtension()
                              + " " + juce::String(int(getpid())) + "] " + what;
   #if JUCE_MAC
    nd2SystemLog(("ND2: " + line).toRawUTF8());
   #endif
    const juce::File f = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("ND2").getChildFile("ND2-log.txt");
    if (f.getSize() > 256 * 1024) f.replaceWithText({});
    f.appendText(line + "\n", false, false, "\n");
}

}  // namespace nd2diag
