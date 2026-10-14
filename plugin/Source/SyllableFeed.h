#pragma once

#include "cuicui/control/syllable_engine.hpp"

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstdint>

/// WP-R4: what the whistle voice sings, from the audio thread to the editor ("now singing" display).
/// - A single-producer / single-consumer FIFO of syllable starts: lock-free, no allocation. When the
///   editor is closed or slow, the FIFO fills and new starts are dropped (counted), never waited for.
/// - The whistle clock (samples since prepareToPlay) and a few live values, as relaxed atomics.
class SyllableFeed {
public:
    static constexpr int kCapacity = 512;

    struct Event {
        std::int64_t sample = 0; ///< start, absolute on the whistle clock (before the plugin latency)
        int syllable = 0;        ///< bank index
        int note = -1;           ///< MIDI key that asked
        int singer = 0;          ///< slot of the playing mode (stable while the key sings)
        float shiftCents = 0.0f;
        float targetCents = 0.0f; ///< the key's target pitch (MIDI cents)
        float gain = 0.0f;
        bool first = false;
        // WP-R3: a varied rendition (the editor rebuilds the same curve, control::makeVariant)
        float shape = 0.0f;
        double lengthScale = 1.0;
        double rangeScale = 1.0;
        std::uint64_t variantSeed = 0;
    };

    /// Audio thread.
    void push(const cuicui::control::SyllableLogEntry& e) noexcept {
        const auto scope = fifo_.write(1);
        Event* slot = scope.blockSize1 > 0   ? &buf_[static_cast<std::size_t>(scope.startIndex1)]
                      : scope.blockSize2 > 0 ? &buf_[static_cast<std::size_t>(scope.startIndex2)]
                                             : nullptr;
        if (slot == nullptr) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const auto& r = e.request;
        slot->sample = e.sample;
        slot->syllable = r.syllable;
        slot->note = r.note;
        slot->singer = r.singer;
        slot->shiftCents = static_cast<float>(r.shiftCents);
        slot->targetCents = static_cast<float>(r.targetCents);
        slot->gain = r.gain;
        slot->first = r.first;
        slot->shape = static_cast<float>(r.shape);
        slot->lengthScale = r.lengthScale;
        slot->rangeScale = r.rangeScale;
        slot->variantSeed = r.variantSeed;
    }

    /// Audio thread, once per block: the whistle clock and live controller values.
    void publish(std::int64_t clock, double sampleRate, float modWheel, bool sustain) noexcept {
        clock_.store(clock, std::memory_order_relaxed);
        clockWallMs_.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
        sampleRate_.store(sampleRate, std::memory_order_relaxed);
        modWheel_.store(modWheel, std::memory_order_relaxed);
        sustain_.store(sustain, std::memory_order_relaxed);
    }

    /// Audio thread: count note-ons that came from outside (a MIDI keyboard or the host).
    void countExternalNotes(int n) noexcept {
        if (n > 0) {
            externalNotes_.fetch_add(static_cast<std::uint32_t>(n), std::memory_order_relaxed);
        }
    }

    /// Message thread: read up to `max` events (oldest first). Returns how many were read.
    int pop(Event* out, int max) noexcept {
        const auto scope = fifo_.read(std::min(max, fifo_.getNumReady()));
        int k = 0;
        for (int i = 0; i < scope.blockSize1; ++i) {
            out[k++] = buf_[static_cast<std::size_t>(scope.startIndex1 + i)];
        }
        for (int i = 0; i < scope.blockSize2; ++i) {
            out[k++] = buf_[static_cast<std::size_t>(scope.startIndex2 + i)];
        }
        return k;
    }

    /// prepareToPlay: the whistle clock restarts. The FIFO itself is not touched (the editor may be
    /// reading it); readers drop events that fall outside their time window, and watch epoch().
    void reset() noexcept {
        clock_.store(0, std::memory_order_relaxed);
        clockWallMs_.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
        epoch_.fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] std::int64_t clock() const noexcept { return clock_.load(std::memory_order_relaxed); }
    [[nodiscard]] double clockWallMs() const noexcept { return clockWallMs_.load(std::memory_order_relaxed); }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_.load(std::memory_order_relaxed); }
    [[nodiscard]] float modWheel() const noexcept { return modWheel_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool sustain() const noexcept { return sustain_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint32_t externalNotes() const noexcept {
        return externalNotes_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] int dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    /// Changes when the clock restarts (prepareToPlay), so readers can forget old events.
    [[nodiscard]] std::uint32_t epoch() const noexcept { return epoch_.load(std::memory_order_relaxed); }

private:
    juce::AbstractFifo fifo_{kCapacity};
    std::array<Event, kCapacity> buf_{};
    std::atomic<std::int64_t> clock_{0};
    std::atomic<double> clockWallMs_{0.0};
    std::atomic<double> sampleRate_{48000.0};
    std::atomic<float> modWheel_{0.0f};
    std::atomic<bool> sustain_{false};
    std::atomic<std::uint32_t> externalNotes_{0};
    std::atomic<std::uint32_t> epoch_{0};
    std::atomic<int> dropped_{0};
};
