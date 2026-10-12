#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

/// WP-R4: the Play page's controls. Each one is bound to a plugin parameter (APVTS attachments, message
/// thread only) and carries its help text (Component::setHelpText), which the editor's help line shows.
namespace syrinx::ui {

/// A titled panel.
class Card : public juce::Component {
public:
    explicit Card(juce::String title) : title_(std::move(title)) { setInterceptsMouseClicks(false, true); }
    void paint(juce::Graphics&) override;
    /// The area below the title, for the card's controls.
    [[nodiscard]] juce::Rectangle<int> content() const { return getLocalBounds().reduced(14, 0).withTrimmedTop(38).withTrimmedBottom(14); }

private:
    juce::String title_;
};

/// A big knob: arc and value inside, name below, optional captions under the arc ends.
class Knob : public juce::Component {
public:
    using Formatter = std::function<juce::String(double plain)>;

    Knob(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, juce::String name,
         Formatter format, juce::String leftCaption = {}, juce::String rightCaption = {}, bool bipolar = false);

    void resized() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    /// The mod wheel's share, shown as a second arc (0 = none).
    void setModAmount(float amount);
    [[nodiscard]] juce::Slider& slider() noexcept { return slider_; }

private:
    juce::Slider slider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
    juce::RangedAudioParameter* param_ = nullptr;
    juce::String name_, left_, right_;
    Formatter format_;
    float mod_ = 0.0f;
};

/// Two or three exclusive options side by side (a choice parameter, or a plain callback).
class Segmented : public juce::Component {
public:
    /// Bound to a choice parameter; `labels` are the short texts shown, in the parameter's choice order.
    Segmented(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, juce::StringArray labels);
    /// Not bound to a parameter: `onChange` is called with the new index.
    Segmented(juce::StringArray labels, std::function<void(int)> onChange);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void setIndex(int index, bool notify);
    [[nodiscard]] int index() const noexcept { return index_; }
    void setTextHeight(float h) { textHeight_ = h; }

private:
    int segmentAt(juce::Point<int> p) const;
    juce::StringArray labels_;
    std::function<void(int)> onChange_;
    std::unique_ptr<juce::ParameterAttachment> attachment_;
    int index_ = 0;
    int hover_ = -1;
    float textHeight_ = 12.5f;
};

/// An on/off switch with its label (a bool parameter).
class Switch : public juce::Component {
public:
    Switch(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, juce::String label);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    [[nodiscard]] bool on() const noexcept { return on_; }

private:
    juce::String label_;
    std::unique_ptr<juce::ParameterAttachment> attachment_;
    bool on_ = false;
};

/// A small horizontal slider with its name and value.
class MiniSlider : public juce::Component {
public:
    MiniSlider(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, juce::String name,
               Knob::Formatter format);
    void resized() override;
    void paint(juce::Graphics&) override;
    [[nodiscard]] juce::Slider& slider() noexcept { return slider_; }

private:
    juce::Slider slider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
    juce::RangedAudioParameter* param_ = nullptr;
    juce::String name_;
    Knob::Formatter format_;
};

/// A drop-down list with a small caption above (a choice parameter).
class ChoiceBox : public juce::Component {
public:
    ChoiceBox(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, juce::String caption,
              juce::StringArray labels);
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    juce::ComboBox box_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment_;
    juce::String caption_;
};

} // namespace syrinx::ui
