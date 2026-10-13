#include "Diag.h"
#include "RmlSkinComponent.h"
#include "SkinSettings.h"
#include <RmlUi/Core/Input.h>
#include "gl/RmlGLRenderer.h"
#include "skin_system.h"
#include "skin_view.h"

#if JUCE_MAC
bool nd2WindowVisible(void* nsView);           // MacWindow.mm
#endif

namespace {
constexpr float kWheelNotch = 0.14f;      // JUCE wheel delta of one mouse-wheel notch (about)

// JUCE image -> premultiplied RGBA bytes, top row first.
bool decodeImage(const std::vector<uint8_t>& file, std::vector<uint8_t>& rgba, int& w, int& h) {
    juce::Image img = juce::ImageFileFormat::loadFrom(file.data(), file.size());
    if (!img.isValid()) return false;
    img = img.convertedToFormat(juce::Image::ARGB);
    w = img.getWidth(); h = img.getHeight();
    rgba.resize(size_t(w) * size_t(h) * 4);
    const juce::Image::BitmapData bd(img, juce::Image::BitmapData::readOnly);
    uint8_t* out = rgba.data();
    for (int y = 0; y < h; ++y) {
        const uint8_t* line = bd.getLinePointer(y);
        for (int x = 0; x < w; ++x, line += bd.pixelStride) {
            const auto* p = reinterpret_cast<const juce::PixelARGB*>(line);                     // premultiplied
            *out++ = p->getRed(); *out++ = p->getGreen(); *out++ = p->getBlue(); *out++ = p->getAlpha();
        }
    }
    return true;
}
}  // namespace

RmlSkinComponent::RmlSkinComponent(ND2Processor& p, juce::File logFile) : proc_(p), logFile_(std::move(logFile)) {
    setOpaque(false);
    setWantsKeyboardFocus(true);
    juce::OpenGLPixelFormat pf;
    pf.stencilBufferBits = 8;
    gl_.setPixelFormat(pf);
    gl_.setOpenGLVersionRequired(juce::OpenGLContext::openGL3_2);
    gl_.setRenderer(this);
    gl_.setComponentPaintingEnabled(true);      // the banners are painted over the skin
    gl_.setContinuousRepainting(false);            // frames only when something changed (timerCallback)
    gl_.attachTo(*this);
    startTimerHz(60);
}

RmlSkinComponent::~RmlSkinComponent() {
    stopTimer();
    gl_.detach();                               // closes the context: openGLContextClosing runs on the GL thread
}

void RmlSkinComponent::post(std::function<void()> f) {
    {
        std::lock_guard<std::mutex> lk(queueMtx_);
        queue_.push_back(std::move(f));
    }
    frameWanted_ = true;
}

void RmlSkinComponent::loadSkin(const juce::File& folder, const juce::String& name) {
    const std::string f = folder.getFullPathName().toStdString(), n = name.toStdString();
    post([this, f, n] { folder_ = f; name_ = n; startLoad(); });
}

void RmlSkinComponent::reload() { post([this] { startLoad(); }); }

void RmlSkinComponent::setDebugger(bool on) {
    debugger_ = on;
    post([this, on] { if (view_) view_->setDebuggerVisible(on); });
}

void RmlSkinComponent::setBanners(std::vector<Banner> banners) {
    if (banners.size() == banners_.size() && std::equal(banners.begin(), banners.end(), banners_.begin())) {
        banners_ = std::move(banners);               // same text: keep the new actions, no repaint
        return;
    }
    banners_ = std::move(banners);
    repaint();
}

void RmlSkinComponent::showMessage(const juce::String& text, int seconds) {
    message_ = text;
    messageUntil_ = juce::Time::getMillisecondCounter() + juce::uint32(seconds * 1000);
    repaint();
}

bool RmlSkinComponent::visibleOnScreen() const {
    if (!isShowing()) return false;
    auto* peer = getPeer();
    if (peer == nullptr || peer->isMinimised()) return false;
#if JUCE_MAC
    return nd2WindowVisible(peer->getNativeHandle());
#else
    return true;
#endif
}

// 60 times a second on the message thread: a frame only when the panel changed, input arrived or the skin's next
// wake time came, and never while the window cannot be seen.
void RmlSkinComponent::timerCallback() {
    if (message_.isNotEmpty() && juce::Time::getMillisecondCounter() > messageUntil_) {
        message_.clear();
        repaint();
    }
    const bool visible = visibleOnScreen();
    if (int(visible) != visibleLogged_) {          // diagnostics: the window became visible / hidden
        visibleLogged_ = int(visible);
        auto* peer = getPeer();
        nd2diag::log(juce::String("panel ") + (visible ? "visible" : "not visible") + " (showing " + (isShowing() ? "yes" : "no")
                     + ", window " + (peer != nullptr ? "yes" : "no") + ", " + juce::String(getWidth()) + " x " + juce::String(getHeight()) + ")");
    }
    if (!visible) return;
    const uint32_t serial = proc_.panelPort().serial();   // the display, the LEDs and the BANKS label's light
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (frameWanted_.exchange(false) || serial != lastSerial_ || now >= nextWake_.load()) {
        lastSerial_ = serial;
        gl_.triggerRepaint();
    }
}

// Skin log: ~/Documents/ND2/skin-log.txt (warnings, Lua errors and Log.Message output of the loaded skin), written
// only while Developer > Write skin log is on; otherwise no file is created.
void RmlSkinComponent::writeLog(void* self, const std::string& line) {
    if (!SkinSettings::skinLog()) return;
    auto* c = static_cast<RmlSkinComponent*>(self);
    if (c->logFile_.getSize() > (1 << 20)) c->logFile_.replaceWithText({});
    c->logFile_.appendText(juce::String::fromUTF8(line.c_str()) + "\n", false, false, "\n");
}

// GL thread: tell the message thread how loading went.
void RmlSkinComponent::startLoad() {
    if (!view_ || name_.empty()) return;
    writeLog(this, "--- " + juce::Time::getCurrentTime().toString(true, true).toStdString() + ": loading skin \"" + name_ + "\" from " + folder_);
    {                                                  // decode the skin's images in parallel before the first frame
        std::vector<std::string> images;
        for (const auto& f : juce::File(juce::String::fromUTF8(folder_.c_str())).findChildFiles(juce::File::findFiles, false, "*.png;*.jpg;*.jpeg;*.gif"))
            images.push_back(f.getFullPathName().toStdString());
        renderer_->prefetch(images);
        dropPrefetch_ = true;
    }
    std::string err;
    const bool ok = view_->load(folder_, name_, err);
    writeLog(this, ok ? std::string("loaded") : "not loaded: " + err);
    if (ok && debugger_) view_->setDebuggerVisible(true);
    frameWanted_ = true;                           // draw the loaded skin (nothing else may ask for a frame)
    juce::Component::SafePointer<RmlSkinComponent> self(this);
    juce::MessageManager::callAsync([self, ok, err] {
        if (self && self->onLoaded) self->onLoaded(ok, juce::String::fromUTF8(err.c_str()));
    });
}

// ------------------------------------------------------------------ GL thread

void RmlSkinComponent::newOpenGLContextCreated() {
    nd2diag::log("OpenGL context created");
    std::string message;
    juce::String error;
    if (!RmlGLRenderer::loadGL(message)) error = "OpenGL could not be started: " + juce::String(message);
    else {
        renderer_ = std::make_unique<RmlGLRenderer>(decodeImage);
        if (!renderer_->ok()) { error = "The OpenGL skin renderer could not be started."; renderer_.reset(); }
    }
    if (error.isNotEmpty()) {
        nd2diag::log("OpenGL: " + error);
        juce::Component::SafePointer<RmlSkinComponent> self(this);
        juce::MessageManager::callAsync([self, error] { if (self && self->onLoaded) self->onLoaded(false, error); });
        return;
    }
    skin::PluginInfo info{"ND2", "DHammers", JucePlugin_VersionString};
    view_ = std::make_unique<skin::SkinView>(proc_.panelPort(), info, renderer_->rml());
    view_->onLog = &RmlSkinComponent::writeLog;
    view_->onLogUser = this;
}

void RmlSkinComponent::renderOpenGL() {
    JUCE_AUTORELEASEPOOL                                  // macOS: JUCE's render thread drains no pool of its own
    if (!view_) return;
    std::vector<std::function<void()>> work;
    {
        std::lock_guard<std::mutex> lk(queueMtx_);
        work.swap(queue_);
    }
    for (auto& f : work) f();
    const float scale = float(gl_.getRenderingScale());
    scale_ = scale;
    const int w = std::max(1, juce::roundToInt(float(width_.load()) * scale));
    const int h = std::max(1, juce::roundToInt(float(height_.load()) * scale));
    const float dp = zoom_.load() * scale;
    if (w != viewW_ || h != viewH_ || std::abs(dp - viewDp_) > 1e-4f) {
        viewW_ = w; viewH_ = h; viewDp_ = dp;
        view_->setViewport(w, h, dp);
    }
    view_->update(juce::Time::getMillisecondCounterHiRes() / 1000.0);
    nextWake_ = view_->nextWake();
    std::lock_guard<std::recursive_mutex> lk(skin::skinLock());      // RmlUi's shared state, also used by the renderer
    renderer_->beginFrame(w, h);
    view_->render();
    renderer_->endFrame();
    if (dropPrefetch_) { renderer_->dropPrefetched(); dropPrefetch_ = false; }   // images the skin did not use
    if (framesDrawn_ == 0) nd2diag::log("first frame drawn " + juce::String(w) + " x " + juce::String(h) + " px");
    ++framesDrawn_;
}

void RmlSkinComponent::openGLContextClosing() {
    std::lock_guard<std::recursive_mutex> lk(skin::skinLock());
    view_.reset();
    renderer_.reset();
}

// ------------------------------------------------------------------ message thread: banners and input

void RmlSkinComponent::paint(juce::Graphics& g) {
    // Resize grip (the editor's own corner resizer sits here but is hidden by the OpenGL view).
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    for (int i = 1; i <= 3; ++i) {
        const float d = float(i) * 4.0f;
        g.drawLine(float(getWidth()) - d, float(getHeight()) - 2.0f, float(getWidth()) - 2.0f, float(getHeight()) - d, 1.0f);
    }
    shown_ = banners_;
    if (message_.isNotEmpty()) shown_.push_back(Banner{{}, message_, {}});
    hits_ = banner::draw(g, getLocalBounds(), shown_);
}

bool RmlSkinComponent::bannerClick(juce::Point<int> p) {
    if (p.y >= banner::height(shown_.size())) return false;
    for (const banner::Hit& h : hits_)
        if (h.r.contains(p)) {
            if (auto f = shown_[size_t(h.banner)].buttons[size_t(h.button)].action) juce::MessageManager::callAsync(f);
            return true;
        }
    // A click on the timed message (the last banner) hides it; clicks elsewhere on a banner do nothing.
    if (message_.isNotEmpty() && p.y >= banner::height(shown_.size() - 1)) showMessage({});
    return true;
}

void RmlSkinComponent::resized() { width_ = getWidth(); height_ = getHeight(); frameWanted_ = true; }

int RmlSkinComponent::toPx(float v) const { return juce::roundToInt(v * scale_.load()); }

int RmlSkinComponent::mods(const juce::ModifierKeys& m) {
    return (m.isCtrlDown() ? skin::ModCtrl : 0) | (m.isShiftDown() ? skin::ModShift : 0) |
           (m.isAltDown() ? skin::ModAlt : 0) | (m.isCommandDown() ? skin::ModMeta : 0);
}

void RmlSkinComponent::mouseMove(const juce::MouseEvent& e) {
    const bool corner = onCornerResize && e.x >= getWidth() - kCornerSize && e.y >= getHeight() - kCornerSize;
    const bool overButton = std::any_of(hits_.begin(), hits_.end(), [&](const banner::Hit& h) { return h.r.contains(e.getPosition()); });
    setMouseCursor(corner ? juce::MouseCursor::BottomRightCornerResizeCursor
                          : overButton || hoverClickable_.load() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    const int x = toPx(e.position.x), y = toPx(e.position.y), m = mods(e.mods);
    post([this, x, y, m] { if (view_) { view_->mouseMove(x, y, m); hoverClickable_ = view_->hoverClickable(); } });
}

void RmlSkinComponent::mouseDrag(const juce::MouseEvent& e) {
    if (cornerDrag_) { if (onCornerResize) onCornerResize(1, e.getScreenPosition() - cornerStart_); return; }
    if (inputCaptured_) return;
    const int x = toPx(e.position.x), y = toPx(e.position.y), m = mods(e.mods);
    post([this, x, y, m] { if (view_) view_->mouseMove(x, y, m); });
}

void RmlSkinComponent::mouseDown(const juce::MouseEvent& e) {
    inputCaptured_ = false;
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(e); return; }          // right-click never presses
    if (bannerClick(e.getPosition())) { inputCaptured_ = true; return; }
    if (onCornerResize && e.x >= getWidth() - kCornerSize && e.y >= getHeight() - kCornerSize) {
        cornerDrag_ = true;
        cornerStart_ = e.getScreenPosition();
        onCornerResize(0, {});
        return;
    }
    grabKeyboardFocus();
    if (onLeftClick) onLeftClick();
    const int x = toPx(e.position.x), y = toPx(e.position.y), m = mods(e.mods);
    const int button = e.mods.isMiddleButtonDown() ? 2 : 0;
    post([this, x, y, m, button] { if (view_) { view_->mouseMove(x, y, m); view_->mouseDown(button, m); } });
}

void RmlSkinComponent::mouseUp(const juce::MouseEvent& e) {
    if (cornerDrag_) { cornerDrag_ = false; if (onCornerResize) onCornerResize(2, e.getScreenPosition() - cornerStart_); return; }
    if (inputCaptured_ || e.mods.isPopupMenu()) { inputCaptured_ = false; return; }
    const int m = mods(e.mods);
    const int button = e.mods.isMiddleButtonDown() ? 2 : 0;
    post([this, m, button] { if (view_) view_->mouseUp(button, m); });
}

void RmlSkinComponent::mouseExit(const juce::MouseEvent&) { post([this] { if (view_) view_->mouseLeave(); }); }

void RmlSkinComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    const float notches = -w.deltaY / kWheelNotch;          // RmlUi: positive = towards the user
    const int m = mods(e.mods);
    post([this, notches, m] { if (view_) view_->mouseWheel(notches, m); });
}

bool RmlSkinComponent::keyPressed(const juce::KeyPress& k) {
    if (onKey && onKey(k)) return true;
    if (k.getKeyCode() == juce::KeyPress::F5Key) { reload(); return true; }
    if (k == juce::KeyPress::escapeKey) {
        post([this] { if (view_) view_->releaseHeldButtons(); });
        return true;
    }
    return false;
}
