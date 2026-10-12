#pragma once

#include "ParameterLayout.h"
#include "SafetyLimiter.h"
#include "VoiceAllocator.h"
#include "cuicui/control/inverse_map.hpp"
#include "cuicui/control/syllable_bank.hpp"
#include "cuicui/control/syllable_engine.hpp"
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

    /// WP-R1: is the whistle voice selected (voice.type = whistle, the default)?
    [[nodiscard]] bool whistleSelected() const noexcept;
    /// The bird's syllables (embedded factory bank zf_22938).
    [[nodiscard]] const cuicui::control::SyllableBank& syllableBank() const noexcept { return bank_; }
    /// Offline renderer / tests: log every syllable start into a caller-owned buffer (nullptr = off).
    /// Samples are absolute since prepareToPlay, before the latency delay.
    void setSyllableLog(cuicui::control::SyllableLogEntry* buffer, int capacity) noexcept {
        whistle_.setLog(buffer, capacity);
    }
    [[nodiscard]] int syllableLogCount() const noexcept { return whistle_.logCount(); }

    struct WhistleTelemetry {
        int singers = 0;
        int voices = 0;
    };
    [[nodiscard]] WhistleTelemetry whistleTelemetry() const noexcept {
        return {telem_singers_.load(std::memory_order_relaxed), telem_voices_.load(std::memory_order_relaxed)};
    }

private:
    struct HeldNote {
        int note = 60;
        int vel = 100;
    };

    void processSubblocks(float* dest, int n, const juce::MidiBuffer& midi, int timeOffset) noexcept;
    void processAnatomical(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept;
    void processWhistle(juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, bool add) noexcept;
    void switchVoice(bool toWhistle) noexcept;
    cuicui::control::WhistleSettings readWhistleSettings() const noexcept;
    std::uint64_t whistleSeed() const noexcept;
    void cacheParamPointers();
    void handleMidi(const juce::MidiMessage& msg) noexcept;
    void applyPhysiologyNote(cuicui::ControlFrame& frame) noexcept;
    void fillPerformInputs(cuicui::control::PerformInputs& in, bool noteOnEvent, bool noteOffEvent) noexcept;
    std::uint32_t makePhraseSeed(int note) noexcept;
    void readHostTempo() noexcept;
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

    cuicui::control::SyllableBank bank_;
    cuicui::control::SyllableEngine whistle_;
    std::vector<float> wl_, wr_;            // whistle render buffers
    std::vector<float> delayL_, delayR_;    // whistle delayed by the anatomical latency (constant latency)
    int delayPos_ = 0;
    std::vector<float> fadeL_, fadeR_;      // anatomical path while it fades out after a voice switch
    static constexpr int kMaxBlockEvents = 512;
    std::array<cuicui::control::PlayEvent, kMaxBlockEvents> events_{};
    cuicui::dsp::Smoother whistleMasterSm_;
    juce::AudioParameterChoice* voiceParam_ = nullptr;
    juce::AudioParameterChoice* ruleParam_ = nullptr;
    juce::AudioParameterChoice* mappingParam_ = nullptr;
    juce::AudioParameterChoice* orderParam_ = nullptr;
    bool lastWhistle_ = true;
    int anatomicalFadeLeft_ = 0;
    int anatomicalFadeLen_ = 1;
    std::atomic<int> telem_singers_{0};
    std::atomic<int> telem_voices_{0};

    cuicui::BirdVoice voice_;
    cuicui::control::InverseMap map_;
    cuicui::control::VoiceController controller_;
    VoiceAllocator allocator_;
    SafetyLimiter limiter_;
    cuicui::dsp::Smoother masterSm_;

    std::array<std::atomic<float>*, static_cast<std::size_t>(cuicui::kParamCount)> raw_{};
    juce::AudioParameterInt* seedParam_ = nullptr;
    juce::AudioParameterChoice* osParam_ = nullptr;
    juce::AudioParameterChoice* modeParam_ = nullptr;
    juce::AudioParameterChoice* scaleParam_ = nullptr;
    juce::AudioParameterChoice* syncDivParam_ = nullptr;
    juce::AudioParameterInt* polyParam_ = nullptr;
    juce::AudioParameterInt* perfSeedParam_ = nullptr;

    std::vector<float> mono_;
    std::vector<float> right_;
    cuicui::ControlFrame lastFrame_{};
    bool prepared_ = false;
    std::uint32_t phraseCounter_ = 1;
    double hostBpm_ = 120.0;
    double hostPpq_ = 0.0;
    bool hostPlaying_ = false;
    bool hostTempoValid_ = false;

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
