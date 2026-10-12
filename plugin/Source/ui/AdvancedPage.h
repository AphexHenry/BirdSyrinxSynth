#pragma once

#include "Theme.h"

#include "cuicui/params.hpp"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <string>
#include <vector>

class SyrinxAudioProcessor;

namespace syrinx::ui {

/// WP-R4: the anatomical engine's controls, as they were in the R1 editor (one row per parameter, grouped),
/// on their own page. The whistle's controls live on the Play page and are not repeated here.
class AdvancedPage : public juce::Component {
public:
    explicit AdvancedPage(SyrinxAudioProcessor& proc);
    ~AdvancedPage() override;

    void resized() override;
    void paint(juce::Graphics&) override;
    /// Message thread: the anatomical engine's live values.
    void update();

    /// The Play page shows these; the Advanced page leaves them out.
    static bool isPlayControl(std::string_view id);

private:
    struct Row {
        juce::Slider slider;
        juce::Label label;
        juce::ComboBox combo;
        juce::ToggleButton toggle;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bAttach;
        bool isCombo = false;
        bool isToggle = false;
        std::string id;
    };
    void addRow(const cuicui::ParamSpec& spec);

    SyrinxAudioProcessor& proc_;
    juce::Label telem_;
    juce::Viewport viewport_;
    juce::Component content_;
    std::vector<std::unique_ptr<juce::GroupComponent>> groups_;
    std::vector<std::unique_ptr<Row>> rows_;
};

} // namespace syrinx::ui
