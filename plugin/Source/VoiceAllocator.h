#pragma once

#include "cuicui/control/gesture_engine.hpp"
#include "cuicui/control/inverse_map.hpp"
#include "cuicui/control/voice_controller.hpp"
#include "cuicui/model/bird_voice.hpp"
#include "cuicui/params.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

/// Polyphony, steal, stereo spread, per-voice GestureEngine. WP-C5 §4.3.
class VoiceAllocator {
public:
    static constexpr int kMaxVoices = 16;
    static constexpr int kFadeMs = 5;

    void prepare(const cuicui::ProcessSpec& spec, const cuicui::control::InverseMap& map);
    void reset(std::uint64_t engineSeed) noexcept;
    /// Prime every slot at the current body size / rest labia (matches C3 `primeVoice`).
    void prime(const cuicui::VoiceSettings& settings, double alpha, double beta, double oecHz, double gape,
               double alphaOffR, double betaR, const cuicui::ControlFrame& restFrame) noexcept;

    void setPolyphony(int n) noexcept;
    void setSpread(float spread) noexcept { spread_ = std::clamp(spread, 0.0f, 1.0f); }
    void setCpuCap(int cap) noexcept;

    void noteOn(int note, int vel, std::uint32_t phraseSeed) noexcept;
    void noteOff(int note) noexcept;
    void allNotesOff() noexcept;
    void setMono(int note, int vel, bool gate, bool noteOnEvent, bool noteOffEvent,
                 std::uint32_t phraseSeed) noexcept;

    void process(float* left, float* right, int n, const cuicui::control::PerformInputs& macros,
                 const cuicui::VoiceSettings& settings, const cuicui::ControlFrame& hostFrame) noexcept;

    [[nodiscard]] int latencySamples() const noexcept { return slots_[0].voice.latencySamples(); }
    [[nodiscard]] int playingCount() const noexcept;
    [[nodiscard]] int stealCount() const noexcept { return stealCount_; }
    [[nodiscard]] cuicui::control::VoiceController::Debug firstDebug() const noexcept;
    [[nodiscard]] cuicui::VoiceTelemetry firstTelemetry() const noexcept;
    [[nodiscard]] int cpuCap() const noexcept { return cpuCap_; }
    [[nodiscard]] bool usedSteal() const noexcept { return stealCount_ > 0; }

    void notifyBlockTime(double elapsedSec, double blockSec) noexcept;

    cuicui::BirdVoice& monoVoice() noexcept { return slots_[0].voice; }

private:
    struct Slot {
        cuicui::BirdVoice voice;
        cuicui::control::VoiceController controller;
        cuicui::control::GestureEngine engine;
        cuicui::ControlFrame lastFrame{};
        int note = -1;
        int vel = 100;
        std::uint32_t age = 0;
        std::uint32_t phraseSeed = 1;
        bool playing = false;
        bool gated = false;
        bool fadeIn = false;
        int fadeSamples = 0;
        int fadeLen = 1;
        float fadeFrom = 1.0f;
        float fadeTo = 1.0f;
        float panL = 1.0f;
        float panR = 1.0f;
        double sizeMul = 1.0;
        double toneOff = 0.0;
        bool prepared = false;
    };

    int allocSlot(int note) noexcept;
    void startSlot(int i, int note, int vel, std::uint32_t phraseSeed, bool fadeIn) noexcept;
    void releaseSlot(int i) noexcept;
    void updatePan(int i) noexcept;

    std::array<Slot, kMaxVoices> slots_{};
    const cuicui::control::InverseMap* map_ = nullptr;
    cuicui::ProcessSpec spec_{};
    double ctrlRate_ = 1500.0;
    int polyphony_ = 1;
    int cpuCap_ = kMaxVoices;
    int overrunBlocks_ = 0;
    float spread_ = 0.3f;
    std::uint32_t ageCounter_ = 1;
    int stealCount_ = 0;
    int fadeLen_ = 1;
    bool prepared_ = false;
    int monoNote_ = 60;
    int monoVel_ = 100;
};
