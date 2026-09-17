#pragma once

#include "ParameterLayout.h"
#include "SafetyLimiter.h"
#include "cuicui/dsp/smoother.hpp"
#include "cuicui/model/bird_voice.hpp"
#include "cuicui/params.hpp"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

class SyrinxAudioProcessor : public juce::AudioProcessor {
public:
    static constexpr int kStateVersion = 1;
    static constexpr int kSubBlock = 32;
    static constexpr int kMaxHeldNotes = 16;

    SyrinxAudioProcessor();
    ~SyrinxAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Syrinx"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

    struct TelemetrySnapshot {
        float f0_hz = 0.0f;
        float alpha_L = 0.0f, alpha_R = 0.0f;
        float beta_L = 0.0f, beta_R = 0.0f;
        float pressure = 0.0f;
        float level_db = -120.0f;
        std::uint32_t nonFiniteResets = 0;
    };

    TelemetrySnapshot telemetrySnapshot() const noexcept;

    /// Set a host parameter from a plain-unit value (offline renderer / tests).
    bool setParamPlain(std::string_view id, float value);

    [[nodiscard]] int voiceLatencySamples() const noexcept { return voice_.latencySamples(); }

private:
    void processSubblocks(float* dest, int n, const juce::MidiBuffer& midi, int timeOffset) noexcept;
    void cacheParamPointers();
    void handleMidi(const juce::MidiMessage& msg) noexcept;
    void applyNotePhysics(cuicui::ControlFrame& frame) const noexcept;
    cuicui::ControlFrame readControlFrame() const noexcept;
    cuicui::VoiceSettings readSettings() const noexcept;
    int readOversampling() const noexcept;
    std::uint64_t readSeed() const noexcept;
    float raw(cuicui::ParamId id) const noexcept;
    void publishTelemetry() noexcept;
    void primeVoice() noexcept;

    cuicui::BirdVoice voice_;
    SafetyLimiter limiter_;
    cuicui::dsp::Smoother masterSm_;

    std::array<std::atomic<float>*, static_cast<std::size_t>(cuicui::kParamCount)> raw_{};
    juce::AudioParameterInt* seedParam_ = nullptr;
    juce::AudioParameterChoice* osParam_ = nullptr;

    std::vector<float> mono_;
    cuicui::ControlFrame lastFrame_{};
    bool prepared_ = false;

    int heldNotes_[kMaxHeldNotes]{};
    int nHeld_ = 0;
    float bendSt_ = 0.0f;
    float midiPressure_ = 0.0f;
    float midiBeta_ = 0.3f;
    bool betaDriven_ = false;

    std::atomic<float> telem_f0_{0.0f};
    std::atomic<float> telem_aL_{0.0f};
    std::atomic<float> telem_aR_{0.0f};
    std::atomic<float> telem_bL_{0.0f};
    std::atomic<float> telem_bR_{0.0f};
    std::atomic<float> telem_p_{0.0f};
    std::atomic<float> telem_level_{ -120.0f};
    std::atomic<std::uint32_t> telem_resets_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SyrinxAudioProcessor)
};
