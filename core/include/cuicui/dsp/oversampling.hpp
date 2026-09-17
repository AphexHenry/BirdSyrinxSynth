#pragma once

#include "cuicui/dsp/halfband.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace cuicui::dsp {

/// Smallest power-of-two oversampling so hostRate * OS ≥ 176400 Hz, capped at 8.
/// `requested` ∈ {1,2,4,8} is used as-is; 0 (auto) or any other value → auto.
[[nodiscard]] inline int resolveOversampling(double hostRate, int requested) noexcept {
    if (requested == 1 || requested == 2 || requested == 4 || requested == 8) {
        return requested;
    }
    int os = 1;
    const double rate = hostRate > 0.0 ? hostRate : 48000.0;
    while (os < 8 && rate * static_cast<double>(os) < 176400.0) {
        os *= 2;
    }
    return os;
}

} // namespace cuicui::dsp

namespace cuicui {

struct ProcessSpec {
    double hostSampleRate = 48000.0;
    int maxBlockSize = 512;
    int oversampling = 0; // 0 = auto

    [[nodiscard]] int resolvedOversampling() const noexcept {
        return dsp::resolveOversampling(hostSampleRate, oversampling);
    }

    [[nodiscard]] double internalRate() const noexcept {
        return hostSampleRate * static_cast<double>(resolvedOversampling());
    }
};

} // namespace cuicui

namespace cuicui::dsp {

/// Cascade of half-band stages for OS = 2, 4, 8. OS = 1 is a copy.
class Oversampling {
public:
    void prepare(const cuicui::ProcessSpec& spec) {
        host_rate_ = spec.hostSampleRate > 0.0 ? spec.hostSampleRate : 48000.0;
        os_ = spec.resolvedOversampling();
        max_host_ = spec.maxBlockSize > 0 ? spec.maxBlockSize : 512;
        stages_.clear();
        if (os_ <= 1) {
            latency_host_ = 0;
            return;
        }
        const int nStages = static_cast<int>(std::log2(static_cast<double>(os_)));
        stages_.resize(static_cast<std::size_t>(nStages));
        double fin = host_rate_ * static_cast<double>(os_);
        double delay_host = 0.0;
        for (int s = 0; s < nStages; ++s) {
            stages_[static_cast<std::size_t>(s)].prepare(fin);
            const double gd = static_cast<double>(stages_[static_cast<std::size_t>(s)].groupDelayInputSamples());
            delay_host += gd / (static_cast<double>(os_) / static_cast<double>(1 << s));
            fin *= 0.5;
        }
        latency_host_ = static_cast<int>(std::lround(delay_host));
        scratch_a_.assign(static_cast<std::size_t>(max_host_ * os_), 0.0f);
        scratch_b_.assign(static_cast<std::size_t>(max_host_ * os_), 0.0f);
        reset();
    }

    void reset() noexcept {
        for (auto& st : stages_) {
            st.reset();
        }
        std::fill(scratch_a_.begin(), scratch_a_.end(), 0.0f);
        std::fill(scratch_b_.begin(), scratch_b_.end(), 0.0f);
    }

    [[nodiscard]] int factor() const noexcept { return os_; }
    [[nodiscard]] int latencySamples() const noexcept { return latency_host_; }
    [[nodiscard]] int numStages() const noexcept { return static_cast<int>(stages_.size()); }
    [[nodiscard]] int tapCount(int stage) const noexcept {
        if (stage < 0 || stage >= numStages()) {
            return 0;
        }
        return stages_[static_cast<std::size_t>(stage)].numTaps();
    }
    [[nodiscard]] const HalfbandDecimator& stage(int i) const noexcept { return stages_[static_cast<std::size_t>(i)]; }

    /// Decimate `nHost * os_` internal-rate samples into `nHost` host-rate samples.
    void decimate(const float* in, float* out, int nHost) noexcept {
        if (out == nullptr || nHost <= 0) {
            return;
        }
        if (os_ <= 1) {
            if (in != nullptr) {
                std::copy(in, in + nHost, out);
            }
            return;
        }
        const int nInt = nHost * os_;
        const float* src = in;
        int n = nInt;
        int ping = 0;
        for (std::size_t s = 0; s < stages_.size(); ++s) {
            const bool last = (s + 1 == stages_.size());
            float* dest = last ? out : (ping == 0 ? scratch_a_.data() : scratch_b_.data());
            int outCount = 0;
            for (int i = 0; i < n; ++i) {
                bool had = false;
                const float y = stages_[s].push(src[i], had);
                if (had) {
                    dest[outCount++] = y;
                }
            }
            n = outCount;
            src = dest;
            ping ^= 1;
        }
    }

    void save(int& nStages, HalfbandState* stages, int cap) const noexcept {
        nStages = static_cast<int>(stages_.size());
        const int n = std::min(nStages, cap);
        for (int i = 0; i < n; ++i) {
            stages_[static_cast<std::size_t>(i)].save(stages[i]);
        }
    }

    void load(int nStages, const HalfbandState* stages, int cap) noexcept {
        const int n = std::min(nStages, std::min(cap, static_cast<int>(stages_.size())));
        for (int i = 0; i < n; ++i) {
            stages_[static_cast<std::size_t>(i)].load(stages[i]);
        }
    }

private:
    double host_rate_ = 48000.0;
    int os_ = 1;
    int max_host_ = 512;
    int latency_host_ = 0;
    std::vector<HalfbandDecimator> stages_;
    std::vector<float> scratch_a_;
    std::vector<float> scratch_b_;
};

} // namespace cuicui::dsp
