#pragma once

#include "PluginProcessor.h"
#include "ui/AdvancedPage.h"
#include "ui/BirdKeyboard.h"
#include "ui/LookAndFeel.h"
#include "ui/PlayPage.h"
#include "ui/PresetBar.h"
#include "ui/Widgets.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

/// WP-R4: the Syrinx window. A fixed 1080 × 720 design (the canvas), scaled as a whole when the window is
/// resized. Header (name, Play / Advanced, presets, MIDI light) · page · help line · keyboard.
/// Everything here runs on the message thread; the audio thread is only read through atomics and
/// the lock-free SyllableFeed.
class SyrinxAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit SyrinxAudioProcessorEditor(SyrinxAudioProcessor&);
    ~SyrinxAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void showPage(int page);
    /// Offline screenshots (plugin_render): show the display at the audio clock, no timer needed.
    void prepareSnapshot();
    /// The help line's text now (tests, screenshots).
    [[nodiscard]] const juce::String& helpText() const noexcept { return canvas_.help; }
    /// One timer tick by hand (tests).
    void tick() { timerCallback(); }

private:
    class Canvas : public juce::Component {
    public:
        explicit Canvas(SyrinxAudioProcessorEditor& ed) : ed_(ed) {}
        void paint(juce::Graphics&) override;
        void paintBackground(juce::Graphics&) const;
        juce::String help;
        juce::Path logo;
        bool midiLit = false;

    private:
        void paintLive(juce::Graphics&) const;
        SyrinxAudioProcessorEditor& ed_;
        juce::Image background_; // gradients and the name, drawn once per screen scale
        float backgroundScale_ = 0.0f;
    };

    void timerCallback() override;
    void updateHelp();
    void buildLogo();

    SyrinxAudioProcessor& proc_;
    syrinx::ui::SyrinxLookAndFeel lnf_; // outlives every child (declared first)
    Canvas canvas_{*this};
    syrinx::ui::Segmented tabs_;
    syrinx::ui::PresetBar presetBar_;
    syrinx::ui::PlayPage play_;
    syrinx::ui::AdvancedPage advanced_;
    syrinx::ui::BirdKeyboard keyboard_;
    juce::Component midiZone_; // help text holder for the MIDI light

    int ticks_ = 0;
    std::uint32_t lastNotes_ = 0;
    double midiLitUntilMs_ = 0.0;
    int keyHash_ = 0;
    bool focusDone_ = false;
    bool midiChecked_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SyrinxAudioProcessorEditor)
};
