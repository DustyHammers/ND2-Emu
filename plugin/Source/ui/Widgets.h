// Shared, themeable widgets for plugin windows: the window frame (header with title, tabs and a round close
// button; the body; the footer), buttons, switches, a segmented control, the footer's line, list rows and the popup
// menus' look. Nothing here knows the plugin. All sizes in dp (ui/Theme.h); the owner scales the frame as a whole.
#pragma once
#include <functional>
#include <vector>
#include "ui/Theme.h"

namespace ui {

// A list row's background: the selection colours, or the row's (hover lightens it).
void drawRow(juce::Graphics& g, juce::Rectangle<float> r, float radius, bool selected, bool hover = false);
// A text field in the theme (the footer's name box, inline renames): row colours, the selection edge, `px` text.
void styleField(juce::TextEditor& field, const Theme& theme, float px);

class Button : public juce::Component {
public:
    Button(const Theme& theme, juce::String label, bool primary = false);
    void setLabel(const juce::String& label) { label_ = label; repaint(); }
    const juce::String& label() const { return label_; }
    void setPrimary(bool primary) { primary_ = primary; repaint(); }
    bool primary() const { return primary_; }
    std::function<void()> onClick;
    // The width that shows the whole label.
    static float idealWidth(const Theme& theme, const juce::String& label);
    void paint(juce::Graphics&) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void click() { if (isEnabled() && onClick) onClick(); }
    void enablementChanged() override { repaint(); }

private:
    const Theme& theme_;
    juce::String label_;
    bool primary_, down_ = false;
};

// An on/off switch (a pill with a knob); the accent while on.
class Switch : public juce::Component {
public:
    explicit Switch(const Theme& theme) : theme_(theme) { setSize(40, 22); }
    bool isOn() const { return on_; }
    void setOn(bool on) { if (on != on_) { on_ = on; repaint(); } }
    std::function<void(bool on)> onChange;
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent& e) override { if (isEnabled() && contains(e.getPosition())) toggle(); }
    void toggle() { if (!isEnabled()) return; on_ = !on_; repaint(); if (onChange) onChange(on_); }
    void enablementChanged() override { repaint(); }

private:
    const Theme& theme_;
    bool on_ = false;
};

// One choice of several (Zoom steps, Sync modes); items can be greyed out.
class Segmented : public juce::Component {
public:
    explicit Segmented(const Theme& theme) : theme_(theme) {}
    void setItems(const juce::StringArray& items);
    const juce::StringArray& items() const { return items_; }
    void setSelected(int index) { if (index != selected_) { selected_ = index; repaint(); } }
    int selected() const { return selected_; }
    void setItemEnabled(int index, bool on);
    bool itemEnabled(int index) const { return index >= 0 && index < int(enabled_.size()) && enabled_[size_t(index)]; }
    std::function<void(int index)> onSelect;
    void choose(int index);                          // as a click on the item (nothing if greyed out)
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    float idealWidth() const;

private:
    int itemAt(float x) const;
    const Theme& theme_;
    juce::StringArray items_;
    std::vector<bool> enabled_;
    int selected_ = -1;
};

class TabBar : public juce::Component {
public:
    TabBar(const Theme& theme, const juce::StringArray& tabs);
    int selected() const { return selected_; }
    void setSelected(int index) { if (index != selected_) { selected_ = index; repaint(); } }
    std::function<void(int index)> onSelect;
    void choose(int index) { setSelected(index); if (onSelect) onSelect(index); }
    const juce::StringArray& tabs() const { return tabs_; }
    float idealWidth() const;
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

private:
    juce::Rectangle<float> tabBounds(int i) const;
    const Theme& theme_;
    juce::StringArray tabs_;
    int selected_ = 0;
};

class CloseButton : public juce::Component {
public:
    explicit CloseButton(const Theme& theme) : theme_(theme) {}
    std::function<void()> onClick;
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent& e) override { if (contains(e.getPosition()) && onClick) onClick(); }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

private:
    const Theme& theme_;
};

// The footer's line: text parts (one of them may be a text field) on the left, buttons on the right. Text alone (no
// field) wraps, each part in its own colour (muted parts grey); with a field, text that does not fit ends in "…".
class Footer : public juce::Component {
public:
    explicit Footer(const Theme& theme);
    struct Part { juce::String text; bool field = false, muted = false; };   // field: text = its starting text
    struct Action { juce::String label; std::function<void()> onClick; bool primary = false, enabled = true; };
    // Shows the line; buttons with the same labels and kinds as before are kept (their actions updated), and a field
    // that is already showing keeps what was typed in it.
    void show(std::vector<Part> parts, std::vector<Action> actions = {});
    void clear() { show({}, {}); }
    juce::TextEditor* field() { return field_.isVisible() ? &field_ : nullptr; }
    std::function<void()> onFieldReturn, onFieldEscape;
    // Tests: the line as shown (a field as [its text]), the buttons, a click on one.
    juce::String text() const;
    juce::StringArray actionLabels() const;
    bool click(const juce::String& label);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    const Theme& theme_;
    std::vector<Part> parts_;
    std::vector<Action> actions_;
    std::vector<std::unique_ptr<Button>> buttons_;
    juce::TextEditor field_;
    std::vector<juce::Rectangle<float>> partBounds_;
    juce::Rectangle<float> textArea_;                // text alone: where it wraps
};

// The window: its header (title, tabs, round close button), body and footer, filling the frame's bounds. The owner
// lays it out in dp, places and scales it as a whole (placeCentred), draws what is around it (drawBackdrop) and puts
// its page in bodyBounds().
class Frame : public juce::Component {
public:
    Frame(const Theme& theme, const juce::String& title, const juce::StringArray& tabs);
    TabBar& tabs() { return tabs_; }
    Footer& footer() { return footer_; }
    CloseButton& closeButton() { return close_; }
    juce::Rectangle<float> windowBounds() const;
    juce::Rectangle<int> bodyBounds() const;         // inside the body padding
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void()> onBodyResized;

private:
    const Theme& theme_;
    juce::String title_;
    TabBar tabs_;
    CloseButton close_;
    Footer footer_;
};

// Where a window laid out at `layout` dp goes in `area` (points; `pointsPerDp` points per dp of what it covers): drawn
// `drawnWidth` dp of the area wide (its layout scaled uniformly), centred to the point; scaled down uniformly when the
// area less `margin` dp on each side is too small, never larger.
struct Placement {
    float scale = 1;                                 // points per layout dp
    float fit = 1;                                   // 1, or less when scaled down to fit
    juce::Rectangle<float> bounds;                   // the window in the area's coordinates
};
Placement placeCentred(juce::Rectangle<int> area, float pointsPerDp, juce::Point<float> layout, float drawnWidth, float margin);

// Around a window: the area dimmed (colours::dim) and the window's soft shadow (dp::shadow*, `pointsPerDp` points per dp
// of what it covers).
void drawBackdrop(juce::Graphics& g, juce::Rectangle<float> area, juce::Rectangle<float> window, float pointsPerDp);

// The popup menus' look: rows on the row colour with the button edge, the accent behind the highlighted item (white
// text), section headers in the heading colour, arrows and ticks drawn as paths. scale: points per dp of the window
// the menu belongs to (the menu's text follows it, within limits).
class MenuLookAndFeel : public juce::LookAndFeel_V4 {
public:
    explicit MenuLookAndFeel(const Theme& theme) : theme_(theme) {}
    void setScale(float pointsPerDp) { scale_ = pointsPerDp; }
    float textSize() const;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader(juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& name) override;
    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int standardHeight, int& w, int& h) override;
    void drawPopupMenuUpDownArrow(juce::Graphics&, int width, int height, bool up) override;
    int getPopupMenuBorderSize() override { return 4; }
    bool shouldPopupMenuScaleWithTargetComponent(const juce::PopupMenu::Options&) override { return false; }

private:
    const Theme& theme_;
    float scale_ = 1.0f;
};

}  // namespace ui
