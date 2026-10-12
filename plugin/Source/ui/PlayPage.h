#pragma once

#include "SongDisplay.h"
#include "Widgets.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

class SyrinxAudioProcessor;

namespace syrinx::ui {

/// WP-R4: the instrument page. The live display on top, then five cards:
/// Melody · Height · Song · Flock · Sound (the whistle voice's controls, D-025).
class PlayPage : public juce::Component {
public:
    explicit PlayPage(SyrinxAudioProcessor& proc);
    ~PlayPage() override;

    void resized() override;
    /// Message thread, ~30 Hz: display, mod wheel arc, hints, enabled state.
    void update();
    [[nodiscard]] SongDisplay& display() noexcept { return display_; }

private:
    void updateHints();

    SyrinxAudioProcessor& proc_;
    SongDisplay display_;
    Card melody_{"Melody"}, height_{"Height"}, song_{"Song"}, flock_{"Flock"}, sound_{"Sound"};

    std::unique_ptr<Knob> melodyKnob_, sizeKnob_, pauseKnob_, birdsKnob_, volumeKnob_;
    std::unique_ptr<Switch> firstOnNote_, harmonics_, room_;
    std::unique_ptr<ChoiceBox> noteRule_;
    std::unique_ptr<Segmented> mapping_, order_, voice_;
    std::unique_ptr<MiniSlider> spread_;
    juce::Label mappingCaption_, orderCaption_, heightHint_, songHint_, flockHint_;
    std::unique_ptr<juce::ParameterAttachment> mappingWatch_, orderWatch_;
    int shownMapping_ = -1, shownOrder_ = -1;
    bool whistle_ = true;
};

} // namespace syrinx::ui
