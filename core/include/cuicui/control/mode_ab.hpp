#pragma once

#include "cuicui/control/phrase_order.hpp"
#include "cuicui/control/placement.hpp"
#include "cuicui/control/playing_mode.hpp"
#include "cuicui/control/syllable_bank.hpp"
#include "cuicui/dsp/rng.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace cuicui::control {

/// Playing modes A + B as one behaviour (D-021), ported from the PROTO1 prototype (`Singer`, `Performer`):
/// - tap a key → one real syllable, starting on the note-on sample;
/// - hold → the bird continues (real phrases or shuffled), each next syllable scheduled sample-accurately;
/// - release → nothing new starts; the syllable already sounding finishes (in the voices).
/// A new press of the same key releases the old singer; over the polyphony limit the oldest held key is
/// released. CC64 holds released keys; the mod wheel adds to snap.
///
/// The release alarm (D-030, `PlayContext::releaseAlarm`): the bird's long rising call is kept for the end
/// of a long key press. Taps and phrases never use it. When a key that sang at least
/// `releaseAlarmHoldS` is released (or its pedal is lifted), the bird ends with one: after the syllable
/// still sounding and the bird's own lead-in silence (`SyllableBank::releaseGapMs`), or at once if it was
/// between phrases. Its pitch is placed like any following syllable (snap + mod wheel), with the key's
/// loudness and place. Pressing the same key again before it starts cancels it; keys released by the
/// polyphony limit, all-notes-off or a voice switch never call.
///
/// Randomness: one master stream seeded by reset(); each key press draws a 64-bit seed from it, and the
/// singer then uses only its own stream (pan, start, order, which alarm). Results therefore do not depend
/// on the host block size. Real-time safe: no allocation after construction.
class ModeAB final : public PlayingMode {
public:
    static constexpr int kMaxSingers = 16;

    void prepare(double sampleRate) override { sr_ = sampleRate > 0.0 ? sampleRate : 48000.0; }

    void reset(std::uint64_t seed) noexcept override {
        master_.setSeed(seed == 0 ? 1ULL : seed);
        for (auto& s : singers_) {
            s = Singer{};
        }
        for (auto& f : finals_) {
            f = Final{};
        }
        phrasePointer_ = 0;
        sustain_ = false;
        modWheel_ = 0.0;
        ageCounter_ = 1;
        serial_ = 0;
        now_ = 0;
    }

    void setBank(const SyllableBank* bank) noexcept override {
        bank_ = bank;
        releaseAll();
        phrasePointer_ = 0;
    }

    int process(const PlayEvent* events, int numEvents, int numSamples, const PlayContext& ctx,
                SyllableRequest* out, int maxOut) noexcept override {
        Emit em{out, maxOut, 0, now_};
        const std::int64_t blockEnd = now_ + std::max(0, numSamples);
        const int lastOffset = std::max(0, numSamples - 1);
        int ei = 0;
        for (;;) {
            int si = -1;
            std::int64_t ts = std::numeric_limits<std::int64_t>::max();
            for (int i = 0; i < kMaxSingers; ++i) {
                const auto& s = singers_[static_cast<std::size_t>(i)];
                if (s.active && s.hasNext && s.nextSample < blockEnd && s.nextSample < ts) {
                    ts = s.nextSample;
                    si = i;
                }
            }
            int fi = -1;
            for (int i = 0; i < kMaxSingers; ++i) {
                const auto& f = finals_[static_cast<std::size_t>(i)];
                if (f.pending && f.at < blockEnd && f.at < ts) {
                    ts = f.at;
                    fi = i;
                }
            }
            if (ei < numEvents) {
                const auto& ev = events[ei];
                const std::int64_t te = now_ + std::clamp(ev.offset, 0, lastOffset);
                if (te <= ts) { // input first on ties: a release at the same sample cancels the start
                    handle(ev, te, ctx, em);
                    ++ei;
                    continue;
                }
            }
            if (fi >= 0) {
                emitFinal(fi, ts, ctx, em);
                continue;
            }
            if (si < 0) {
                break;
            }
            emit(si, ts, ctx, em, false);
        }
        now_ = blockEnd;
        return em.count;
    }

    void releaseAll() noexcept override {
        for (auto& s : singers_) {
            release(s);
        }
        for (auto& f : finals_) { // a voice switch, a new bank, all-notes-off: no closing call
            f.pending = false;
        }
    }

    [[nodiscard]] int activeSingers() const noexcept override {
        int n = 0;
        for (const auto& s : singers_) {
            n += s.active ? 1 : 0;
        }
        return n;
    }

    [[nodiscard]] bool sustainDown() const noexcept { return sustain_; }
    [[nodiscard]] double modWheel() const noexcept { return modWheel_; }
    /// Absolute sample position of the next block (samples processed since reset).
    [[nodiscard]] std::int64_t now() const noexcept { return now_; }
    /// Release alarms scheduled and not started yet (tests).
    [[nodiscard]] int pendingReleaseAlarms() const noexcept {
        int n = 0;
        for (const auto& f : finals_) {
            n += f.pending ? 1 : 0;
        }
        return n;
    }

private:
    struct Singer {
        bool active = false;    ///< still scheduling (key held, or released while the pedal is down)
        bool sustained = false; ///< key released while the pedal was down
        int note = -1;
        std::uint32_t age = 0;
        std::int64_t t0 = 0; ///< note-on sample (absolute)
        double target = 0.0;
        float gain = 0.3f;
        float pan = 0.0f;
        dsp::Rng rng{1};
        PhraseOrder order{};
        bool hasNext = false;
        int nextSyl = 0;
        double nextStartS = 0.0;
        std::int64_t nextSample = 0;
        std::int64_t lastEnd = 0; ///< where its last syllable ends (absolute sample)
    };

    /// A release alarm waiting for its sample (D-030).
    struct Final {
        bool pending = false;
        std::int64_t at = 0;
        int syllable = 0;
        int note = -1;
        int singer = 0;
        float gain = 0.3f;
        float pan = 0.0f;
        double target = 0.0;
    };

    struct Emit {
        SyllableRequest* out;
        int maxOut;
        int count;
        std::int64_t blockStart;
    };

    static void release(Singer& s) noexcept {
        s.active = false;
        s.sustained = false;
        s.hasNext = false;
    }

    void handle(const PlayEvent& ev, std::int64_t te, const PlayContext& ctx, Emit& em) noexcept {
        switch (ev.type) {
        case PlayEvent::Type::NoteOn:
            noteOn(ev.note, static_cast<double>(ev.value), te, ctx, em);
            break;
        case PlayEvent::Type::NoteOff:
            for (int i = 0; i < kMaxSingers; ++i) {
                auto& s = singers_[static_cast<std::size_t>(i)];
                if (s.active && !s.sustained && s.note == ev.note) {
                    if (sustain_) {
                        s.sustained = true;
                    } else {
                        scheduleFinal(i, te, ctx);
                        release(s);
                    }
                }
            }
            break;
        case PlayEvent::Type::Sustain:
            sustain_ = ev.value >= 0.5f;
            if (!sustain_) {
                for (int i = 0; i < kMaxSingers; ++i) {
                    auto& s = singers_[static_cast<std::size_t>(i)];
                    if (s.active && s.sustained) {
                        scheduleFinal(i, te, ctx);
                        release(s);
                    }
                }
            }
            break;
        case PlayEvent::Type::ModWheel:
            modWheel_ = std::clamp(static_cast<double>(ev.value), 0.0, 1.0);
            break;
        case PlayEvent::Type::AllNotesOff:
        case PlayEvent::Type::AllSoundOff:
            releaseAll();
            sustain_ = false;
            break;
        }
    }

    void noteOn(int note, double velocity, std::int64_t te, const PlayContext& ctx, Emit& em) noexcept {
        if (bank_ == nullptr || bank_->empty()) {
            return;
        }
        for (auto& s : singers_) { // the same key again: the old singer finishes its syllable
            if (s.active && s.note == note) {
                release(s);
            }
        }
        for (auto& f : finals_) { // ... and the call it was about to end with is not sung
            if (f.pending && f.note == note) {
                f.pending = false;
            }
        }
        const int poly = std::clamp(ctx.polyphony, 1, kMaxSingers);
        while (activeSingers() >= poly) { // over the limit: release the oldest held key (no closing call)
            Singer* oldest = nullptr;
            for (auto& s : singers_) {
                if (s.active && (oldest == nullptr || s.age < oldest->age)) {
                    oldest = &s;
                }
            }
            release(*oldest);
        }
        int si = 0;
        while (si < kMaxSingers && singers_[static_cast<std::size_t>(si)].active) {
            ++si;
        }
        auto& s = singers_[static_cast<std::size_t>(si)];
        s.active = true;
        s.sustained = false;
        s.note = note;
        s.age = ageCounter_++;
        s.t0 = te;
        s.lastEnd = te;
        const std::uint64_t hi = master_.next();
        const std::uint64_t lo = master_.next();
        s.rng.setSeed((hi << 32) | lo);
        const double u = uniform01(s.rng); // drawn even in mono, so the stream does not depend on polyphony
        s.pan = poly > 1 ? static_cast<float>((2.0 * u - 1.0) * std::clamp(ctx.spread, 0.0, 1.0)) : 0.0f;
        s.gain = static_cast<float>(velocityGain(velocity));
        s.target = placementTarget(note, bank_->centreCents(ctx.rule), ctx.mapping, ctx.sizeSt);
        const OrderStep first = s.order.begin(*bank_, ctx.order, s.rng, phrasePointer_, ctx.releaseAlarm);
        s.nextSyl = first.syllable;
        s.nextStartS = 0.0;
        s.hasNext = true;
        s.nextSample = te;
        emit(si, te, ctx, em, true);
    }

    void emit(int si, std::int64_t ts, const PlayContext& ctx, Emit& em, bool first) noexcept {
        auto& s = singers_[static_cast<std::size_t>(si)];
        const int syl = s.nextSyl;
        const double k = (first && ctx.firstOnNote) ? 1.0 : std::clamp(ctx.snap + modWheel_, 0.0, 1.0);
        if (em.count < em.maxOut) {
            SyllableRequest& r = em.out[em.count++];
            r.syllable = syl;
            r.offset = static_cast<int>(std::max<std::int64_t>(0, ts - em.blockStart));
            r.shiftCents = placementShift(bank_->anchorCents(syl, ctx.rule), s.target, bank_->centreCents(ctx.rule), k);
            r.gain = s.gain;
            r.pan = s.pan;
            r.singer = si;
            r.serial = serial_;
            r.first = first;
            r.note = s.note;
            r.targetCents = s.target;
            r.releaseAlarm = false;
        }
        ++serial_;
        s.lastEnd = ts + std::llround(static_cast<double>(bank_->syllable(syl).lengthMs) * 0.001 * sr_);
        const OrderStep st = s.order.next(*bank_, s.nextStartS, ctx.pauseS, s.rng, phrasePointer_);
        s.nextSyl = st.syllable;
        s.nextStartS = st.startS;
        s.nextSample = std::max(s.t0 + static_cast<std::int64_t>(std::llround(st.startS * sr_)), ts + 1);
        s.hasNext = true;
    }

    /// A released key that sang long enough ends with the bird's long rising call (D-030).
    void scheduleFinal(int si, std::int64_t te, const PlayContext& ctx) noexcept {
        auto& s = singers_[static_cast<std::size_t>(si)];
        if (!ctx.releaseAlarm || bank_ == nullptr || bank_->releaseSyllables().empty()) {
            return;
        }
        const double heldS = static_cast<double>(te - s.t0) / sr_;
        if (heldS + 1e-9 < ctx.releaseAlarmHoldS) {
            return; // a short press: never the call
        }
        Final* f = nullptr;
        for (auto& c : finals_) {
            if (!c.pending) {
                f = &c;
                break;
            }
        }
        if (f == nullptr) {
            return;
        }
        const auto& calls = bank_->releaseSyllables();
        const auto lead = static_cast<std::int64_t>(std::llround(bank_->releaseGapMs() * 0.001 * sr_));
        f->pending = true;
        f->at = std::max(te, s.lastEnd + lead);
        f->syllable = calls[static_cast<std::size_t>(uniformIndex(s.rng, static_cast<int>(calls.size())))];
        f->note = s.note;
        f->singer = si;
        f->gain = s.gain;
        f->pan = s.pan;
        f->target = s.target;
    }

    void emitFinal(int fi, std::int64_t ts, const PlayContext& ctx, Emit& em) noexcept {
        auto& f = finals_[static_cast<std::size_t>(fi)];
        f.pending = false;
        const double k = std::clamp(ctx.snap + modWheel_, 0.0, 1.0); // placed like a following syllable
        if (em.count < em.maxOut) {
            SyllableRequest& r = em.out[em.count++];
            r.syllable = f.syllable;
            r.offset = static_cast<int>(std::max<std::int64_t>(0, ts - em.blockStart));
            r.shiftCents =
                placementShift(bank_->anchorCents(f.syllable, ctx.rule), f.target, bank_->centreCents(ctx.rule), k);
            r.gain = f.gain;
            r.pan = f.pan;
            r.singer = f.singer;
            r.serial = serial_;
            r.first = false;
            r.note = f.note;
            r.targetCents = f.target;
            r.releaseAlarm = true;
        }
        ++serial_;
    }

    std::array<Singer, kMaxSingers> singers_{};
    std::array<Final, kMaxSingers> finals_{};
    const SyllableBank* bank_ = nullptr;
    dsp::Rng master_{1};
    double sr_ = 48000.0;
    double modWheel_ = 0.0;
    std::int64_t now_ = 0;
    std::uint32_t ageCounter_ = 1;
    std::uint32_t serial_ = 0;
    int phrasePointer_ = 0;
    bool sustain_ = false;
};

} // namespace cuicui::control
