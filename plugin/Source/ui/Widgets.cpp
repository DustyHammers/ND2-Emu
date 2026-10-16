#include "ui/Widgets.h"

namespace ui {

void drawRow(juce::Graphics& g, juce::Rectangle<float> r, float radius, bool selected, bool hover) {
    g.setColour(selected ? colours::selection : hover ? colours::row.brighter(0.08f) : colours::row);
    g.fillRoundedRectangle(r, radius);
    g.setColour(selected ? colours::selectionEdge : colours::rowEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
}

void styleField(juce::TextEditor& field, const Theme& theme, float px) {
    field.setFont(theme.font(px));
    field.applyFontToAllText(theme.font(px));
    field.setColour(juce::TextEditor::backgroundColourId, colours::row);
    field.setColour(juce::TextEditor::outlineColourId, colours::selectionEdge);
    field.setColour(juce::TextEditor::focusedOutlineColourId, colours::selectionEdge);
    field.setColour(juce::TextEditor::textColourId, colours::text);
    field.setColour(juce::TextEditor::highlightColourId, colours::selection.brighter(0.2f));
    field.setColour(juce::TextEditor::highlightedTextColourId, colours::text);
    field.setColour(juce::CaretComponent::caretColourId, colours::text);
    field.setIndents(8, 0);
    field.setJustification(juce::Justification::centredLeft);
}

// ------------------------------------------------------------------ Button

Button::Button(const Theme& theme, juce::String label, bool primary) : theme_(theme), label_(std::move(label)), primary_(primary) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

float Button::idealWidth(const Theme& theme, const juce::String& label) {
    return std::ceil(textWidth(theme.font(dp::buttonText), label)) + 2 * 16;
}

void Button::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    const bool on = isEnabled(), hover = on && isMouseOver(true);
    juce::Colour fill = primary_ ? theme_.accent : colours::button, edge = primary_ ? theme_.accentEdge : colours::buttonEdge;
    if (hover) fill = fill.brighter(0.12f);
    if (down_ && on) fill = fill.darker(0.15f);
    g.setOpacity(1.0f);
    g.setColour(on ? fill : fill.withMultipliedAlpha(0.45f));
    g.fillRoundedRectangle(r, dp::buttonRadius);
    g.setColour(on ? edge : edge.withMultipliedAlpha(0.45f));
    g.drawRoundedRectangle(r, dp::buttonRadius, 1.0f);
    drawText(g, label_, theme_.font(dp::buttonText), r.reduced(10, 0), on ? (primary_ ? juce::Colours::white : colours::text) : colours::muted,
             juce::Justification::centred);
}

void Button::mouseDown(const juce::MouseEvent&) { down_ = true; repaint(); }
void Button::mouseUp(const juce::MouseEvent& e) {
    down_ = false;
    repaint();
    if (contains(e.getPosition())) click();
}

// ------------------------------------------------------------------ Switch

void Switch::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat().reduced(1);
    const float h = r.getHeight();
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    g.setColour((on_ ? theme_.accent : colours::button).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(r, h / 2);
    g.setColour((on_ ? theme_.accentEdge : colours::buttonEdge).withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(r, h / 2, 1.0f);
    const float d = h - 6, x = on_ ? r.getRight() - 3 - d : r.getX() + 3;
    g.setColour((on_ ? juce::Colours::white : colours::muted).withMultipliedAlpha(alpha));
    g.fillEllipse(x, r.getY() + 3, d, d);
}

// ------------------------------------------------------------------ Segmented

void Segmented::setItems(const juce::StringArray& items) {
    items_ = items;
    enabled_.assign(size_t(items.size()), true);
    repaint();
}

void Segmented::setItemEnabled(int index, bool on) {
    if (index < 0 || index >= int(enabled_.size()) || enabled_[size_t(index)] == on) return;
    enabled_[size_t(index)] = on;
    repaint();
}

float Segmented::idealWidth() const {
    float w = 0;
    for (const auto& s : items_) w = std::max(w, textWidth(theme_.font(dp::buttonText), s) + 24);
    return w * float(items_.size());
}

int Segmented::itemAt(float x) const {
    if (items_.isEmpty()) return -1;
    const int i = int(x / (float(getWidth()) / float(items_.size())));
    return juce::jlimit(0, items_.size() - 1, i);
}

void Segmented::choose(int index) {
    if (!itemEnabled(index) || !isEnabled()) return;
    setSelected(index);
    if (onSelect) onSelect(index);
}

void Segmented::mouseUp(const juce::MouseEvent& e) { if (contains(e.getPosition())) choose(itemAt(e.position.x)); }

void Segmented::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(colours::button);
    g.fillRoundedRectangle(r, dp::buttonRadius);
    g.setColour(colours::buttonEdge);
    g.drawRoundedRectangle(r, dp::buttonRadius, 1.0f);
    const int n = items_.size();
    if (n == 0) return;
    const float w = r.getWidth() / float(n);
    const int hover = isMouseOver() ? itemAt(float(getMouseXYRelative().x)) : -1;
    for (int i = 0; i < n; ++i) {
        const auto cell = juce::Rectangle<float>(r.getX() + w * float(i), r.getY(), w, r.getHeight());
        const bool on = enabled_[size_t(i)] && isEnabled();
        if (i == selected_) {
            g.setColour(theme_.accent);
            g.fillRoundedRectangle(cell.reduced(2), dp::buttonRadius - 2);
            g.setColour(theme_.accentEdge);
            g.drawRoundedRectangle(cell.reduced(2), dp::buttonRadius - 2, 1.0f);
        } else if (i == hover && on) {
            g.setColour(colours::button.brighter(0.12f));
            g.fillRoundedRectangle(cell.reduced(2), dp::buttonRadius - 2);
        }
        if (i > 0 && i != selected_ && i - 1 != selected_) {
            g.setColour(colours::buttonEdge);
            g.drawVerticalLine(int(cell.getX()), cell.getY() + 8, cell.getBottom() - 8);
        }
        drawText(g, items_[i], theme_.font(dp::buttonText), cell.reduced(4, 0),
                 i == selected_ ? juce::Colours::white : on ? colours::text : colours::muted.withMultipliedAlpha(0.6f), juce::Justification::centred);
    }
}

// ------------------------------------------------------------------ TabBar

TabBar::TabBar(const Theme& theme, const juce::StringArray& tabs) : theme_(theme), tabs_(tabs) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> TabBar::tabBounds(int i) const {
    float x = 0;
    const juce::Font f = theme_.font(dp::tabText);
    for (int k = 0; k < tabs_.size(); ++k) {
        const float w = textWidth(f, tabs_[k]) + 2 * 14;
        if (k == i) return {x, 0, w, float(getHeight())};
        x += w + 6;
    }
    return {};
}

float TabBar::idealWidth() const { return tabs_.isEmpty() ? 0 : tabBounds(tabs_.size() - 1).getRight(); }

void TabBar::mouseUp(const juce::MouseEvent& e) {
    for (int i = 0; i < tabs_.size(); ++i)
        if (tabBounds(i).contains(e.position)) { choose(i); return; }
}

void TabBar::paint(juce::Graphics& g) {
    const auto mouse = getMouseXYRelative().toFloat();
    for (int i = 0; i < tabs_.size(); ++i) {
        const auto r = tabBounds(i).reduced(0.5f);
        if (i == selected_) {
            g.setColour(theme_.accent);
            g.fillRoundedRectangle(r, dp::tabRadius);
            g.setColour(theme_.accentEdge);
            g.drawRoundedRectangle(r, dp::tabRadius, 1.0f);
        } else if (isMouseOver() && r.contains(mouse)) {
            g.setColour(colours::button);
            g.fillRoundedRectangle(r, dp::tabRadius);
        }
        drawText(g, tabs_[i], theme_.font(dp::tabText), r, i == selected_ ? juce::Colours::white : colours::muted, juce::Justification::centred);
    }
}

// ------------------------------------------------------------------ CloseButton

void CloseButton::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(isMouseOver() ? colours::button.brighter(0.12f) : colours::button);
    g.fillEllipse(r);
    g.setColour(colours::buttonEdge);
    g.drawEllipse(r, 1.0f);
    drawCross(g, r.withSizeKeepingCentre(r.getWidth() * 0.32f, r.getHeight() * 0.32f), colours::text, 1.6f);
}

// ------------------------------------------------------------------ Footer

Footer::Footer(const Theme& theme) : theme_(theme) {
    addChildComponent(field_);
    styleField(field_, theme_, dp::buttonText);
    field_.onReturnKey = [this] { if (onFieldReturn) onFieldReturn(); };
    field_.onEscapeKey = [this] { if (onFieldEscape) onFieldEscape(); };
}

void Footer::show(std::vector<Part> parts, std::vector<Action> actions) {
    parts_ = std::move(parts);
    actions_ = std::move(actions);
    buttons_.clear();
    for (const Action& a : actions_) {
        auto b = std::make_unique<Button>(theme_, a.label, a.primary);
        b->setEnabled(a.enabled);
        b->onClick = a.onClick;
        addAndMakeVisible(*b);
        buttons_.push_back(std::move(b));
    }
    bool hasField = false;
    for (const Part& p : parts_)
        if (p.field) { hasField = true; field_.setText(p.text, juce::dontSendNotification); }
    styleField(field_, theme_, dp::buttonText);
    field_.setVisible(hasField);
    resized();
    repaint();
}

juce::String Footer::text() const {
    juce::String s;
    for (const Part& p : parts_) s << (p.field ? "[" + field_.getText() + "]" : p.text);
    return s;
}

juce::StringArray Footer::actionLabels() const {
    juce::StringArray a;
    for (const Action& x : actions_) a.add(x.label);
    return a;
}

bool Footer::click(const juce::String& label) {
    for (auto& b : buttons_)
        if (b->label() == label && b->isEnabled()) { b->click(); return true; }
    return false;
}

void Footer::resized() {
    auto r = getLocalBounds().toFloat().reduced(dp::padX, 0);
    const float bh = dp::button, by = (float(getHeight()) - bh) / 2;
    for (size_t i = buttons_.size(); i-- > 0;) {
        const float w = Button::idealWidth(theme_, buttons_[i]->label());
        buttons_[i]->setBounds(juce::Rectangle<float>(r.getRight() - w, by, w, bh).toNearestInt());
        r.removeFromRight(w + 10);
    }
    textArea_ = r.withTrimmedTop(6).withTrimmedBottom(6);
    // The parts in order; a field takes 260 dp (less if it must); the last text part before the room runs out ends in "…".
    partBounds_.clear();
    const juce::Font f = theme_.font(dp::buttonText);
    float x = r.getX();
    for (const Part& p : parts_) {
        const float want = p.field ? 260.0f : textWidth(f, p.text);
        const float w = std::max(0.0f, std::min(want, r.getRight() - x));
        partBounds_.push_back({x, by, w, bh});
        if (p.field) field_.setBounds(juce::Rectangle<float>(x, by, w, bh).toNearestInt());
        x += w + (p.field ? 6 : 0);
    }
}

void Footer::paint(juce::Graphics& g) {
    const juce::Font f = theme_.font(dp::buttonText);
    if (!field_.isVisible() && !parts_.empty()) {     // text alone: whole, wrapped, each part in its own colour
        juce::AttributedString s;
        for (const Part& p : parts_) s.append(p.text, f, p.muted ? colours::muted : colours::text);
        s.setJustification(juce::Justification::centredLeft);
        s.setWordWrap(juce::AttributedString::byWord);
        s.draw(g, textArea_);
        return;
    }
    for (size_t i = 0; i < parts_.size() && i < partBounds_.size(); ++i)
        if (!parts_[i].field) drawText(g, parts_[i].text, f, partBounds_[i], parts_[i].muted ? colours::muted : colours::text);
}

// ------------------------------------------------------------------ Frame

Frame::Frame(const Theme& theme, const juce::String& title, const juce::StringArray& tabs)
    : theme_(theme), title_(title), tabs_(theme, tabs), close_(theme), footer_(theme) {
    addAndMakeVisible(tabs_);
    addAndMakeVisible(close_);
    addAndMakeVisible(footer_);
    close_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> Frame::windowBounds() const { return getLocalBounds().toFloat().reduced(dp::inset); }

juce::Rectangle<int> Frame::bodyBounds() const {
    auto w = windowBounds();
    w.removeFromTop(dp::header);
    w.removeFromBottom(dp::footer);
    return w.reduced(dp::padX, dp::padY).toNearestInt();
}

void Frame::resized() {
    const auto w = windowBounds();
    const float titleW = textWidth(theme_.font(dp::titleText, true), title_) + 28;
    const float tabsH = 34;
    tabs_.setBounds(juce::Rectangle<float>(w.getX() + dp::padX + titleW, w.getY() + (dp::header - tabsH) / 2, tabs_.idealWidth() + 2, tabsH).toNearestInt());
    const float c = 34;
    close_.setBounds(juce::Rectangle<float>(w.getRight() - dp::padX - c, w.getY() + (dp::header - c) / 2, c, c).toNearestInt());
    footer_.setBounds(juce::Rectangle<float>(w.getX(), w.getBottom() - dp::footer, w.getWidth(), dp::footer).toNearestInt());
    if (onBodyResized) onBodyResized();
}

void Frame::paint(juce::Graphics& g) {
    g.fillAll(colours::overlay);
    const auto w = windowBounds();
    g.setColour(colours::window);
    g.fillRoundedRectangle(w, dp::radius);
    g.setColour(colours::line);
    g.drawRoundedRectangle(w.reduced(0.5f), dp::radius, 1.0f);
    g.drawHorizontalLine(int(w.getY() + dp::header), w.getX(), w.getRight());
    g.drawHorizontalLine(int(w.getBottom() - dp::footer), w.getX(), w.getRight());
    const juce::Font tf = theme_.font(dp::titleText, true);   // as wide as resized() leaves it before the tabs
    drawText(g, title_, tf, {w.getX() + dp::padX, w.getY(), textWidth(tf, title_) + 2, dp::header}, colours::text);
}

// ------------------------------------------------------------------ MenuLookAndFeel

float MenuLookAndFeel::textSize() const { return juce::jlimit(12.0f, 20.0f, 14.0f * scale_); }

juce::Font MenuLookAndFeel::getPopupMenuFont() { return theme_.font(textSize()); }

void MenuLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    g.fillAll(colours::row);
    g.setColour(colours::buttonEdge);
    g.drawRect(0, 0, width, height, 1);
}

void MenuLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                        bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                        const juce::String& shortcutKeyText, const juce::Drawable*, const juce::Colour*) {
    const auto r = area.toFloat();
    if (isSeparator) {
        g.setColour(colours::buttonEdge);
        g.fillRect(r.getX() + 8, r.getCentreY() - 0.5f, r.getWidth() - 16, 1.0f);
        return;
    }
    const bool hi = isHighlighted && isActive;
    if (hi) {
        g.setColour(theme_.accent);
        g.fillRoundedRectangle(r.reduced(3, 1), 5);
    }
    const juce::Colour fg = hi ? juce::Colours::white : isActive ? colours::text : colours::muted.withMultipliedAlpha(0.7f);
    const float h = r.getHeight(), ts = textSize();
    auto inner = r.reduced(8, 0);
    const auto tickBox = inner.removeFromLeft(h * 0.8f);
    if (isTicked) drawTick(g, tickBox.withSizeKeepingCentre(ts * 0.9f, ts * 0.9f), fg, std::max(1.5f, ts / 8));
    if (hasSubMenu) drawArrow(g, inner.removeFromRight(h * 0.7f).withSizeKeepingCentre(ts * 0.8f, ts * 0.8f), fg);
    const juce::Font f = getPopupMenuFont();
    if (shortcutKeyText.isNotEmpty()) {
        const float sw = textWidth(f, shortcutKeyText) + 12;
        drawText(g, shortcutKeyText, f, inner.removeFromRight(sw), hi ? juce::Colours::white : colours::muted, juce::Justification::centredRight);
    }
    drawText(g, text, f, inner, fg);
}

void MenuLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name) {
    drawText(g, name, theme_.font(textSize() * 0.86f, true), area.toFloat().reduced(12, 0).withTrimmedTop(2), colours::heading);
}

void MenuLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int, int& w, int& h) {
    const float ts = textSize();
    if (isSeparator) { w = 50; h = int(ts * 0.8f); return; }
    h = int(std::round(ts * 2.0f));
    w = int(std::ceil(textWidth(getPopupMenuFont(), text))) + int(h * 0.8f) + int(h * 0.7f) + 28;
}

void MenuLookAndFeel::drawPopupMenuUpDownArrow(juce::Graphics& g, int width, int height, bool up) {
    g.fillAll(colours::row);
    const float s = std::min(float(height), 12.0f);
    juce::Path p = arrowPath({0, 0, s, s});
    p.applyTransform(juce::AffineTransform::rotation(up ? -juce::MathConstants<float>::halfPi : juce::MathConstants<float>::halfPi, s / 2, s / 2)
                         .translated(float(width) / 2 - s / 2, float(height) / 2 - s / 2));
    g.setColour(colours::muted);
    g.fillPath(p);
}

}  // namespace ui
