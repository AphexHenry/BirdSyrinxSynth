#pragma once

#include "cuicui/control/archetype_grammar.hpp"
#include "cuicui/control/gesture.hpp"
#include "cuicui/control/gesture_source.hpp"
#include "cuicui/control/scales.hpp"
#include "cuicui/dsp/rng.hpp"
#include "cuicui/params.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace cuicui::control {

/// Macros and host tempo consumed by `GestureEngine` (updated each control tick). WP-C5.
struct GestureEngineContext {
    double motion = 0.3;
    double range = 1.0;
    double speed = 1.0;
    double variation = 1.0;
    double density = 0.5;
    double snap = 0.0;
    double tone = 0.6;
    int scale = 0;
    int root = 0;
    bool freeze = false;
    bool tempoSync = false;
    int syncDivision = 2; // 1/16
    bool tempoValid = false;
    bool hostPlaying = false;
    double bpm = 120.0;
    double ppq = 0.0;
};

struct SyllableTrace {
    int nGestures = 0;
    int types[4]{-1, -1, -1, -1};
    double params[4][8]{};
    double durationMs = 0.0;
    double onsetSec = 0.0;
    bool fromMotif = false;
};

[[nodiscard]] inline std::uint32_t hashPhraseSeed(std::uint32_t perfSeed, int note,
                                                  std::uint32_t counter) noexcept {
    std::uint64_t s = (static_cast<std::uint64_t>(perfSeed) << 32) ^
                      (static_cast<std::uint64_t>(static_cast<std::uint16_t>(note)) << 16) ^
                      static_cast<std::uint64_t>(counter);
    const std::uint64_t z = dsp::splitmix64(s);
    return static_cast<std::uint32_t>(z == 0 ? 1u : z);
}

/// Per-voice generative source. All storage is fixed-size; `next` allocates nothing. WP-C5 §4.
class GestureEngine final : public GestureSource {
public:
    static constexpr int kMaxMotif = 16;
    static constexpr int kMaxGest = 4;
    static constexpr int kTraceCap = 64;

    enum class State { Idle, Syllable, Gap };

    void prepare() noexcept { initGrammarMovement(); }

    void setContext(const GestureEngineContext& c) noexcept { ctx_ = c; }

    [[nodiscard]] const GestureEngineContext& context() const noexcept { return ctx_; }

    void noteOn(double anchorCents, int velocity, std::uint32_t seed) noexcept override {
        (void)velocity;
        anchor_ = anchorCents;
        phraseSeed_ = seed == 0 ? 1u : seed;
        rng_.setSeed(dsp::deriveStreamSeed(phraseSeed_, dsp::kStreamGesture));
        genCounter_ = 0;
        finishing_ = false;
        freezeLatched_ = ctx_.freeze;
        motifLen_ = 0;
        motifFill_ = 0;
        motifPos_ = 0;
        motifTarget_ = 2 + static_cast<int>(rng_.uniform() * 3.999f); // 2..5
        motifTarget_ = std::clamp(motifTarget_, 2, 5);
        lastType_[0] = -1;
        lastType_[1] = -1;
        tGest_ = 0.0;
        gapLeft_ = 0.0;
        timeSec_ = 0.0;
        sylCount_ = 0;
        traceN_ = 0;
        curN_ = 0;
        curI_ = 0;
        state_ = State::Gap;
        gapLeft_ = 0.0;
        maybeStartIntro_ = rng_.uniform() < 0.6f;
        startNextSyllable();
    }

    void noteOff() noexcept override { finishing_ = true; }

    GestureSample next(double dtSeconds) noexcept override {
        const double dt = dtSeconds > 0.0 ? dtSeconds : 0.0;
        timeSec_ += dt;
        if (ctx_.tempoValid && ctx_.hostPlaying && ctx_.bpm > 0.0) {
            ppq_ = ctx_.ppq;
        } else {
            ppq_ += dt * (ctx_.bpm > 0.0 ? ctx_.bpm : 120.0) / 60.0;
        }

        if (state_ == State::Idle) {
            return {};
        }
        if (state_ == State::Gap) {
            if (finishing_) {
                state_ = State::Idle;
                return {};
            }
            gapLeft_ -= dt;
            if (gapLeft_ <= 0.0) {
                startNextSyllable();
            } else {
                return {};
            }
        }
        if (state_ != State::Syllable || curI_ >= curN_ || curN_ <= 0) {
            return {};
        }

        auto& g = cur_.g[static_cast<std::size_t>(curI_)];
        g.inst.tStart = 0.0;
        GestureSample s = evaluateInstance(g.inst, tGest_);
        tGest_ += dt;
        const double dur = std::max(1.0e-4, g.inst.durationMs * 0.001);
        if (tGest_ >= dur) {
            ++curI_;
            tGest_ = 0.0;
            if (curI_ >= curN_ || finishing_) {
                if (finishing_) {
                    state_ = State::Idle;
                } else {
                    beginGap();
                }
            }
        }
        return s;
    }

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] int motifLength() const noexcept { return motifLen_; }
    [[nodiscard]] int syllableCount() const noexcept { return sylCount_; }
    [[nodiscard]] int traceCount() const noexcept { return traceN_; }
    [[nodiscard]] const SyllableTrace& trace(int i) const noexcept {
        return trace_[static_cast<std::size_t>(std::clamp(i, 0, kTraceCap - 1))];
    }
    [[nodiscard]] double timeSec() const noexcept { return timeSec_; }
    [[nodiscard]] int currentType() const noexcept {
        if (state_ != State::Syllable || curI_ >= curN_) {
            return -1;
        }
        return cur_.g[static_cast<std::size_t>(curI_)].type;
    }

    /// Generate one syllable into an unused scratch (benchmark / tests). Not used in `next`.
    void benchmarkGenerate() noexcept {
        SylRec tmp{};
        composeSyllable(tmp, false);
        generateCurves(tmp);
    }

private:
    struct GestRec {
        int type = 0;
        int nParams = 0;
        double params[8]{};
        GestureInstance inst{};
    };
    struct SylRec {
        int n = 0;
        GestRec g[kMaxGest]{};
    };

    [[nodiscard]] double truncGauss() noexcept {
        for (int i = 0; i < 24; ++i) {
            const double x = static_cast<double>(rng_.gaussian());
            if (x >= -2.5 && x <= 2.5) {
                return x;
            }
        }
        return std::clamp(static_cast<double>(rng_.gaussian()), -2.5, 2.5);
    }

    [[nodiscard]] int pickWeighted(const double* w, int n) noexcept {
        double s = 0.0;
        for (int i = 0; i < n; ++i) {
            s += std::max(0.0, w[i]);
        }
        if (!(s > 0.0)) {
            return 0;
        }
        double u = static_cast<double>(rng_.uniform()) * s;
        for (int i = 0; i < n; ++i) {
            u -= std::max(0.0, w[i]);
            if (u <= 0.0) {
                return i;
            }
        }
        return n - 1;
    }

    void fillParams(int type, double* out, int& nOut, bool fromStored, const double* stored,
                    double vScale) noexcept {
        const auto& info = kArchetypes[static_cast<std::size_t>(std::clamp(type, 0, kNumArchetypes - 1))];
        nOut = info.nParams;
        const double range = std::clamp(ctx_.range, 0.0, 2.0);
        const double v = std::clamp(ctx_.variation, 0.0, 2.0) * vScale;
        for (int i = 0; i < info.nParams; ++i) {
            const auto& ps = info.params[i];
            double base = fromStored && stored != nullptr ? stored[i] : ps.def;
            if (!fromStored) {
                if (paramIsDuration(ps.name)) {
                    base = ps.min + kGenDurBlend * (ps.def - ps.min);
                }
                if (paramIsPitchDepth(ps.name)) {
                    base = ps.min + kGenDepthBlend * (ps.def - ps.min);
                }
            }
            const double xi = v > 0.0 ? truncGauss() : 0.0;
            double th = base + v * xi * (ps.max - ps.min) / 6.0;
            if (!fromStored) {
                if (paramIsPitchDepth(ps.name)) {
                    th *= range;
                }
            }
            out[i] = clampSpec(ps, th);
        }
    }

    [[nodiscard]] int pickType(double* w) noexcept {
        if (ctx_.variation >= 0.3 && lastType_[0] == lastType_[1] && lastType_[0] >= 0) {
            w[lastType_[0]] = 0.0;
        }
        int t = pickWeighted(w, kNumArchetypes);
        if (kGrammarBaseWeight[static_cast<std::size_t>(t)] <= 0.0) {
            t = kArchWhistle;
        }
        lastType_[1] = lastType_[0];
        lastType_[0] = t;
        return t;
    }

    void composeSyllable(SylRec& syl, bool forceIntro) noexcept {
        syl = {};
        if (forceIntro) {
            const int n = 1 + pickWeighted(kIntroN_, 3);
            syl.n = n;
            for (int i = 0; i < n; ++i) {
                auto& g = syl.g[static_cast<std::size_t>(i)];
                g.type = kArchIntro;
                fillParams(kArchIntro, g.params, g.nParams, false, nullptr, 1.0);
                lastType_[1] = lastType_[0];
                lastType_[0] = kArchIntro;
            }
            return;
        }
        double kw[4]{};
        syllableCountWeights(ctx_.motion, kw);
        const int k = 1 + pickWeighted(kw, 4);
        syl.n = k;
        double tw[kNumArchetypes]{};
        gestureTypeWeights(ctx_.motion, ctx_.tone, tw);
        for (int i = 0; i < k; ++i) {
            auto& g = syl.g[static_cast<std::size_t>(i)];
            g.type = pickType(tw);
            fillParams(g.type, g.params, g.nParams, false, nullptr, 1.0);
        }
    }

    /// Replace slow cosine attacks (10 % of a long whistle) with ~3 ms / ~8 ms edges.
    /// Trill/buzz/chip already pulse; leave them.
    void sharpenAmp(GestRec& g) noexcept {
        if (g.type == kArchTrill || g.type == kArchBuzz || g.type == kArchChip) {
            return;
        }
        const double durS = std::max(1.0e-4, g.inst.durationMs * 0.001);
        const double a = std::min(0.28, 0.003 / durS);
        const double r = std::min(0.38, 0.008 / durS);
        auto& amp = g.inst.curves.ampDb;
        for (int i = 0; i < GestureCurves::kN; ++i) {
            const double u = static_cast<double>(i) / static_cast<double>(GestureCurves::kN - 1);
            amp[static_cast<std::size_t>(i)] = envDb(u, a, r);
        }
    }

    void generateCurves(SylRec& syl) noexcept {
        const double range = std::clamp(ctx_.range, 0.0, 2.0);
        const double sigma = kAnchorSigmaCents * range;
        double off = truncGauss() * sigma;
        double absP = anchor_ + off;
        if (ctx_.snap > 0.5) {
            absP = quantizeCents(absP, ctx_.scale, ctx_.root);
            off = absP - anchor_;
        }
        for (int i = 0; i < syl.n; ++i) {
            auto& g = syl.g[static_cast<std::size_t>(i)];
            const std::uint64_t gseed =
                dsp::deriveStreamSeed(phraseSeed_, dsp::kStreamGesture + 8 + genCounter_++);
            g.inst = generateArchetypeIndexed(g.type, g.params, g.nParams, gseed);
            g.inst.tStart = 0.0;
            g.inst.anchorCents = anchor_ + off;
            sharpenAmp(g);
            const double speed = std::clamp(ctx_.speed, 0.25, 4.0);
            g.inst.durationMs = std::max(8.0, g.inst.durationMs / speed);
        }
    }

    void replayMotifSyllable() noexcept {
        if (motifLen_ <= 0) {
            composeSyllable(cur_, false);
            generateCurves(cur_);
            return;
        }
        const SylRec& src = motif_[static_cast<std::size_t>(motifPos_)];
        motifPos_ = (motifPos_ + 1) % motifLen_;
        const bool freeze = ctx_.freeze || freezeLatched_;
        const double vScale = freeze ? 0.2 : 1.0;
        const bool exact = ctx_.variation <= 0.0 && !freeze;
        cur_ = src;
        if (exact) {
            return;
        }
        for (int i = 0; i < cur_.n; ++i) {
            auto& g = cur_.g[static_cast<std::size_t>(i)];
            fillParams(g.type, g.params, g.nParams, freeze, src.g[static_cast<std::size_t>(i)].params, vScale);
            lastType_[1] = lastType_[0];
            lastType_[0] = g.type;
        }
        generateCurves(cur_);
    }

    void startNextSyllable() noexcept {
        bool fromMotif = false;
        const bool freeze = ctx_.freeze || freezeLatched_;
        if (freeze && motifLen_ > 0) {
            freezeLatched_ = true;
            fromMotif = true;
            replayMotifSyllable();
        } else if (motifLen_ > 0 && motifFill_ >= motifTarget_) {
            const double pRep = 1.0 - 0.4 * std::clamp(ctx_.variation, 0.0, 2.0);
            if (static_cast<double>(rng_.uniform()) < pRep) {
                fromMotif = true;
                replayMotifSyllable();
            } else {
                composeSyllable(cur_, false);
                generateCurves(cur_);
            }
        } else {
            const bool intro = maybeStartIntro_;
            maybeStartIntro_ = false;
            composeSyllable(cur_, intro);
            generateCurves(cur_);
            if (motifFill_ < motifTarget_ && motifFill_ < kMaxMotif) {
                motif_[static_cast<std::size_t>(motifFill_)] = cur_;
                ++motifFill_;
                motifLen_ = motifFill_;
            }
        }
        curN_ = cur_.n;
        curI_ = 0;
        tGest_ = 0.0;
        state_ = State::Syllable;
        pushTrace(fromMotif);
        ++sylCount_;
    }

    void beginGap() noexcept {
        const double speed = std::clamp(ctx_.speed, 0.25, 4.0);
        const double dens = std::clamp(ctx_.density, 0.0, 1.0);
        const double medMs = (kGapOffsetMs + kGapSpanMs * (1.0 - dens)) / speed;
        const double z = truncGauss();
        double gapSec = (medMs * 0.001) * std::exp(0.35 * z);
        if (static_cast<double>(rng_.uniform()) < 0.15 * (1.0 - dens)) {
            gapSec *= 3.0;
        }
        if (ctx_.tempoSync && ctx_.hostPlaying && ctx_.bpm > 1.0) {
            const double grid = syncDivisionQuarterNotes(ctx_.syncDivision);
            const double bpm = ctx_.bpm;
            double ppq = ctx_.tempoValid ? ctx_.ppq : ppq_;
            double next = std::ceil((ppq + 1.0e-9) / grid) * grid;
            double wait = (next - ppq) * 60.0 / bpm;
            if (wait < 0.030) {
                next += grid;
                wait = (next - ppq) * 60.0 / bpm;
            }
            gapSec = wait;
        }
        gapLeft_ = std::max(0.0, gapSec);
        state_ = State::Gap;
    }

    void pushTrace(bool fromMotif) noexcept {
        if (traceN_ >= kTraceCap) {
            return;
        }
        auto& t = trace_[static_cast<std::size_t>(traceN_++)];
        t.nGestures = cur_.n;
        t.durationMs = 0.0;
        t.onsetSec = timeSec_;
        t.fromMotif = fromMotif;
        for (int i = 0; i < 4; ++i) {
            t.types[i] = -1;
            for (double& p : t.params[i]) {
                p = 0.0;
            }
        }
        for (int i = 0; i < cur_.n; ++i) {
            t.types[i] = cur_.g[static_cast<std::size_t>(i)].type;
            t.durationMs += cur_.g[static_cast<std::size_t>(i)].inst.durationMs;
            for (int j = 0; j < cur_.g[static_cast<std::size_t>(i)].nParams && j < 8; ++j) {
                t.params[i][j] = cur_.g[static_cast<std::size_t>(i)].params[j];
            }
        }
    }

    GestureEngineContext ctx_{};
    dsp::Rng rng_{1};
    std::uint32_t phraseSeed_ = 1;
    std::uint32_t genCounter_ = 0;
    double anchor_ = 6900.0;
    State state_ = State::Idle;
    bool finishing_ = false;
    bool freezeLatched_ = false;
    bool maybeStartIntro_ = false;
    SylRec motif_[kMaxMotif]{};
    int motifLen_ = 0;
    int motifFill_ = 0;
    int motifPos_ = 0;
    int motifTarget_ = 3;
    SylRec cur_{};
    int curN_ = 0;
    int curI_ = 0;
    double tGest_ = 0.0;
    double gapLeft_ = 0.0;
    double timeSec_ = 0.0;
    double ppq_ = 0.0;
    int lastType_[2]{-1, -1};
    int sylCount_ = 0;
    SyllableTrace trace_[kTraceCap]{};
    int traceN_ = 0;
    double kIntroN_[3]{5.0, 0.45, 0.08};
};

} // namespace cuicui::control
