// The custom skin elements: <pbutton button>, <led code> (with an optional <lamp> child), <knob control="master">,
// <encoder>, <segdigit pos> (with <seg class="a".."g"|"dp"> children), <trigin pad> (with an optional <lamp> child).
#pragma once
#include <RmlUi/Core/Element.h>
#include <string>
#include <vector>

namespace skin {

class SkinView;

void registerElements();          // once per RmlUi initialisation

// Shows sprite <spriteprefix><NNN> (three digits) through the element's decorator.
class SpriteElement : public Rml::Element {
public:
    explicit SpriteElement(const Rml::String& tag) : Rml::Element(tag) {}
    const std::string& shownSprite() const { return shown_; }   // "image(<sprite>)" or empty
protected:
    void showSprite(int index);
    std::string prefix();
    int frames();                                                // RCSS `frames` (at least 1)
private:
    std::string shown_;
};

// MASTER LEVEL: raw 0-1023; frame = round(raw x (frames-1) / 1023). Vertical drag, mouse wheel.
class KnobElement : public SpriteElement {
public:
    explicit KnobElement(const Rml::String& tag);
    ~KnobElement() override;
    int raw() const { return raw_; }
    void move(int raw);                // moves and sends (unless unchanged)
    void show(int raw);                // shows without sending
    void wheel(float notches, bool fine);
protected:
    void OnUpdate() override;
private:
    void redraw();
    SkinView* view_;
    int raw_ = 0;
    bool known_ = false;
};

// The endless value encoder: sends detents; the sprite turns with them (RCSS `frames` for one full turn and
// `detents` per turn). Vertical drag (up = clockwise), mouse wheel.
class EncoderElement : public SpriteElement {
public:
    explicit EncoderElement(const Rml::String& tag);
    ~EncoderElement() override;
    void turn(int detents);            // sends and turns the picture
    void showTurn(int detents);        // turns the picture only
    int detentsPerTurn();
protected:
    void OnUpdate() override;
private:
    SkinView* view_;
    int position_ = 0;                 // detents, modulo one turn
};

// Left button: pressed while down (class pressed); release on mouse-up (also outside). Alt-click: latching hold.
class PButtonElement : public Rml::Element {
public:
    explicit PButtonElement(const Rml::String& tag);
    ~PButtonElement() override;
    int button() const { return button_; }
    void mouseDown(bool alt);
    void mouseRelease();
protected:
    void OnAttributeChange(const Rml::ElementAttributes& changed) override;
private:
    SkinView* view_;
    int button_ = -1;
    bool down_ = false;
};

// Class lit while the LED is on; its <lamp> children get the LED's brightness as opacity.
class LedElement : public Rml::Element {
public:
    explicit LedElement(const Rml::String& tag);
    ~LedElement() override;
    int code() const { return code_; }
    bool setLevel(int eighths);        // true if anything changed
protected:
    void OnAttributeChange(const Rml::ElementAttributes& changed) override;
private:
    SkinView* view_;
    int code_ = -1;
    int level_ = -1;
};

// Trigger input (pad 1-6): class lit while audio comes in; its <lamp> children are shown then (opacity 1, else 0).
class TrigInElement : public Rml::Element {
public:
    explicit TrigInElement(const Rml::String& tag);
    ~TrigInElement() override;
    int pad() const { return pad_; }   // 0-5, -1 if none
    bool setActive(bool on);           // true if anything changed
protected:
    void OnAttributeChange(const Rml::ElementAttributes& changed) override;
private:
    SkinView* view_;
    int pad_ = -1;
    int state_ = -1;                   // -1 unknown, 0 quiet, 1 audio
};

// One display digit (pos 0 = leftmost); its <seg> children (class a ... g, dp) get class lit.
class SegDigitElement : public Rml::Element {
public:
    explicit SegDigitElement(const Rml::String& tag);
    ~SegDigitElement() override;
    int pos() const { return pos_; }
    bool setByte(uint8_t b);           // the OS's byte (active low); true if anything changed
protected:
    void OnAttributeChange(const Rml::ElementAttributes& changed) override;
    void OnChildAdd(Rml::Element* child) override;
private:
    void apply();
    SkinView* view_;
    int pos_ = 0;
    int byte_ = 0xFF;
};

int parseInt(const std::string& s, int fallback);   // decimal or 0x hex

}  // namespace skin
