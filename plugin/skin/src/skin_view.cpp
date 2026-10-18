#include "skin_view.h"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include "skin_elements.h"
#include "skin_lua.h"
#include "skin_system.h"
#include <RmlUi/Lua/IncludeLua.h>

namespace fs = std::filesystem;

namespace skin {
namespace {

constexpr double kRawPerDp = double(kMasterMax) / 256.0;   // knob drag: full range over 256 dp
constexpr double kFineFactor = 0.1;                        // with Shift
constexpr double kDpPerDetent = 12.0, kFineDpPerDetent = 36.0;   // encoder drag
constexpr double kPotDpPerTurn = 256.0;                    // bend pot drag: its whole travel over 256 dp, like the knob
constexpr double kFlickerSeconds = 0.15, kFlickerHz = 30;  // the bend LED flickers while a pot turns (and just after)
constexpr size_t kLogLines = 300;

// Draws nothing (tests, and a view whose renderer is not ready).
class NullRender : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 1; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dims, const Rml::String&) override { dims = {1, 1}; return 1; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 1; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};

template <class T> T* findUp(Rml::Element* e) {
    for (; e; e = e->GetParentNode())
        if (auto* t = dynamic_cast<T*>(e)) return t;
    return nullptr;
}

template <class F> void guarded(SkinView* v, F&& f) {
    try {
        f();
    } catch (const std::exception& e) {
        v->addLog(std::string("error: ") + e.what());
    } catch (...) {
        v->addLog("error: unexpected failure in the skin");
    }
}

}  // namespace

SkinView::SkinView(PanelPort& port, PluginInfo info, Rml::RenderInterface* render)
    : port_(port), info_(std::move(info)), render_(render) {
    if (!render_) { nullRender_ = std::make_unique<NullRender>(); render_ = nullRender_.get(); }
    char name[64];
    std::snprintf(name, sizeof name, "nd2-%p", static_cast<void*>(this));
    contextName_ = name;
    guarded(this, [&] {
        ViewScope s(this);
        systemOk_ = acquireSystem(systemError_);
        if (!systemOk_) return;
        context_ = Rml::CreateContext(contextName_, {widthPx_, heightPx_}, render_);
        if (!context_) systemError_ = "The skin view could not be created.";
    });
}

SkinView::~SkinView() {
    guarded(this, [&] {
        ViewScope s(this);
        unload();
        if (context_) {
            contextClosing(context_);
            Rml::RemoveContext(contextName_);
            context_ = nullptr;
            Rml::ReleaseRenderManagers();
        }
        if (systemOk_) releaseSystem();
    });
}

std::vector<std::string> SkinView::log() const {
    std::lock_guard<std::recursive_mutex> lk(skinLock());
    return log_;
}

void SkinView::addLog(const std::string& line) {
    std::lock_guard<std::recursive_mutex> lk(skinLock());
    log_.push_back(line);
    if (log_.size() > kLogLines) log_.erase(log_.begin(), log_.begin() + long(log_.size() - kLogLines));
    if (onLog) onLog(onLogUser, line);
}

// ------------------------------------------------------------------ loading

bool SkinView::load(const std::string& folder, const std::string& name, std::string& error) {
    bool ok = false;
    guarded(this, [&] {
        ViewScope s(this);
        unload();
        if (!systemOk_ || !context_) { error = systemError_; return; }
        const fs::path dir = fs::u8path(folder).lexically_normal();
        const fs::path file = dir / fs::u8path(name + ".rml");
        std::error_code ec;
        if (!fs::is_regular_file(file, ec)) { error = "The file " + name + ".rml is missing from " + dir.u8string() + "."; return; }
        folder_ = dir.u8string();
        allowFolder(folder_);
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            std::string ext = entry.path().extension().u8string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return char(std::tolower(uint8_t(c))); });
            if (ext == ".ttf" || ext == ".otf") loadFont(entry.path().u8string());
        }
        resetEnvironment(*this);
        installLuaApi(*this);
        down_.clear();
        known_ = false;
        for (int c = 0; c < kLeds; ++c) levels_[size_t(c)] = port_.ledLevel(c);
        segs_ = port_.segments();
        master_ = port_.master();
        size_t logBefore = log_.size();
        document_ = context_->LoadDocument(file.u8string());
        if (document_) {
            std::string bad;                             // a document RmlUi could only partly read is a failed load
            if (document_->GetNumChildren() == 0) bad = name + ".rml has no content";
            for (size_t i = logBefore; i < log_.size() && bad.empty(); ++i)
                if (log_[i].find("parse error") != std::string::npos) bad = log_[i].substr(log_[i].find(": ") + 2);
            if (!bad.empty()) {
                addLog("error: " + bad);
                document_->Close();
                document_ = nullptr;
                logBefore = log_.size() - 1;
            }
        }
        if (!document_) {
            for (size_t i = logBefore; i < log_.size() && i < logBefore + 3; ++i) {
                std::string l = log_[i];
                for (const char* p : {"error: ", "warning: "}) if (l.rfind(p, 0) == 0) l = l.substr(std::strlen(p));
                error += (error.empty() ? "" : "; ") + l;
            }
            if (error.empty()) error = name + ".rml could not be read";
            unload();
            return;
        }
        document_->Show();
        dispatchShow(document_);
        nextWake_ = 0;                                   // draw the new document at once
        ok = true;
    });
    if (!ok && error.empty()) error = name + ".rml could not be read";
    return ok;
}

void SkinView::dispatchShow(Rml::Element* e) {
    for (int i = 0; i < e->GetNumChildren(); ++i) {
        Rml::Element* c = e->GetChild(i);
        if (c->HasAttribute("onshow")) c->DispatchEvent(Rml::EventId::Show, Rml::Dictionary());
        dispatchShow(c);
    }
}

void SkinView::unload() {
    if (dragPot_) { BendPotElement* p = dragPot_; dragPot_ = nullptr; p->endDrag(); }
    bent_ = -1;
    dragKnob_ = nullptr;
    dragEncoder_ = nullptr;
    if (mousePressed_) { mousePressed_->mouseRelease(); mousePressed_ = nullptr; }
    for (int b : std::set<int>(held_)) setHeld(b, false);
    if (document_) {
        document_->Close();
        document_ = nullptr;
        if (context_) context_->Update();                 // closing is completed in the next update
    }
    dropLuaCallbacks(*this);
    dropEnvironment(*this);
}

void SkinView::setViewport(int widthPx, int heightPx, float dpRatio) {
    widthPx_ = std::max(1, widthPx); heightPx_ = std::max(1, heightPx); dpRatio_ = dpRatio > 0 ? dpRatio : 1;
    guarded(this, [&] {
        ViewScope s(this);
        if (!context_) return;
        context_->SetDimensions({widthPx_, heightPx_});
        context_->SetDensityIndependentPixelRatio(dpRatio_);
    });
}

// ------------------------------------------------------------------ per frame

void SkinView::update(double now) {
    guarded(this, [&] {
        ViewScope s(this);
        now_ = now;
        ++frames_;
        bool changed = false;
        const uint64_t lua0 = luaCalls_.load();
        if (!context_) return;
        if (document_) {
            changed = poll();
            // Frame events only for a handler with code in it: an absent, empty or blank onframe asks for no frames.
            const std::string onframe = document_->GetAttribute<Rml::String>("onframe", "");
            hasFrameHandler_ = onframe.find_first_not_of(" \t\r\n") != std::string::npos;
            if (hasFrameHandler_ && now_ - lastFrameEvent_ >= 1.0 / kFrameEventHz - 0.001) {
                lastFrameEvent_ = now_;
                if (document_) document_->DispatchEvent("frame", Rml::Dictionary());
            }
        }
        context_->Update();
        if (luaCalls_.load() != lua0) changed = true;
        if (changed) ++changedFrames_;
        computeNextWake();
    });
}

SkinView::Stats SkinView::stats() const { return {frames_.load(), changedFrames_.load(), luaCalls_.load()}; }

// The bend LED: lit; flickering while a pot turns (and kFlickerSeconds after its last move).
bool SkinView::updateBendLeds() {
    const bool flickering = lastPotTurn_ >= 0 && now_ - lastPotTurn_ < kFlickerSeconds;
    if (!flickering) flicker_ = 1;
    else if (now_ >= nextFlicker_) {
        flickerRng_ ^= flickerRng_ << 13; flickerRng_ ^= flickerRng_ >> 17; flickerRng_ ^= flickerRng_ << 5;
        flicker_ = 0.35f + 0.65f * float(flickerRng_ >> 8) / 16777216.0f;
        nextFlicker_ = now_ + 1.0 / kFlickerHz;
    }
    bool changed = false;
    for (BendLedElement* l : bendLeds_) if (l->setOpacity(flicker_)) changed = true;
    return changed;
}

void SkinView::computeNextWake() {
    double wake = std::numeric_limits<double>::infinity();
    if (lastPotTurn_ >= 0 && now_ - lastPotTurn_ < kFlickerSeconds + 0.05) wake = std::min(nextFlicker_, lastPotTurn_ + kFlickerSeconds + 0.01);
    if (document_ && hasFrameHandler_) wake = lastFrameEvent_ + 1.0 / kFrameEventHz;
    if (context_) {
        const double d = context_->GetNextUpdateDelay();         // animations, transitions (0: next frame)
        if (d >= 0 && d < 1e9) wake = std::min(wake, now_ + d);
    }
    nextWake_ = wake;
}

void SkinView::render() {
    guarded(this, [&] {
        ViewScope s(this);
        if (context_) context_->Render();
    });
}

// The instrument's panel as it is now: LED levels, the display, the master knob (moved by a project load).
bool SkinView::poll() {
    bool changed = false;
    for (int c = 0; c < kLeds; ++c) {
        const int lv = port_.ledLevel(c);
        const bool edge = (lv > 0) != (levels_[size_t(c)] > 0) || !known_;
        if (lv != levels_[size_t(c)] || !known_) {
            levels_[size_t(c)] = lv;
            changed = true;
            if (edge) if (auto it = onLedFns.find(c); it != onLedFns.end()) callLua(it->second, {lv});
        }
    }
    for (LedElement* e : leds_) if (e->setLevel(ledLevel(e->code()))) changed = true;
    const std::array<uint8_t, 4> s = port_.segments();
    if (s != segs_ || !known_) {
        segs_ = s;
        changed = true;
        callLua(onDisplayFns, {segs_[0], segs_[1], segs_[2], segs_[3]});
    }
    for (SegDigitElement* d : digits_) if (d->setByte(segs_[size_t(d->pos())])) changed = true;
    for (ActionElement* a : actions_) if (a->setLit(port_.actionLit(a->name()))) changed = true;
    const int bv = port_.bentView() ? 1 : 0;
    if (bv != bent_) {
        bent_ = bv;
        if (document_) document_->SetClass("bent", bv == 1);
        changed = true;
    }
    for (int p = 0; p < kBendPots; ++p) {
        const int b = port_.bend(p);
        if (b != bends_[size_t(p)] || !known_) {
            bends_[size_t(p)] = b;
            changed = true;
        }
    }
    for (BendPotElement* pot : pots_) if (pot != dragPot_ && pot->pot() >= 0) pot->show(bends_[size_t(pot->pot())]);
    if (updateBendLeds()) changed = true;
    const int m = port_.master();
    if (m != master_ && !dragKnob_) {
        master_ = m;
        for (KnobElement* k : knobs_) k->show(m);
        changed = true;
    }
    known_ = true;
    return changed;
}

// ------------------------------------------------------------------ instrument actions

void SkinView::masterMoved(int raw) {
    if (raw == master_) return;
    master_ = raw;
    port_.setMaster(raw);
    callLua(onMasterFns, {raw});
}

void SkinView::setMaster(int raw) {
    raw = std::clamp(raw, 0, kMasterMax);
    for (KnobElement* k : knobs_) k->show(raw);
    masterMoved(raw);
}

void SkinView::encoderTurned(int detents) {
    if (detents == 0) return;
    port_.turnEncoder(detents);
    callLua(onEncoderFns, {detents});
}

void SkinView::turnEncoder(int detents) {
    for (EncoderElement* e : encoders_) e->showTurn(detents);
    encoderTurned(detents);
}

void SkinView::bendTurned(int pot, int value) {
    lastPotTurn_ = now_;
    if (pot < 0 || pot >= kBendPots || value < 0) return;
    bends_[size_t(pot)] = value;
    port_.setBend(pot, value);
}

void SkinView::button(int index, bool down) {
    if (index < 0 || index >= kButtons) return;
    if ((down_.count(index) != 0) == down) return;  // only press and release edges
    if (down) down_.insert(index); else down_.erase(index);
    port_.sendButton(index, down);
    auto it = onButtonFns.find(index);
    if (it != onButtonFns.end()) callLuaBool(it->second, down);
}

void SkinView::setHeld(int index, bool held) {
    if (held == isHeld(index)) return;
    if (held) held_.insert(index); else held_.erase(index);
    for (PButtonElement* b : buttons_)
        if (b->button() == index) b->SetClass("held", held);
    button(index, held);
}

// ------------------------------------------------------------------ input

void SkinView::mouseMove(int x, int y, int mods) {
    guarded(this, [&] {
        ViewScope s(this);
        mouseX_ = x; mouseY_ = y;
        const bool fine = (mods & ModShift) != 0;
        if (dragKnob_) {
            if (fine != dragFine_) { dragFine_ = fine; dragStartY_ = y; dragStartRaw_ = dragKnob_->raw(); }   // no jump
            const double perPx = kRawPerDp * (fine ? kFineFactor : 1.0) / double(dpRatio_);
            dragKnob_->move(dragStartRaw_ + int(std::lround(double(dragStartY_ - y) * perPx)));
        }
        if (dragPot_) {
            if (fine != dragFine_) { dragFine_ = fine; dragStartY_ = y; dragStartFrame_ = dragPot_->frame(); }
            const double framesPerPx = (dragPot_->travel() / kPotDpPerTurn) * (fine ? kFineFactor : 1.0) / double(dpRatio_);
            dragPot_->dragTo(dragStartFrame_ + double(dragStartY_ - y) * framesPerPx);
        }
        if (dragEncoder_) {
            if (fine != dragFine_) { dragFine_ = fine; dragStartY_ = y; dragDetents_ = 0; }
            const double perDetent = (fine ? kFineDpPerDetent : kDpPerDetent) * double(dpRatio_);
            const int total = int(std::floor(double(dragStartY_ - y) / perDetent));
            if (total != dragDetents_) { dragEncoder_->turn(total - dragDetents_); dragDetents_ = total; }
        }
        if (context_) context_->ProcessMouseMove(x, y, mods);
        Rml::Element* hover = context_ ? context_->GetHoverElement() : nullptr;
        hoverClickable_ = findUp<SegDigitElement>(hover) != nullptr || findUp<ActionElement>(hover) != nullptr;
    });
}

void SkinView::mouseDown(int button, int mods) {
    guarded(this, [&] {
        ViewScope s(this);
        if (!context_) return;
        if (button == 0 && document_ && (mods & ModMeta)) {   // Cmd-click: the bent panel shows / hides
            port_.setBentView(!port_.bentView());
            poll();
            return;
        }
        if (button == 0 && document_) {
            Rml::Element* hover = context_->GetHoverElement();
            dragFine_ = (mods & ModShift) != 0;
            dragStartY_ = mouseY_;
            if (BendPotElement* p = findUp<BendPotElement>(hover); p && p->pot() >= 0) {
                dragPot_ = p;
                dragStartFrame_ = p->frame();
                p->beginDrag();
            } else if (KnobElement* k = findUp<KnobElement>(hover)) {
                dragKnob_ = k;
                dragStartRaw_ = k->raw();
            } else if (EncoderElement* e = findUp<EncoderElement>(hover)) {
                dragEncoder_ = e;
                dragDetents_ = 0;
            } else if (PButtonElement* b = findUp<PButtonElement>(hover)) {
                b->mouseDown((mods & ModAlt) != 0);
            } else if (ActionElement* a = findUp<ActionElement>(hover)) {
                if (!a->name().empty()) port_.action(a->name());
            } else if (findUp<SegDigitElement>(hover)) {
                port_.action("program_entry");             // the display: type a program (bank and number)
            }
        }
        context_->ProcessMouseButtonDown(button, mods);
    });
}

void SkinView::mouseUp(int button, int mods) {
    guarded(this, [&] {
        ViewScope s(this);
        if (context_) context_->ProcessMouseButtonUp(button, mods);
        if (button == 0) {
            if (dragPot_) { BendPotElement* p = dragPot_; dragPot_ = nullptr; p->endDrag(); }
            dragKnob_ = nullptr;
            dragEncoder_ = nullptr;
            if (mousePressed_) { PButtonElement* b = mousePressed_; mousePressed_ = nullptr; b->mouseRelease(); }
        }
    });
}

void SkinView::mouseWheel(float deltaY, int mods) {
    guarded(this, [&] {
        ViewScope s(this);
        if (!context_) return;
        if (document_) {
            Rml::Element* hover = context_->GetHoverElement();
            if (BendPotElement* p = findUp<BendPotElement>(hover); p && p->pot() >= 0) {
                p->wheel(deltaY < 0 ? 1 : -1);
                return;
            }
            if (KnobElement* k = findUp<KnobElement>(hover)) { k->wheel(-deltaY, (mods & ModShift) != 0); return; }
            if (EncoderElement* e = findUp<EncoderElement>(hover)) {
                const int d = deltaY < 0 ? int(std::ceil(-deltaY)) : -int(std::ceil(deltaY));
                e->turn(d);
                return;
            }
        }
        context_->ProcessMouseWheel(deltaY, mods);
    });
}

void SkinView::mouseLeave() {
    guarded(this, [&] {
        ViewScope s(this);
        if (context_) context_->ProcessMouseLeave();
    });
}

void SkinView::releaseHeldButtons() {
    guarded(this, [&] {
        ViewScope s(this);
        for (int b : std::set<int>(held_)) setHeld(b, false);
    });
}

void SkinView::keyDown(int rmlKey, int mods) {
    guarded(this, [&] {
        ViewScope s(this);
        if (context_) context_->ProcessKeyDown(Rml::Input::KeyIdentifier(rmlKey), mods);
    });
}

void SkinView::keyUp(int rmlKey, int mods) {
    guarded(this, [&] {
        ViewScope s(this);
        if (context_) context_->ProcessKeyUp(Rml::Input::KeyIdentifier(rmlKey), mods);
    });
}

void SkinView::setDebuggerVisible(bool on) {
    guarded(this, [&] {
        ViewScope s(this);
        if (!context_) return;
        showDebugger(context_, on);
        debugger_ = on;
    });
}

std::string SkinView::evalForTest(const std::string& code) {
    std::string out;
    guarded(this, [&] {
        ViewScope s(this);
        lua_State* L = luaState();
        if (!L) { out = "error: no Lua"; return; }
        const int base = lua_gettop(L);
        if (luaL_loadstring(L, ("return " + code).c_str()) != LUA_OK) {
            lua_settop(L, base);
            if (luaL_loadstring(L, code.c_str()) != LUA_OK) { out = std::string("error: ") + lua_tostring(L, -1); lua_settop(L, base); return; }
        }
        if (lua_pcall(L, 0, 1, 0) != LUA_OK) out = std::string("error: ") + lua_tostring(L, -1);
        else out = luaL_tolstring(L, -1, nullptr);
        lua_settop(L, base);
    });
    return out;
}

// ------------------------------------------------------------------ element registry

void SkinView::registerElement(KnobElement* e) { knobs_.insert(e); }
void SkinView::registerElement(EncoderElement* e) { encoders_.insert(e); }
void SkinView::registerElement(PButtonElement* e) { buttons_.insert(e); }
void SkinView::registerElement(LedElement* e) { leds_.insert(e); }
void SkinView::registerElement(SegDigitElement* e) { digits_.insert(e); }
void SkinView::unregisterElement(KnobElement* e) { knobs_.erase(e); if (dragKnob_ == e) dragKnob_ = nullptr; }
void SkinView::unregisterElement(EncoderElement* e) { encoders_.erase(e); if (dragEncoder_ == e) dragEncoder_ = nullptr; }
void SkinView::unregisterElement(PButtonElement* e) { buttons_.erase(e); if (mousePressed_ == e) mousePressed_ = nullptr; }
void SkinView::unregisterElement(LedElement* e) { leds_.erase(e); }
void SkinView::unregisterElement(SegDigitElement* e) { digits_.erase(e); }
void SkinView::registerElement(ActionElement* e) { actions_.insert(e); }
void SkinView::unregisterElement(ActionElement* e) { actions_.erase(e); }
void SkinView::registerElement(BendPotElement* e) { pots_.insert(e); }
void SkinView::unregisterElement(BendPotElement* e) { pots_.erase(e); if (dragPot_ == e) dragPot_ = nullptr; }
void SkinView::registerElement(BendLedElement* e) { bendLeds_.insert(e); }
void SkinView::unregisterElement(BendLedElement* e) { bendLeds_.erase(e); }

}  // namespace skin
