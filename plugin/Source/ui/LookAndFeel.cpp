#include "LookAndFeel.h"

#include <cmath>

namespace syrinx::ui {

namespace {

juce::Point<float> onCircle(juce::Point<float> c, float r, float angle) {
    return {c.x + r * std::sin(angle), c.y - r * std::cos(angle)};
}

void strokeArc(juce::Graphics& g, juce::Point<float> c, float r, float a0, float a1, float thickness) {
    if (std::abs(a1 - a0) < 1e-4f) {
        return;
    }
    juce::Path p;
    p.addCentredArc(c.x, c.y, r, r, 0.0f, std::min(a0, a1), std::max(a0, a1), true);
    g.strokePath(p, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void drawSwitch(juce::Graphics& g, juce::Rectangle<float> r, bool on, bool enabled, bool over) {
    const float alpha = enabled ? 1.0f : 0.35f;
    const float h = r.getHeight();
    g.setColour((on ? colours::accent : colours::track.brighter(over ? 0.15f : 0.0f)).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(r, h * 0.5f);
    const float d = h - 4.0f;
    const float x = on ? r.getRight() - 2.0f - d : r.getX() + 2.0f;
    auto thumb = juce::Rectangle<float>(x, r.getY() + 2.0f, d, d);
    g.setColour(juce::Colours::black.withAlpha(0.25f * alpha));
    g.fillEllipse(thumb.translated(0.0f, 0.8f));
    g.setColour(colours::text.withMultipliedAlpha(alpha));
    g.fillEllipse(thumb);
}

} // namespace

SyrinxLookAndFeel::SyrinxLookAndFeel() {
    setColourScheme({colours::bgTop, colours::surface, colours::surface, colours::border, colours::text,
                     colours::accent, colours::text, colours::accent.withAlpha(0.25f), colours::text});

    setColour(juce::ResizableWindow::backgroundColourId, colours::bgTop);
    setColour(juce::DocumentWindow::backgroundColourId, colours::bgTop);
    setColour(juce::Label::textColourId, colours::text2);
    setColour(juce::Label::textWhenEditingColourId, colours::text);
    setColour(juce::Slider::textBoxTextColourId, colours::text);
    setColour(juce::Slider::textBoxBackgroundColourId, colours::inset);
    setColour(juce::Slider::textBoxOutlineColourId, colours::insetBorder);
    setColour(juce::Slider::textBoxHighlightColourId, colours::accent.withAlpha(0.35f));
    setColour(juce::ComboBox::textColourId, colours::text);
    setColour(juce::ComboBox::backgroundColourId, colours::inset);
    setColour(juce::ComboBox::outlineColourId, colours::insetBorder);
    setColour(juce::ComboBox::arrowColourId, colours::text2);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xfa1d2025)); // not opaque: rounded corners
    setColour(juce::PopupMenu::textColourId, colours::text);
    setColour(juce::PopupMenu::headerTextColourId, colours::text3);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha(0.18f));
    setColour(juce::PopupMenu::highlightedTextColourId, colours::text);
    setColour(juce::TextButton::buttonColourId, colours::surfaceTop);
    setColour(juce::TextButton::buttonOnColourId, colours::accent);
    setColour(juce::TextButton::textColourOffId, colours::text);
    setColour(juce::TextButton::textColourOnId, colours::inset);
    setColour(juce::ToggleButton::textColourId, colours::text2);
    setColour(juce::TextEditor::backgroundColourId, colours::inset);
    setColour(juce::TextEditor::textColourId, colours::text);
    setColour(juce::TextEditor::highlightColourId, colours::accent.withAlpha(0.35f));
    setColour(juce::TextEditor::highlightedTextColourId, colours::text);
    setColour(juce::TextEditor::outlineColourId, colours::insetBorder);
    setColour(juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour(juce::CaretComponent::caretColourId, colours::accent);
    setColour(juce::AlertWindow::backgroundColourId, colours::surface);
    setColour(juce::AlertWindow::textColourId, colours::text);
    setColour(juce::AlertWindow::outlineColourId, colours::border);
    setColour(juce::GroupComponent::outlineColourId, colours::border);
    setColour(juce::GroupComponent::textColourId, colours::text3);
    setColour(juce::ScrollBar::thumbColourId, colours::text3);
    setColour(juce::TooltipWindow::backgroundColourId, colours::surfaceTop);
    setColour(juce::TooltipWindow::textColourId, colours::text);
    setColour(juce::TooltipWindow::outlineColourId, colours::border);
}

// --------------------------------------------------------------------------------------------- knobs
void SyrinxLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0,
                                         float a1, juce::Slider& s) {
    const float alpha = s.isEnabled() ? 1.0f : 0.35f;
    const auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat();
    const float d = std::min(bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float ring = d * 0.5f - 4.0f;            // value ring radius
    const float thick = std::max(3.0f, d * 0.045f); // ring thickness
    const float body = ring - thick - std::max(4.0f, d * 0.07f);
    const float angle = a0 + pos * (a1 - a0);
    const bool bipolar = static_cast<bool>(s.getProperties().getWithDefault("bipolar", false));
    const float from = bipolar ? (a0 + a1) * 0.5f : a0;

    g.setColour(colours::track.withMultipliedAlpha(alpha));
    strokeArc(g, c, ring, a0, a1, thick);

    const float mod = static_cast<float>(static_cast<double>(s.getProperties().getWithDefault("modAmount", 0.0)));
    if (mod > 0.0005f) { // the mod wheel's share, beyond the knob's own value
        const float modEnd = a0 + std::min(1.0f, pos + mod) * (a1 - a0);
        g.setColour(colours::accent2.withAlpha(0.16f * alpha));
        strokeArc(g, c, ring, angle, modEnd, thick * 2.6f);
        g.setColour(colours::accent2.withMultipliedAlpha(alpha));
        strokeArc(g, c, ring, angle, modEnd, thick);
    }
    g.setColour(colours::accent.withAlpha(0.14f * alpha));
    strokeArc(g, c, ring, from, angle, thick * 2.8f);
    g.setColour(colours::accent.withMultipliedAlpha(alpha));
    strokeArc(g, c, ring, from, angle, thick);
    // the value's end: a small bright bead on the ring
    const auto bead = onCircle(c, ring, angle);
    g.setColour(colours::text.withMultipliedAlpha(alpha));
    g.fillEllipse(juce::Rectangle<float>(thick * 1.25f, thick * 1.25f).withCentre(bead));

    // the knob body: soft shadow, gradient, rim, top light
    const auto bodyRect = juce::Rectangle<float>(body * 2.0f, body * 2.0f).withCentre(c);
    {
        juce::Path shadow;
        shadow.addEllipse(bodyRect.translated(0.0f, 2.5f));
        juce::DropShadow(juce::Colours::black.withAlpha(0.55f * alpha), static_cast<int>(d * 0.12f), {0, 2})
            .drawForPath(g, shadow);
    }
    g.setGradientFill(juce::ColourGradient(colours::knobTop.withMultipliedAlpha(alpha), c.x, bodyRect.getY(),
                                           colours::knobBottom.withMultipliedAlpha(alpha), c.x, bodyRect.getBottom(),
                                           false));
    g.fillEllipse(bodyRect);
    g.setColour(juce::Colours::black.withAlpha(0.5f * alpha));
    g.drawEllipse(bodyRect, 1.0f);
    {
        juce::Path light;
        light.addCentredArc(c.x, c.y, body - 1.0f, body - 1.0f, 0.0f, -1.9f, 1.9f, true);
        g.setColour(juce::Colours::white.withAlpha(0.09f * alpha));
        g.strokePath(light, juce::PathStrokeType(1.0f));
    }
    if (static_cast<bool>(s.getProperties().getWithDefault("valueInside", false))) {
        return; // the knob shows its value in the middle; the bead on the ring is the pointer
    }
    g.setColour(colours::text.withMultipliedAlpha(alpha));
    juce::Path ptr;
    ptr.startNewSubPath(onCircle(c, body * 0.42f, angle));
    ptr.lineTo(onCircle(c, body * 0.80f, angle));
    g.strokePath(ptr, juce::PathStrokeType(std::max(2.0f, d * 0.03f), juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
}

// --------------------------------------------------------------------------------------------- sliders
void SyrinxLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                         juce::Slider::SliderStyle style, juce::Slider& s) {
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0.0f, 0.0f, style, s);
        return;
    }
    const float alpha = s.isEnabled() ? 1.0f : 0.35f;
    const auto r = juce::Rectangle<int>(x, y, w, h).toFloat();
    const float cy = r.getCentreY();
    const float th = 4.0f;
    const auto trackR = juce::Rectangle<float>(r.getX(), cy - th * 0.5f, r.getWidth(), th);
    g.setColour(colours::track.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(trackR, th * 0.5f);
    const float px = juce::jlimit(r.getX(), r.getRight(), pos);
    g.setColour(colours::accent.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(trackR.withRight(px), th * 0.5f);
    const float d = 12.0f;
    const auto thumb = juce::Rectangle<float>(d, d).withCentre({px, cy});
    g.setColour(juce::Colours::black.withAlpha(0.35f * alpha));
    g.fillEllipse(thumb.translated(0.0f, 1.0f));
    g.setColour((s.isMouseOverOrDragging() ? juce::Colours::white : colours::text).withMultipliedAlpha(alpha));
    g.fillEllipse(thumb);
}

juce::Label* SyrinxLookAndFeel::createSliderTextBox(juce::Slider& s) {
    auto* l = LookAndFeel_V4::createSliderTextBox(s);
    l->setFont(font(12.0f, Weight::medium, true));
    l->setJustificationType(juce::Justification::centred);
    l->setColour(juce::Label::backgroundColourId, colours::inset);
    l->setColour(juce::Label::outlineColourId, colours::insetBorder);
    l->setColour(juce::Label::textColourId, colours::text);
    return l;
}

// --------------------------------------------------------------------------------------------- combo boxes
void SyrinxLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool isDown, int, int, int, int,
                                     juce::ComboBox& box) {
    const float alpha = box.isEnabled() ? 1.0f : 0.4f;
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)).reduced(0.5f);
    g.setColour(colours::inset.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(r, 7.0f);
    const bool hot = isDown || box.isPopupActive() || box.isMouseOver(true);
    g.setColour((hot ? colours::text3 : colours::insetBorder).withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(r, 7.0f, 1.0f);
    const float cx = static_cast<float>(w) - 14.0f;
    const float cy = static_cast<float>(h) * 0.5f;
    juce::Path chev;
    chev.startNewSubPath(cx - 4.0f, cy - 2.0f);
    chev.lineTo(cx, cy + 2.2f);
    chev.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(colours::text2.withMultipliedAlpha(alpha));
    g.strokePath(chev, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font SyrinxLookAndFeel::getComboBoxFont(juce::ComboBox&) {
    return font(12.5f, Weight::medium);
}

void SyrinxLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

// --------------------------------------------------------------------------------------------- menus
void SyrinxLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h) {
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h));
    g.setColour(findColour(juce::PopupMenu::backgroundColourId).withAlpha(1.0f));
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(colours::border.brighter(0.1f));
    g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
}

void SyrinxLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                          bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                          const juce::String& text, const juce::String& shortcut,
                                          const juce::Drawable* icon, const juce::Colour* textColour) {
    if (isSeparator) {
        g.setColour(colours::border);
        g.fillRect(area.reduced(10, 0).withHeight(1).withY(area.getCentreY()));
        return;
    }
    auto r = area.reduced(4, 1).toFloat();
    if (isHighlighted && isActive) {
        g.setColour(findColour(juce::PopupMenu::highlightedBackgroundColourId));
        g.fillRoundedRectangle(r, 5.0f);
    }
    if (isTicked) {
        g.setColour(colours::accent);
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({r.getX() + 11.0f, r.getCentreY()}));
    }
    const auto col = textColour != nullptr ? *textColour : colours::text;
    g.setColour(isActive ? col : colours::textOff);
    g.setFont(getPopupMenuFont());
    g.drawText(text, r.withTrimmedLeft(22.0f).withTrimmedRight(hasSubMenu ? 20.0f : 8.0f), juce::Justification::centredLeft,
               true);
    if (shortcut.isNotEmpty()) {
        g.setColour(colours::text3);
        g.drawText(shortcut, r.withTrimmedRight(10.0f), juce::Justification::centredRight, true);
    }
    if (hasSubMenu) {
        const float cx = r.getRight() - 12.0f;
        juce::Path chev;
        chev.startNewSubPath(cx - 2.0f, r.getCentreY() - 4.0f);
        chev.lineTo(cx + 2.0f, r.getCentreY());
        chev.lineTo(cx - 2.0f, r.getCentreY() + 4.0f);
        g.strokePath(chev, juce::PathStrokeType(1.5f));
    }
    juce::ignoreUnused(icon);
}

void SyrinxLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area,
                                                   const juce::String& name) {
    g.setFont(titleFont(10.0f));
    g.setColour(colours::text3);
    g.drawText(name.toUpperCase(), area.reduced(14, 0).withTrimmedTop(4), juce::Justification::bottomLeft, true);
}

juce::Font SyrinxLookAndFeel::getPopupMenuFont() {
    return font(13.0f, Weight::medium);
}

// --------------------------------------------------------------------------------------------- buttons
void SyrinxLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over,
                                             bool down) {
    const float alpha = b.isEnabled() ? 1.0f : 0.4f;
    const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = b.getToggleState();
    auto top = on ? colours::accent : colours::surfaceTop.brighter(over ? 0.12f : 0.04f);
    auto bottom = on ? colours::accent.darker(0.15f) : colours::surface;
    if (down) {
        std::swap(top, bottom);
    }
    g.setGradientFill(juce::ColourGradient(top.withMultipliedAlpha(alpha), 0.0f, r.getY(),
                                           bottom.withMultipliedAlpha(alpha), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle(r, 7.0f);
    g.setColour((on ? colours::accent.brighter(0.2f) : colours::border.brighter(over ? 0.2f : 0.0f))
                    .withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(r, 7.0f, 1.0f);
}

juce::Font SyrinxLookAndFeel::getTextButtonFont(juce::TextButton&, int h) {
    return font(std::min(13.0f, static_cast<float>(h) * 0.5f), Weight::medium);
}

void SyrinxLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    g.setFont(getTextButtonFont(b, b.getHeight()));
    g.setColour((b.getToggleState() ? colours::inset : colours::text).withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.4f));
    g.drawText(b.getButtonText(), b.getLocalBounds().reduced(6, 0), juce::Justification::centred, true);
}

void SyrinxLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool) {
    const auto r = b.getLocalBounds().toFloat();
    const float h = std::min(16.0f, r.getHeight() - 4.0f);
    const auto sw = juce::Rectangle<float>(r.getX() + 2.0f, r.getCentreY() - h * 0.5f, h * 1.8f, h);
    drawSwitch(g, sw, b.getToggleState(), b.isEnabled(), over);
    if (b.getButtonText().isNotEmpty()) {
        g.setFont(font(12.5f, Weight::medium));
        g.setColour(colours::text2.withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.4f));
        g.drawText(b.getButtonText(), r.withTrimmedLeft(sw.getWidth() + 10.0f), juce::Justification::centredLeft, true);
    }
}

// --------------------------------------------------------------------------------------------- groups, scroll
void SyrinxLookAndFeel::drawGroupComponentOutline(juce::Graphics& g, int w, int h, const juce::String& text,
                                                  const juce::Justification&, juce::GroupComponent&) {
    // the same panel as the Play page's cards: title inside, after a small beak-coloured dot
    const auto r = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(w) - 1.0f, static_cast<float>(h) - 1.0f);
    g.setGradientFill(juce::ColourGradient(colours::surfaceTop, 0.0f, r.getY(), colours::surface, 0.0f, r.getBottom(),
                                           false));
    g.fillRoundedRectangle(r, 10.0f);
    g.setColour(colours::border);
    g.drawRoundedRectangle(r, 10.0f, 1.0f);
    if (text.isNotEmpty()) {
        g.setColour(colours::accent.withAlpha(0.8f));
        g.fillEllipse(juce::Rectangle<float>(4.5f, 4.5f).withCentre({16.5f, 16.5f}));
        g.setColour(colours::text2);
        g.setFont(titleFont(10.0f));
        g.drawText(text.toUpperCase(), juce::Rectangle<float>(25.0f, 8.0f, r.getWidth() - 30.0f, 17.0f),
                   juce::Justification::centredLeft, false);
    }
}

void SyrinxLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                      int thumbStart, int thumbSize, bool over, bool down) {
    juce::Rectangle<float> thumb;
    if (vertical) {
        thumb = juce::Rectangle<float>(static_cast<float>(x) + 2.0f, static_cast<float>(thumbStart),
                                       static_cast<float>(w) - 4.0f, static_cast<float>(thumbSize));
    } else {
        thumb = juce::Rectangle<float>(static_cast<float>(thumbStart), static_cast<float>(y) + 2.0f,
                                       static_cast<float>(thumbSize), static_cast<float>(h) - 4.0f);
    }
    g.setColour(colours::text3.withAlpha(down ? 0.8f : (over ? 0.6f : 0.35f)));
    g.fillRoundedRectangle(thumb.reduced(1.0f), std::min(thumb.getWidth(), thumb.getHeight()) * 0.5f);
}

// --------------------------------------------------------------------------------------------- text, dialogs
juce::Font SyrinxLookAndFeel::getLabelFont(juce::Label& l) {
    juce::ignoreUnused(l);
    return font(12.5f, Weight::medium);
}

juce::Font SyrinxLookAndFeel::getAlertWindowTitleFont() {
    return font(17.0f, Weight::demibold);
}

juce::Font SyrinxLookAndFeel::getAlertWindowMessageFont() {
    return font(13.5f);
}

juce::Font SyrinxLookAndFeel::getAlertWindowFont() {
    return font(13.0f, Weight::medium);
}

void SyrinxLookAndFeel::drawAlertBox(juce::Graphics& g, juce::AlertWindow& w, const juce::Rectangle<int>& textArea,
                                     juce::TextLayout& layout) {
    const auto r = w.getLocalBounds().toFloat();
    g.setColour(colours::surface);
    g.fillRoundedRectangle(r, 10.0f);
    g.setColour(colours::border.brighter(0.15f));
    g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.0f);
    g.setColour(colours::text);
    layout.draw(g, textArea.toFloat().withTrimmedLeft(4.0f));
}

void SyrinxLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int w, int h, juce::TextEditor&) {
    g.setColour(colours::inset);
    g.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), 6.0f);
}

void SyrinxLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int w, int h, juce::TextEditor& e) {
    g.setColour(e.hasKeyboardFocus(true) ? colours::accent : colours::insetBorder);
    g.drawRoundedRectangle(juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(w) - 1.0f, static_cast<float>(h) - 1.0f),
                           6.0f, 1.0f);
}

juce::Rectangle<int> SyrinxLookAndFeel::getTooltipBounds(const juce::String& text, juce::Point<int> pos,
                                                         juce::Rectangle<int> parent) {
    const auto f = font(12.0f);
    const int w = static_cast<int>(std::min(360.0f, textWidth(f, text) + 20.0f));
    const int lines = static_cast<int>(std::ceil((textWidth(f, text) + 20.0f) / 360.0f));
    const int h = 10 + lines * 16;
    return juce::Rectangle<int>(pos.x - w / 2, pos.y + 18, w, h).constrainedWithin(parent);
}

void SyrinxLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int w, int h) {
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h));
    g.setColour(colours::surfaceTop);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(colours::border.brighter(0.2f));
    g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
    g.setColour(colours::text);
    g.setFont(font(12.0f));
    g.drawFittedText(text, r.reduced(10.0f, 4.0f).toNearestInt(), juce::Justification::centredLeft, 4);
}

} // namespace syrinx::ui
