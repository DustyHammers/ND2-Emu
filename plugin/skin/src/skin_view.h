// One loaded skin in one plugin window: an RmlUi context with the skin's document, its Lua environment, and the
// bindings between the custom elements (pbutton, led, knob, encoder, segdigit, action) and the instrument (PanelPort).
//
// Threading: RmlUi and Lua are not thread-safe and their state is shared by every plugin instance in the process.
// Every SkinView method takes the global skin lock, so views on different threads (one render thread per window)
// take turns. Call all methods of one view from one thread (the plugin uses its OpenGL thread).
#pragma once
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "skin_port.h"

namespace Rml { class Context; class ElementDocument; class Element; class RenderInterface; }

namespace skin {

class KnobElement;
class EncoderElement;
class PButtonElement;
class LedElement;
class SegDigitElement;
class ActionElement;
class BendPotElement;
class BendLedElement;

constexpr double kFrameEventHz = 60;          // body onframe at most this often
constexpr double kLuaTimeoutSeconds = 3;      // longest a single entry into Lua may run
constexpr float kBodyWidthDp = 1725, kBodyHeightDp = 1035;   // document body size; window = body x zoom

// Modifier bits for input (same values as Rml::Input::KeyModifier).
enum Modifier { ModCtrl = 1, ModShift = 2, ModAlt = 4, ModMeta = 8 };

class SkinView {
public:
    // render: the renderer for this view's context (nullptr: a no-op renderer, for tests). Must outlive the view.
    SkinView(PanelPort& port, PluginInfo info, Rml::RenderInterface* render);
    ~SkinView();
    SkinView(const SkinView&) = delete;
    SkinView& operator=(const SkinView&) = delete;

    // Loads <folder>/<name>.rml. On failure returns false with a plain-language reason (the view is then empty).
    bool load(const std::string& folder, const std::string& name, std::string& error);
    bool loaded() const { return document_ != nullptr; }
    const std::string& folder() const { return folder_; }

    // Window size in physical pixels and the dp ratio (zoom x display scale).
    void setViewport(int widthPx, int heightPx, float dpRatio);

    // Once per displayed frame: reads the instrument (LEDs, display, master), runs Lua callbacks and the frame
    // event, and lays out the document. now = seconds on a steady clock.
    void update(double now);
    void render();
    // When this view next needs a frame although the instrument did not change (an RmlUi animation or transition
    // runs, the skin has an onframe handler): seconds on update()'s clock, infinity when still. Valid after update().
    double nextWake() const { return nextWake_; }
    struct Stats { uint64_t frames = 0, changedFrames = 0, luaCalls = 0; };
    Stats stats() const;
    void countLuaCall() { ++luaCalls_; }

    // Input in physical pixels (same space as setViewport). button: 0 left, 1 right, 2 middle.
    void mouseMove(int x, int y, int mods);
    void mouseDown(int button, int mods);
    void mouseUp(int button, int mods);
    void mouseWheel(float deltaY, int mods);   // positive: towards the user (scroll down)
    void mouseLeave();
    void releaseHeldButtons();                 // Esc
    // The pointer is over something that acts on a click besides the panel's own controls (the display: typed
    // program entry; plugin actions such as BANKS). Valid after mouseMove.
    bool hoverClickable() const { return hoverClickable_; }
    void keyDown(int rmlKey, int mods);
    void keyUp(int rmlKey, int mods);

    void setDebuggerVisible(bool on);
    bool debuggerVisible() const { return debugger_; }

    // Tests: runs Lua code in this view's environment; returns the first result as text, or "error: ...".
    std::string evalForTest(const std::string& code);

    // Recent log lines (warnings and Lua errors) of this view, oldest first.
    std::vector<std::string> log() const;
    void (*onLog)(void* user, const std::string& line) = nullptr;   // every new line (skin lock held)
    void* onLogUser = nullptr;

    // ---- used by the elements and the Lua API (skin lock held)
    static SkinView* current();                // the view whose code is running
    PanelPort& port() { return port_; }
    const PluginInfo& info() const { return info_; }
    Rml::Context* context() const { return context_; }
    void addLog(const std::string& line);

    void registerElement(KnobElement* e);
    void registerElement(EncoderElement* e);
    void registerElement(PButtonElement* e);
    void registerElement(LedElement* e);
    void registerElement(SegDigitElement* e);
    void registerElement(ActionElement* e);
    void registerElement(BendPotElement* e);
    void registerElement(BendLedElement* e);
    void unregisterElement(KnobElement* e);
    void unregisterElement(EncoderElement* e);
    void unregisterElement(PButtonElement* e);
    void unregisterElement(LedElement* e);
    void unregisterElement(SegDigitElement* e);
    void unregisterElement(ActionElement* e);
    void unregisterElement(BendPotElement* e);
    void unregisterElement(BendLedElement* e);

    void masterMoved(int raw);                 // the knob moved by the user: send it and tell Lua
    void setMaster(int raw);                   // panel.setMaster: knobs show it, send it, tell Lua
    void encoderTurned(int detents);           // the encoder turned by the user: send it and tell Lua
    void turnEncoder(int detents);             // panel.turn: encoders show it, send it, tell Lua
    void bendTurned(int pad, int step);        // a bend pot turned by the user (step -1: turned within its step)
    bool bentView() const { return bent_ == 1; }
    void button(int index, bool down);         // press / release edges (mouse, latching hold, Lua)
    bool isHeld(int index) const { return held_.count(index) != 0; }
    void setHeld(int index, bool held);
    void setMousePressed(PButtonElement* b) { mousePressed_ = b; }
    int ledLevel(int code) const { return code >= 0 && code < kLeds ? levels_[size_t(code)] : 0; }
    std::array<uint8_t, 4> displayBytes() const { return segs_; }
    double now() const { return now_; }
    float dpRatio() const { return dpRatio_; }

    // Lua callbacks (registry references), see skin_lua.cpp.
    std::map<int, std::vector<int>> onLedFns, onButtonFns;
    std::vector<int> onDisplayFns, onMasterFns, onEncoderFns;
    int envRef = -2;                           // LUA_NOREF until the environment exists

private:
    friend class ViewScope;
    void unload();
    bool poll();                               // LEDs, display, master from the instrument; true if anything changed
    void computeNextWake();
    void dispatchShow(Rml::Element* e);

    bool systemOk_ = false;
    std::string systemError_;
    PanelPort& port_;
    PluginInfo info_;
    Rml::RenderInterface* render_;
    std::unique_ptr<Rml::RenderInterface> nullRender_;
    Rml::Context* context_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    std::string contextName_, folder_;
    int widthPx_ = 1, heightPx_ = 1;
    float dpRatio_ = 1;
    double now_ = 0, lastFrameEvent_ = -1;
    bool debugger_ = false;

    std::set<KnobElement*> knobs_;
    std::set<EncoderElement*> encoders_;
    std::set<PButtonElement*> buttons_;
    std::set<LedElement*> leds_;
    std::set<SegDigitElement*> digits_;
    std::set<ActionElement*> actions_;
    std::set<BendPotElement*> pots_;
    std::set<BendLedElement*> bendLeds_;
    BendPotElement* dragPot_ = nullptr;
    double dragStartFrame_ = 0;
    int bent_ = -1;                            // the bent panel shown (class bent on the body); -1 not known yet
    std::array<int, kBendPads> bends_{};
    double lastPotTurn_ = -1, nextFlicker_ = 0;
    float flicker_ = 1;
    uint32_t flickerRng_ = 0x9E3779B9u;
    bool updateBendLeds();
    KnobElement* dragKnob_ = nullptr;
    EncoderElement* dragEncoder_ = nullptr;
    int dragStartY_ = 0, dragStartRaw_ = 0, dragDetents_ = 0;
    bool dragFine_ = false;
    int mouseX_ = 0, mouseY_ = 0;
    bool hoverClickable_ = false;
    PButtonElement* mousePressed_ = nullptr;
    std::set<int> held_;
    std::set<int> down_;                       // buttons currently down (press/release edges only)

    std::array<int, kLeds> levels_{};
    std::array<uint8_t, 4> segs_{};
    int master_ = -1;
    bool known_ = false;
    std::vector<std::string> log_;
    bool hasFrameHandler_ = false;
    double nextWake_ = 0;
    std::atomic<uint64_t> frames_{0}, changedFrames_{0}, luaCalls_{0};
};

}  // namespace skin
