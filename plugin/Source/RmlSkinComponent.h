// A skin (RmlUi document) drawn with OpenGL, with the message banners painted over it.
//
// Everything RmlUi and Lua do for this window runs on the window's OpenGL thread: the skin view is created there
// when the GL context starts, input is queued to it, and it is destroyed there when the context closes. Results
// (skin loaded or failed) come back to the message thread through onLoaded.
#pragma once
#include <juce_opengl/juce_opengl.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "Banner.h"
#include "PluginProcessor.h"

class RmlGLRenderer;
namespace skin { class SkinView; }

class RmlSkinComponent : public juce::Component, private juce::OpenGLRenderer, private juce::Timer {
public:
    RmlSkinComponent(ND2Processor& p, juce::File logFile);
    ~RmlSkinComponent() override;

    // Loads <folder>/<name>.rml (asynchronously). onLoaded(ok, error) follows on the message thread.
    void loadSkin(const juce::File& folder, const juce::String& name);
    void reload();
    void setZoom(float zoom) { zoom_ = zoom; }
    void setDebugger(bool on);
    // Banners that stay until replaced (the OS or the presets missing, progress), and one message that goes away
    // after `seconds` or a click.
    void setBanners(std::vector<Banner> banners);
    void showMessage(const juce::String& text, int seconds = 10);

    std::function<void(bool ok, const juce::String& error)> onLoaded;
    std::function<void(const juce::MouseEvent&)> onRightClick;
    // Drags in the bottom-right corner resize the window (the OpenGL view would hide the editor's own resizer):
    // phase 0 = start, 1 = drag (offset from the start, screen pixels), 2 = end.
    std::function<void(int phase, juce::Point<int> offset)> onCornerResize;
    static constexpr int kCornerSize = 24;

    uint64_t framesDrawn() const { return framesDrawn_.load(); }
    bool visibleOnScreen() const;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void timerCallback() override;
    void post(std::function<void()> f);
    void startLoad();
    int toPx(float v) const;
    bool bannerClick(juce::Point<int> p);
    static int mods(const juce::ModifierKeys& m);
    static void writeLog(void* self, const std::string& line);

    ND2Processor& proc_;
    juce::File logFile_;
    juce::OpenGLContext gl_;
    std::unique_ptr<RmlGLRenderer> renderer_;
    std::unique_ptr<skin::SkinView> view_;
    std::mutex queueMtx_;
    std::vector<std::function<void()>> queue_;
    std::atomic<int> width_{1}, height_{1};
    std::atomic<float> zoom_{1.0f}, scale_{1.0f};
    std::string folder_, name_;            // the skin to show (GL thread)
    int viewW_ = 0, viewH_ = 0;            // last viewport given to the view (GL thread)
    float viewDp_ = 0;
    bool debugger_ = false;
    bool cornerDrag_ = false, inputCaptured_ = false;
    juce::Point<int> cornerStart_;
    std::vector<Banner> banners_;
    juce::String message_;
    juce::uint32 messageUntil_ = 0;
    std::vector<banner::Hit> hits_;        // the banner buttons as last painted
    std::vector<Banner> shown_;            // the banners those hits belong to

    // Frames only when needed: the timer asks for one when the panel changed (engine UI counter), input arrived,
    // or the skin's next wake time came, and never while the window cannot be seen.
    std::atomic<bool> frameWanted_{true};
    std::atomic<double> nextWake_{0};
    uint32_t lastSerial_ = 0, lastTrig_ = 0;
    std::atomic<uint64_t> framesDrawn_{0};
};
