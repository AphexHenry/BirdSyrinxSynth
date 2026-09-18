#pragma once

#include "cuicui/control/gesture.hpp"
#include "cuicui/control/inverse_map.hpp"
#include "cuicui/control/voice_controller.hpp"
#include "cuicui/io/gesture_score.hpp"
#include "cuicui/io/offline_render.hpp"
#include "cuicui/model/bird_voice.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cuicui::control {

/// GestureSource backed by a preloaded list of GestureInstance (no allocation in next()).
class ScoreGestureSource final : public GestureSource {
public:
    void setInstances(const GestureInstance* data, int n) noexcept {
        data_ = data;
        n_ = std::max(0, n);
    }

    void setTime(double tSeconds) noexcept { t_ = tSeconds; }

    [[nodiscard]] double time() const noexcept { return t_; }

    void noteOn(double, int, std::uint32_t) noexcept override {}
    void noteOff() noexcept override {}

    GestureSample next(double dtSeconds) noexcept override {
        GestureSample best;
        int bestIdx = -1;
        for (int i = 0; i < n_; ++i) {
            if (gestureCovers(data_[static_cast<std::size_t>(i)], t_)) {
                if (bestIdx < 0 || data_[static_cast<std::size_t>(i)].tStart >=
                                       data_[static_cast<std::size_t>(bestIdx)].tStart) {
                    bestIdx = i;
                }
            }
        }
        if (bestIdx >= 0) {
            best = evaluateInstance(data_[static_cast<std::size_t>(bestIdx)], t_);
        }
        t_ += dtSeconds > 0.0 ? dtSeconds : 0.0;
        return best;
    }

    void reset() noexcept { t_ = 0.0; }

private:
    const GestureInstance* data_ = nullptr;
    int n_ = 0;
    double t_ = 0.0;
};

/// Plays a gesture score: physio tracks (BirdVoice) and/or gesture events (VoiceController).
/// `load` / `prepare` allocate; `process` is real-time safe.
class ScorePlayer {
public:
    static constexpr int kSubBlock = 32;

    void load(const io::GestureScore& score) {
        score_ = score;
        settings_ = io::settings_from_score(score);
        breath_ = io::score_setting(score, "perf.breath", 0.7);
        tone_ = io::score_setting(score, "perf.tone", 0.6);
        size_ = io::score_setting(score, "body.size", settings_.body_size);
        seed_ = settings_.seed;
        instances_.clear();
        if (!score.has_physio) {
            instances_.reserve(score.gestures.size());
            for (const auto& e : score.gestures) {
                instances_.push_back(io::instance_from_event(e));
            }
        }
        src_.setInstances(instances_.data(), static_cast<int>(instances_.size()));
        loaded_ = true;
        prepared_ = false;
    }

    void prepare(const ProcessSpec& spec) {
        spec_ = spec;
        voice_.prepare(spec);
        voice_.setSource(VoiceSource::Labial);
        const double sr = spec.hostSampleRate > 0.0 ? spec.hostSampleRate : 48000.0;
        ctrlRate_ = sr / static_cast<double>(kSubBlock);
        controller_.prepare(ctrlRate_, map_);
        controller_.setGestureSource(&src_);
        reset();
        prepared_ = true;
    }

    void reset() noexcept {
        src_.reset();
        src_.setInstances(instances_.data(), static_cast<int>(instances_.size()));
        controller_.reset();
        voice_.setSettings(settings_);
        voice_.reset(seed_ == 0 ? 1ULL : seed_);
        lastFrame_ = ControlFrame{};
        hostIndex_ = 0;
        primed_ = false;
        wasGate_ = false;
    }

    [[nodiscard]] bool hasPhysio() const noexcept { return score_.has_physio; }
    [[nodiscard]] bool ignoredGestures() const noexcept { return score_.ignore_gestures; }

    [[nodiscard]] double durationSeconds() const {
        if (score_.has_physio) {
            int nCtrl = 0;
            for (const auto& kv : score_.tracks) {
                nCtrl = std::max(nCtrl, static_cast<int>(kv.second.size()));
            }
            if (nCtrl <= 0 || !(score_.control_rate_hz > 0.0)) {
                return 0.0;
            }
            return static_cast<double>(nCtrl) / score_.control_rate_hz;
        }
        double end = 0.0;
        for (const auto& g : instances_) {
            end = std::max(end, g.tStart + g.durationMs * 0.001);
        }
        return end + 0.15; // physical release tail
    }

    /// Offline convenience: physio path is bit-identical to `render_controls`.
    [[nodiscard]] std::vector<float> render(double sampleRate, int oversampling = 0) {
        if (score_.has_physio) {
            VoiceSettings st = settings_;
            return io::render_controls(st, score_.tracks, score_.control_rate_hz, sampleRate, seed_,
                                       oversampling);
        }
        ProcessSpec ps;
        ps.hostSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        ps.maxBlockSize = 512;
        ps.oversampling = oversampling;
        prepare(ps);
        const int n = static_cast<int>(std::llround(durationSeconds() * ps.hostSampleRate));
        std::vector<float> buf(n > 0 ? static_cast<std::size_t>(n) : 0, 0.0f);
        if (n > 0) {
            process(buf.data(), n);
        }
        return buf;
    }

    /// Real-time: no allocation. Call after prepare().
    void process(float* out, int n) noexcept {
        if (!prepared_ || out == nullptr || n <= 0) {
            return;
        }
        if (score_.has_physio) {
            processPhysio(out, n);
            return;
        }
        processGestures(out, n);
    }

private:
    void primeIfNeeded(const ControlFrame& fr) noexcept {
        if (primed_) {
            return;
        }
        voice_.primeLabial(model::kAlphaRest + static_cast<double>(fr.src_L_alpha_offset),
                           static_cast<double>(fr.src_L_beta), static_cast<double>(fr.oec_freq_hz),
                           static_cast<double>(fr.beak_gape), static_cast<double>(fr.src_R_alpha_offset),
                           static_cast<double>(fr.src_R_beta));
        lastFrame_ = fr;
        primed_ = true;
    }

    void processPhysio(float* out, int n) noexcept {
        const double sr = spec_.hostSampleRate > 0.0 ? spec_.hostSampleRate : 48000.0;
        const double cr = score_.control_rate_hz > 0.0 ? score_.control_rate_hz : sr;
        const double dt = 1.0 / sr;
        for (int i = 0; i < n; ++i) {
            const double t0 = static_cast<double>(hostIndex_ + i) * dt;
            const double t1 = static_cast<double>(hostIndex_ + i + 1) * dt;
            ControlFrame from{}, to{};
            VoiceSettings s0 = settings_;
            VoiceSettings s1 = settings_;
            fillPhysio(from, s0, t0, cr);
            fillPhysio(to, s1, t1, cr);
            if (!primed_) {
                primeIfNeeded(from);
            }
            voice_.setOutGainRamp(static_cast<float>(s0.out_gain_db), static_cast<float>(s1.out_gain_db));
            voice_.setSettings(s1);
            voice_.process(from, to, out + i, 1);
        }
        hostIndex_ += n;
    }

    void fillPhysio(ControlFrame& fr, VoiceSettings& st, double t, double cr) const noexcept {
        fr = ControlFrame{};
        for (const auto& kv : score_.tracks) {
            io::apply_control_at(fr, st, kv.first, io::interp_control(kv.second, cr, t));
        }
    }

    void processGestures(float* out, int n) noexcept {
        const double sr = spec_.hostSampleRate > 0.0 ? spec_.hostSampleRate : 48000.0;
        int pos = 0;
        while (pos < n) {
            const int count = std::min(kSubBlock, n - pos);
            const double t = static_cast<double>(hostIndex_ + pos) / sr;
            src_.setTime(t);
            ControlFrame to{};
            PerformInputs in;
            const bool active = gestureActiveAt(t);
            in.gate = active;
            in.noteCents = currentAnchor(t);
            in.velocity01 = 1.0;
            in.breath = breath_;
            in.tone = tone_;
            in.physics = 0.5;
            in.motion = 0.0;
            in.range = 1.0;
            in.speed = 1.0;
            in.snap = 0.0;
            in.glideMs = 0.0;
            in.size = size_;
            in.rangeFold = true;
            in.leash = 1.0;
            in.seed = seed_;
            in.noteOnEvent = active && !wasGate_;
            in.noteOffEvent = !active && wasGate_;
            controller_.tick(in, settings_, to);
            wasGate_ = active;
            if (!primed_) {
                primeIfNeeded(to);
            }
            voice_.setSettings(settings_);
            voice_.setOutGainRamp(static_cast<float>(settings_.out_gain_db),
                                  static_cast<float>(settings_.out_gain_db));
            voice_.process(lastFrame_, to, out + pos, count);
            lastFrame_ = to;
            pos += count;
        }
        hostIndex_ += n;
    }

    [[nodiscard]] bool gestureActiveAt(double t) const noexcept {
        for (const auto& g : instances_) {
            if (gestureCovers(g, t)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] double currentAnchor(double t) const noexcept {
        int best = -1;
        for (int i = 0; i < static_cast<int>(instances_.size()); ++i) {
            if (gestureCovers(instances_[static_cast<std::size_t>(i)], t)) {
                if (best < 0 || instances_[static_cast<std::size_t>(i)].tStart >=
                                    instances_[static_cast<std::size_t>(best)].tStart) {
                    best = i;
                }
            }
        }
        if (best >= 0) {
            return instances_[static_cast<std::size_t>(best)].anchorCents;
        }
        return 9600.0;
    }

    io::GestureScore score_;
    VoiceSettings settings_;
    std::vector<GestureInstance> instances_;
    ScoreGestureSource src_;
    InverseMap map_;
    VoiceController controller_;
    BirdVoice voice_;
    ProcessSpec spec_;
    ControlFrame lastFrame_{};
    double ctrlRate_ = 1500.0;
    double breath_ = 0.7;
    double tone_ = 0.6;
    double size_ = 1.0;
    std::uint64_t seed_ = 1;
    int hostIndex_ = 0;
    bool loaded_ = false;
    bool prepared_ = false;
    bool primed_ = false;
    bool wasGate_ = false;
};

} // namespace cuicui::control
