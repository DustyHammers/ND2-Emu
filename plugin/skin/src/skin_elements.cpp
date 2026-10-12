#include "skin_elements.h"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include "skin_view.h"

namespace skin {

int parseInt(const std::string& s, int fallback) {
    if (s.empty()) return fallback;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 0);       // base 0: "0x1A" or "26"
    return end && *end == 0 ? int(v) : fallback;
}

namespace {
std::string attr(Rml::Element& e, const char* name) { return e.GetAttribute<Rml::String>(name, ""); }

std::vector<std::unique_ptr<Rml::ElementInstancer>>& instancers() {
    static std::vector<std::unique_ptr<Rml::ElementInstancer>> v;
    return v;
}
template <class T> void add(const char* tag) {
    instancers().push_back(std::make_unique<Rml::ElementInstancerGeneric<T>>());
    Rml::Factory::RegisterElementInstancer(tag, instancers().back().get());
}

// Segment class names in bit order (bit 0 = a ... bit 7 = dp).
const char* const kSegClasses[8] = {"a", "b", "c", "d", "e", "f", "g", "dp"};
}  // namespace

void registerElements() {
    add<KnobElement>("knob");
    add<EncoderElement>("encoder");
    add<PButtonElement>("pbutton");
    add<LedElement>("led");
    add<SegDigitElement>("segdigit");
    add<ActionElement>("action");
}

// ------------------------------------------------------------------ sprites

std::string SpriteElement::prefix() {
    const Rml::Property* p = GetProperty("spriteprefix");
    return p ? p->Get<Rml::String>() : std::string();
}

int SpriteElement::frames() {
    const Rml::Property* p = GetProperty("frames");
    return p ? std::max(1, int(std::lround(p->Get<float>()))) : 1;
}

void SpriteElement::showSprite(int index) {
    const std::string pre = prefix();
    if (pre.empty()) return;
    char num[8];
    std::snprintf(num, sizeof num, "%03d", std::clamp(index, 0, 999));
    const std::string want = "image(" + pre + num + ")";
    if (want == shown_) return;
    shown_ = want;
    SetProperty("decorator", want);
}

// ------------------------------------------------------------------ MASTER LEVEL knob

KnobElement::KnobElement(const Rml::String& tag) : SpriteElement(tag), view_(SkinView::current()) {
    if (view_) { view_->registerElement(this); show(view_->port().master()); }
}
KnobElement::~KnobElement() { if (view_) view_->unregisterElement(this); }

void KnobElement::OnUpdate() {
    SpriteElement::OnUpdate();
    redraw();
}

void KnobElement::redraw() { showSprite(int(std::lround(double(raw_) * (frames() - 1) / double(kMasterMax)))); }

void KnobElement::show(int raw) {
    raw_ = std::clamp(raw, 0, kMasterMax);
    known_ = true;
    redraw();
}

void KnobElement::move(int raw) {
    raw = std::clamp(raw, 0, kMasterMax);
    if (known_ && raw == raw_) return;
    show(raw);
    if (view_) view_->masterMoved(raw_);
}

void KnobElement::wheel(float notches, bool fine) { move(raw_ + int(std::lround(notches * (fine ? 1.0 : 16.0)))); }

// ------------------------------------------------------------------ value encoder

EncoderElement::EncoderElement(const Rml::String& tag) : SpriteElement(tag), view_(SkinView::current()) {
    if (view_) view_->registerElement(this);
}
EncoderElement::~EncoderElement() { if (view_) view_->unregisterElement(this); }

int EncoderElement::detentsPerTurn() {
    const Rml::Property* p = GetProperty("detents");
    return p ? std::max(1, int(std::lround(p->Get<float>()))) : 24;
}

void EncoderElement::OnUpdate() {
    SpriteElement::OnUpdate();
    showTurn(0);
}

void EncoderElement::showTurn(int detents) {
    const int per = detentsPerTurn();
    position_ = ((position_ + detents) % per + per) % per;
    showSprite(int(std::lround(double(position_) * frames() / per)) % frames());
}

void EncoderElement::turn(int detents) {
    if (detents == 0) return;
    showTurn(detents);
    if (view_) view_->encoderTurned(detents);
}

// ------------------------------------------------------------------ panel button

PButtonElement::PButtonElement(const Rml::String& tag) : Rml::Element(tag), view_(SkinView::current()) {
    if (view_) view_->registerElement(this);
}
PButtonElement::~PButtonElement() {
    if (view_) {
        if (down_) view_->button(button_, false);
        view_->unregisterElement(this);
    }
}

void PButtonElement::OnAttributeChange(const Rml::ElementAttributes& changed) {
    Rml::Element::OnAttributeChange(changed);
    if (changed.count("button")) button_ = parseInt(attr(*this, "button"), -1);
}

void PButtonElement::mouseDown(bool alt) {
    if (!view_ || button_ < 0 || button_ >= kButtons) return;
    if (view_->isHeld(button_)) { view_->setHeld(button_, false); return; }   // any click on a held button releases it
    if (alt) { view_->setHeld(button_, true); return; }
    down_ = true;
    SetClass("pressed", true);
    view_->setMousePressed(this);
    view_->button(button_, true);
}

void PButtonElement::mouseRelease() {
    if (!down_) return;
    down_ = false;
    SetClass("pressed", false);
    if (view_) view_->button(button_, false);
}

// ------------------------------------------------------------------ LED

LedElement::LedElement(const Rml::String& tag) : Rml::Element(tag), view_(SkinView::current()) {
    if (view_) view_->registerElement(this);
}
LedElement::~LedElement() { if (view_) view_->unregisterElement(this); }

void LedElement::OnAttributeChange(const Rml::ElementAttributes& changed) {
    Rml::Element::OnAttributeChange(changed);
    if (changed.count("code")) { code_ = parseInt(attr(*this, "code"), -1); level_ = -1; }
}

bool LedElement::setLevel(int eighths) {
    eighths = std::clamp(eighths, 0, 8);
    if (eighths == level_) return false;
    level_ = eighths;
    SetClass("lit", eighths > 0);
    // Perceived brightness: the square root of the duty (a half-duty LED looks clearly more than half lit).
    const float opacity = std::sqrt(float(eighths) / 8.0f);
    for (int i = 0; i < GetNumChildren(); ++i)
        if (Rml::Element* c = GetChild(i); c->GetTagName() == "lamp") c->SetProperty("opacity", std::to_string(opacity));
    return true;
}

// ------------------------------------------------------------------ display digit

SegDigitElement::SegDigitElement(const Rml::String& tag) : Rml::Element(tag), view_(SkinView::current()) {
    if (view_) view_->registerElement(this);
}
SegDigitElement::~SegDigitElement() { if (view_) view_->unregisterElement(this); }

void SegDigitElement::OnAttributeChange(const Rml::ElementAttributes& changed) {
    Rml::Element::OnAttributeChange(changed);
    if (changed.count("pos")) {
        pos_ = std::clamp(parseInt(attr(*this, "pos"), 0), 0, 3);
        if (view_) setByte(view_->displayBytes()[size_t(pos_)]);
    }
}

void SegDigitElement::OnChildAdd(Rml::Element* child) {
    Rml::Element::OnChildAdd(child);
    apply();
}

bool SegDigitElement::setByte(uint8_t b) {
    if (b == byte_) return false;
    byte_ = b;
    apply();
    return true;
}

void SegDigitElement::apply() {
    for (int i = 0; i < GetNumChildren(); ++i) {
        Rml::Element* c = GetChild(i);
        if (c->GetTagName() != "seg") continue;
        for (int bit = 0; bit < 8; ++bit)
            if (c->IsClassSet(kSegClasses[bit])) c->SetClass("lit", ((byte_ >> bit) & 1) == 0);
    }
}

// ------------------------------------------------------------------ plugin action

ActionElement::ActionElement(const Rml::String& tag) : Rml::Element(tag), view_(SkinView::current()) {
    if (view_) view_->registerElement(this);
}
ActionElement::~ActionElement() { if (view_) view_->unregisterElement(this); }

void ActionElement::OnAttributeChange(const Rml::ElementAttributes& changed) {
    Rml::Element::OnAttributeChange(changed);
    if (changed.count("name")) { name_ = attr(*this, "name"); lit_ = -1; }
}

bool ActionElement::setLit(bool lit) {
    if (int(lit) == lit_) return false;
    lit_ = int(lit);
    SetClass("lit", lit);
    return true;
}

}  // namespace skin
