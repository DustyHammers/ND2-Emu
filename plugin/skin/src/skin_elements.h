// The custom skin elements: <pbutton button>, <led code> (with an optional <lamp> child), <knob control="master">,
// <encoder>, <segdigit pos> (with <seg class="a".."g"|"dp"> children), <action name> (a plugin action, e.g. "banks"),
// and for the circuit-bent mode <bendpot pad="1".."6"> and <bendled> (with <lamp> children).
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

// A plugin action (name="banks": the Banks window): a left click asks for it; class lit while its window is open.
class ActionElement : public Rml::Element {
public:
    explicit ActionElement(const Rml::String& tag);
    ~ActionElement() override;
    const std::string& name() const { return name_; }
    bool setLit(bool lit);             // true if it changed
protected:
    void OnAttributeChange(const Rml::ElementAttributes& changed) override;
private:
    SkinView* view_;
    std::string name_;
    int lit_ = -1;
};

// Circuit-bent mode: a bend pot <bendpot pad="1".."6"> (a sprite like the knob: RCSS frames, spriteprefix). Step 0
// (Off) ... kBendSteps - 1 sit at frames round(step x (frames - 1) / (kBendSteps - 1)). A drag turns it smoothly and
// selects the nearest step as it goes; on release it settles on that step. Vertical drag, mouse wheel (a step a notch).
class BendPotElement : public SpriteElement {
public:
    explicit BendPotElement(const Rml::String& tag);
    ~BendPotElement() override;
    int pad() const { return pad_; }                    // 0-5, -1 without a valid pad attribute
    int step() const { return step_; }
    double frame() const { return frame_; }
    void show(int step);                                // the instrument's value (unless being dragged)
    void beginDrag();
    void dragTo(double frame);                          // turns; sends the step when it changes
    void endDrag();
    void wheel(int steps);
    double stepFrame(int step);
protected:
    void OnUpdate() override;
    void OnAttributeChange(const Rml::ElementAttributes& changed) override;
private:
    SkinView* view_;
    int pad_ = -1, step_ = 0;
    double frame_ = 0;
    bool dragging_ = false;
};

// Circuit-bent mode: the indicator LED. Its <lamp> children are lit while the bent panel is shown and flicker while a
// bend pot turns (the runtime sets their opacity).
class BendLedElement : public Rml::Element {
public:
    explicit BendLedElement(const Rml::String& tag);
    ~BendLedElement() override;
    bool setOpacity(float opacity);                     // true if it changed
private:
    SkinView* view_;
    float opacity_ = -1;
};

int parseInt(const std::string& s, int fallback);   // decimal or 0x hex

}  // namespace skin
