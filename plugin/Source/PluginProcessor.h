#pragma once

#include "ParameterLayout.h"
#include "SafetyLimiter.h"
#include "cuicui/control/inverse_map.hpp"
#include "cuicui/control/voice_controller.hpp"
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
        float target_hz = 0.0f;
        int active_side = 0;
        bool folded = false;
    };

    TelemetrySnapshot telemetrySnapshot() const noexcept;

    /// Set a host parameter from a plain-unit value (offline renderer / tests).
    bool setParamPlain(std::string_view id, float value);

    [[nodiscard]] int voiceLatencySamples() const noexcept { return voice_.latencySamples(); }

private:
    struct HeldNote {
        int note = 60;
        int vel = 100;
    };

    void processSubblocks(float* dest, int n, const juce::MidiBuffer& midi, int timeOffset) noexcept;
    void cacheParamPointers();
    void handleMidi(const juce::MidiMessage& msg) noexcept;
    void applyPhysiologyNote(cuicui::ControlFrame& frame) noexcept;
    void applyPerformTick(cuicui::ControlFrame& frame, bool noteOnEvent, bool noteOffEvent) noexcept;
    cuicui::ControlFrame readControlFrame() const noexcept;
    cuicui::VoiceSettings readSettings() const noexcept;
    int readOversampling() const noexcept;
    std::uint64_t readSeed() const noexcept;
    float raw(cuicui::ParamId id) const noexcept;
    int choiceIndex(juce::AudioParameterChoice* p) const noexcept;
    bool performMode() const noexcept;
    void publishTelemetry() noexcept;
    void primeVoice() noexcept;
    void pushHeld(int note, int vel) noexcept;
    void popHeld(int note) noexcept;

    cuicui::BirdVoice voice_;
    cuicui::control::InverseMap map_;
    cuicui::control::VoiceController controller_;
    SafetyLimiter limiter_;
    cuicui::dsp::Smoother masterSm_;

    std::array<std::atomic<float>*, static_cast<std::size_t>(cuicui::kParamCount)> raw_{};
    juce::AudioParameterInt* seedParam_ = nullptr;
    juce::AudioParameterChoice* osParam_ = nullptr;
    juce::AudioParameterChoice* modeParam_ = nullptr;
    juce::AudioParameterChoice* scaleParam_ = nullptr;

    std::vector<float> mono_;
    cuicui::ControlFrame lastFrame_{};
    bool prepared_ = false;

    HeldNote held_[kMaxHeldNotes]{};
    int nHeld_ = 0;
    float bendNorm_ = 0.0f; // -1..1
    float aftertouch_ = 0.0f;
    float polyAt_ = 0.0f;
    float midiPressurePhys_ = 0.0f;
    float midiBeta_ = 0.3f;
    bool betaDriven_ = false;
    float cc1_ = 0.0f;   // 0–1
    float cc74_ = 0.5f;  // 0–1, 0.5 = no tone offset
    bool sustain_ = false;
    int lastNote_ = 60;
    int lastVel_ = 100;
    bool wasGated_ = false;

    std::atomic<float> telem_f0_{0.0f};
    std::atomic<float> telem_aL_{0.0f};
    std::atomic<float> telem_aR_{0.0f};
    std::atomic<float> telem_bL_{0.0f};
    std::atomic<float> telem_bR_{0.0f};
    std::atomic<float> telem_p_{0.0f};
    std::atomic<float> telem_level_{ -120.0f};
    std::atomic<std::uint32_t> telem_resets_{0};
    std::atomic<float> telem_target_{0.0f};
    std::atomic<int> telem_side_{0};
    std::atomic<int> telem_folded_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SyrinxAudioProcessor)
};
