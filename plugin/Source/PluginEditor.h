#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

class SyrinxAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit SyrinxAudioProcessorEditor(SyrinxAudioProcessor&);
    ~SyrinxAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void addSlider(const cuicui::ParamSpec& spec, juce::Component& parent);

    SyrinxAudioProcessor& proc_;

    juce::Label telem_;
    juce::MidiKeyboardComponent keyboard_;
    juce::Viewport viewport_;
    juce::Component content_;

    struct Group {
        juce::GroupComponent box;
        juce::Component body;
    };
    Group breath_, srcL_, srcR_, trachea_, oec_, noise_, body_, engine_, perform_;

    struct Row {
        juce::Slider slider;
        juce::Label label;
        juce::ComboBox combo;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAttach;
        bool isCombo = false;
    };
    std::vector<std::unique_ptr<Row>> rows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SyrinxAudioProcessorEditor)
};
