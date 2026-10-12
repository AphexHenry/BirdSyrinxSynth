#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace syrinx::ui {

/// Vector-only look for every standard JUCE control used by the editor (WP-R4): knobs, sliders, combo
/// boxes, menus, buttons, toggles, group boxes, scroll bars, dialogs. The Play page's own widgets
/// (Widgets.h) draw themselves with the same palette.
class SyrinxLookAndFeel : public juce::LookAndFeel_V4 {
public:
    SyrinxLookAndFeel();

    /// Knob properties read from the slider's properties: "bipolar" (arc from the middle),
    /// "modAmount" (0–1 extra arc in the cheek colour, e.g. the mod wheel).
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle,
                          float endAngle, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;

    void drawComboBox(juce::Graphics&, int w, int h, bool isDown, int bx, int by, int bw, int bh,
                      juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                           bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText, const juce::Drawable* icon,
                           const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader(juce::Graphics&, const juce::Rectangle<int>& area,
                                    const juce::String& sectionName) override;
    juce::Font getPopupMenuFont() override;
    int getPopupMenuBorderSize() override { return 6; }

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& background, bool highlighted,
                              bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawGroupComponentOutline(juce::Graphics&, int w, int h, const juce::String& text,
                                   const juce::Justification&, juce::GroupComponent&) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart,
                       int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 10; }

    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    void drawAlertBox(juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea,
                      juce::TextLayout&) override;
    void fillTextEditorBackground(juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int w, int h, juce::TextEditor&) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& text, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int w, int h) override;
};

} // namespace syrinx::ui
