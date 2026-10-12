#pragma once

#include "cuicui/control/mode_ab.hpp"
#include "cuicui/control/playing_mode.hpp"
#include "cuicui/control/syllable_bank.hpp"
#include "cuicui/dsp/room.hpp"
#include "cuicui/model/whistle_voice.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cuicui::control {

/// Everything the whistle path reads per block (plugin parameters, WP-R1 §3.4).
struct WhistleSettings {
    PlayContext play{};
    bool harmonics = true; ///< the bird's own 2nd/3rd harmonics (D-022)
    bool room = true;      ///< the prototype's small room
};

/// A logged syllable start (tests, offline renders). `sample` is absolute since reset().
struct SyllableLogEntry {
    std::int64_t sample = 0;
    SyllableRequest request{};
};

/// The whistle path of WP-R1, modular as D-021 asks:
///   input events → playing mode (ModeAB by default) → syllable bank → placement → whistle voices → room.
/// Voices are a pool: a request starts a free WhistleVoice at its exact sample; when the pool is nearly
/// full the oldest voice fades out over 5 ms. Mono-compatible stereo out (equal-power pan per key).
/// prepare() allocates; reset(), process() and releaseAll() are real-time safe.
class SyllableEngine {
public:
    static constexpr int kMaxVoices = 32;
    static constexpr int kSoftVoiceLimit = 28; ///< above this, the oldest voice is faded out
    static constexpr int kChunk = 256;         ///< internal block (bounds the requests per call)
    static constexpr int kMaxRequests = 64;
    static constexpr double kStealFadeMs = 5.0;

    void prepare(double sampleRate) {
        sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        modeAB_.prepare(sr_);
        for (auto& v : voices_) {
            v.prepare(sr_);
        }
        room_.prepare(sr_);
        stealFade_ = std::max(1, static_cast<int>(std::lround(kStealFadeMs * 0.001 * sr_)));
        reset(1);
    }

    void reset(std::uint64_t seed) noexcept {
        if (mode_ != nullptr) {
            mode_->reset(seed);
        }
        for (auto& v : voices_) {
            v.prepare(sr_);
        }
        room_.reset();
        samples_ = 0;
        voiceAge_ = 1;
        logCount_ = 0;
        steals_ = 0;
        dropped_ = 0;
    }

    void setBank(const SyllableBank* bank) noexcept {
        bank_ = bank;
        if (mode_ != nullptr) {
            mode_->setBank(bank);
        }
        for (auto& v : voices_) { // voices point into the old bank: silence them
            v.prepare(sr_);
        }
    }

    /// Replace the playing mode (nullptr = the built-in ModeAB). Call off the audio thread.
    void setMode(PlayingMode* mode) noexcept {
        mode_ = mode != nullptr ? mode : &modeAB_;
        mode_->prepare(sr_);
        mode_->setBank(bank_);
    }

    [[nodiscard]] PlayingMode& mode() noexcept { return *mode_; }
    [[nodiscard]] ModeAB& modeAB() noexcept { return modeAB_; }

    /// Optional log of every syllable start into a caller-owned buffer (nullptr = off).
    void setLog(SyllableLogEntry* buffer, int capacity) noexcept {
        log_ = buffer;
        logCapacity_ = buffer != nullptr ? std::max(0, capacity) : 0;
        logCount_ = 0;
    }
    [[nodiscard]] int logCount() const noexcept { return logCount_; }

    /// Render n samples into left/right (overwritten). Events are sorted by offset inside [0, n).
    void process(const PlayEvent* events, int numEvents, int n, const WhistleSettings& s, float* left,
                 float* right) noexcept {
        std::fill(left, left + n, 0.0f);
        std::fill(right, right + n, 0.0f);
        model::WhistleVoice::Colour colour;
        colour.harmonics = s.harmonics;
        int ei = 0;
        for (int pos = 0; pos < n; pos += kChunk) {
            const int len = std::min(kChunk, n - pos);
            // events of this chunk, re-based to the chunk start
            int ne = 0;
            while (ei + ne < numEvents && events[ei + ne].offset < pos + len) {
                const auto& src = events[ei + ne];
                auto& dst = chunkEvents_[static_cast<std::size_t>(ne)];
                dst = src;
                dst.offset = std::max(0, src.offset - pos);
                if (src.type == PlayEvent::Type::AllSoundOff) {
                    for (auto& v : voices_) {
                        v.fadeOut(stealFade_);
                    }
                }
                ++ne;
                if (ne == kMaxChunkEvents) {
                    break;
                }
            }
            ei += ne;
            int nreq = 0;
            if (mode_ != nullptr && bank_ != nullptr && !bank_->empty()) {
                nreq = mode_->process(chunkEvents_.data(), ne, len, s.play, requests_.data(), kMaxRequests);
            }
            for (int r = 0; r < nreq; ++r) {
                start(requests_[static_cast<std::size_t>(r)], colour);
            }
            // Sum the voices oldest first: which slot a voice got depends on block boundaries, the age does
            // not, so the float sums (and the output) are identical for any host block size.
            int nv = 0;
            for (int i = 0; i < kMaxVoices; ++i) {
                if (voices_[static_cast<std::size_t>(i)].active()) {
                    int k = nv++;
                    while (k > 0 && voices_[static_cast<std::size_t>(order_[static_cast<std::size_t>(k - 1)])].age() >
                                        voices_[static_cast<std::size_t>(i)].age()) {
                        order_[static_cast<std::size_t>(k)] = order_[static_cast<std::size_t>(k - 1)];
                        --k;
                    }
                    order_[static_cast<std::size_t>(k)] = i;
                }
            }
            for (int k = 0; k < nv; ++k) {
                voices_[static_cast<std::size_t>(order_[static_cast<std::size_t>(k)])].render(left + pos, right + pos, len);
            }
            samples_ += len;
        }
        room_.process(left, right, left, right, n, s.room);
    }

    /// Every key stops scheduling; sounding syllables finish.
    void releaseAll() noexcept {
        if (mode_ != nullptr) {
            mode_->releaseAll();
        }
    }

    /// Something can still be heard (a key sings, a voice sounds, or the room rings).
    [[nodiscard]] bool busy() const noexcept {
        return activeVoices() > 0 || room_.ringing() || (mode_ != nullptr && mode_->activeSingers() > 0);
    }

    [[nodiscard]] int activeVoices() const noexcept {
        int n = 0;
        for (const auto& v : voices_) {
            n += v.active() ? 1 : 0;
        }
        return n;
    }

    [[nodiscard]] std::int64_t samplesProcessed() const noexcept { return samples_; }
    [[nodiscard]] int steals() const noexcept { return steals_; }
    [[nodiscard]] int droppedRequests() const noexcept { return dropped_; }
    [[nodiscard]] const dsp::Room& room() const noexcept { return room_; }

private:
    static constexpr int kMaxChunkEvents = 128;

    void start(const SyllableRequest& r, const model::WhistleVoice::Colour& colour) noexcept {
        if (log_ != nullptr && logCount_ < logCapacity_) {
            log_[logCount_++] = SyllableLogEntry{samples_ + r.offset, r};
        }
        if (activeVoices() >= kSoftVoiceLimit) { // nearly full: fade the oldest out (5 ms)
            model::WhistleVoice* oldest = nullptr;
            for (auto& v : voices_) {
                if (v.active() && !v.fading() && (oldest == nullptr || v.age() < oldest->age())) {
                    oldest = &v;
                }
            }
            if (oldest != nullptr) {
                oldest->fadeOut(stealFade_);
                ++steals_;
            }
        }
        model::WhistleVoice* slot = nullptr;
        for (auto& v : voices_) {
            if (!v.active()) {
                slot = &v;
                break;
            }
        }
        if (slot == nullptr) { // all busy (pathological): take the oldest, hard
            for (auto& v : voices_) {
                if (slot == nullptr || v.age() < slot->age()) {
                    slot = &v;
                }
            }
            ++dropped_;
        }
        slot->start(*bank_, r.syllable, r.shiftCents, r.gain, r.pan, r.offset, colour, voiceAge_++);
    }

    ModeAB modeAB_;
    PlayingMode* mode_ = &modeAB_;
    const SyllableBank* bank_ = nullptr;
    std::array<model::WhistleVoice, kMaxVoices> voices_{};
    std::array<int, kMaxVoices> order_{};
    std::array<PlayEvent, kMaxChunkEvents> chunkEvents_{};
    std::array<SyllableRequest, kMaxRequests> requests_{};
    dsp::Room room_;
    SyllableLogEntry* log_ = nullptr;
    double sr_ = 48000.0;
    std::int64_t samples_ = 0;
    std::uint32_t voiceAge_ = 1;
    int logCapacity_ = 0;
    int logCount_ = 0;
    int stealFade_ = 240;
    int steals_ = 0;
    int dropped_ = 0;
};

} // namespace cuicui::control
