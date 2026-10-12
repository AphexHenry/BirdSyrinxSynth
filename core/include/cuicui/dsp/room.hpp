#pragma once

#include "cuicui/dsp/fft.hpp"
#include "cuicui/dsp/rng.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cuicui::dsp {

/// The PROTO1 prototype's small room (WP-R1, D-022 "light room"), reproduced:
/// - impulse response: 0.4 s, two decorrelated channels of one-pole low-passed noise
///   (lp += 0.35·(noise − lp)), RT60 0.22 s, silent for the first 4 ms (`Engine.roomIR`);
/// - scaled like the Web Audio ConvolverNode with normalize = true (0.00125 / RMS · 44100 / fs);
/// - mixed at 0.09 (−21 dB) on top of the dry signal (`Engine.apply`).
/// Uniformly partitioned FFT convolution. Its block delay is hidden inside the 4 ms pre-delay, so the
/// wet signal is sample-aligned with the prototype and the room adds no latency.
/// prepare() allocates; reset() and process() are real-time safe.
class Room {
public:
    static constexpr double kLengthS = 0.4;
    static constexpr double kRt60S = 0.22;
    static constexpr double kPreDelayS = 0.004;
    static constexpr float kWetGain = 0.09f;
    static constexpr double kSmoothMs = 20.0;

    void prepare(double sampleRate, std::uint64_t seed = 0x51E1A7ULL) {
        sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        const int n = static_cast<int>(std::lround(kLengthS * sr_));
        std::array<std::vector<double>, 2> ir;
        Rng rng(seed);
        double power = 0.0;
        int zeros = 0;
        for (int ch = 0; ch < 2; ++ch) {
            ir[static_cast<std::size_t>(ch)].assign(static_cast<std::size_t>(n), 0.0);
            double lp = 0.0;
            for (int i = 0; i < n; ++i) {
                const double t = static_cast<double>(i) / sr_;
                const double noise = static_cast<double>(rng.next() >> 8) * 0x1.0p-24 * 2.0 - 1.0;
                lp += 0.35 * (noise - lp); // gentle low-pass: rooms are darker
                const double v = t < kPreDelayS ? 0.0 : lp * std::exp(-6.91 * t / kRt60S);
                ir[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] = v;
                power += v * v;
                if (ch == 0 && t < kPreDelayS) {
                    zeros = i + 1;
                }
            }
        }
        // Web Audio ConvolverNode normalisation (normalize = true).
        power = std::sqrt(power / (2.0 * static_cast<double>(n)));
        if (!(power >= 0.000125)) {
            power = 0.000125;
        }
        const double scale = 0.00125 / power * (44100.0 / sr_);
        for (int ch = 0; ch < 2; ++ch) {
            auto& dst = impulse_[static_cast<std::size_t>(ch)];
            dst.resize(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i) {
                dst[static_cast<std::size_t>(i)] =
                    static_cast<float>(ir[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] * scale);
            }
        }

        // Largest power-of-two block that fits in the silent pre-delay: the block delay disappears.
        b_ = 16;
        while (b_ * 2 <= zeros) {
            b_ *= 2;
        }
        const int nfft = 2 * b_;
        k_ = b_ + 1;
        p_ = std::max(1, (n - b_ + b_ - 1) / b_);
        fft_.prepare(nfft);
        re_.assign(static_cast<std::size_t>(nfft), 0.0f);
        im_.assign(static_cast<std::size_t>(nfft), 0.0f);
        const auto spec = static_cast<std::size_t>(p_ * k_);
        for (int ch = 0; ch < 2; ++ch) {
            auto& c = ch_[static_cast<std::size_t>(ch)];
            c.hRe.assign(spec, 0.0f);
            c.hIm.assign(spec, 0.0f);
            c.xRe.assign(spec, 0.0f);
            c.xIm.assign(spec, 0.0f);
            c.in.assign(static_cast<std::size_t>(b_), 0.0f);
            c.prev.assign(static_cast<std::size_t>(b_), 0.0f);
            c.out.assign(static_cast<std::size_t>(b_), 0.0f);
            for (int p = 0; p < p_; ++p) {
                std::fill(re_.begin(), re_.end(), 0.0f);
                std::fill(im_.begin(), im_.end(), 0.0f);
                for (int i = 0; i < b_; ++i) {
                    const int src = b_ + p * b_ + i; // the IR advanced by one block (those taps are zero)
                    if (src < n) {
                        re_[static_cast<std::size_t>(i)] =
                            static_cast<float>(ir[static_cast<std::size_t>(ch)][static_cast<std::size_t>(src)] * scale);
                    }
                }
                fft_.forward(re_.data(), im_.data());
                for (int k = 0; k < k_; ++k) {
                    c.hRe[static_cast<std::size_t>(p * k_ + k)] = re_[static_cast<std::size_t>(k)];
                    c.hIm[static_cast<std::size_t>(p * k_ + k)] = im_[static_cast<std::size_t>(k)];
                }
            }
        }
        yRe_.assign(static_cast<std::size_t>(k_), 0.0f);
        yIm_.assign(static_cast<std::size_t>(k_), 0.0f);
        smooth_ = static_cast<float>(1.0 - std::exp(-1.0 / (kSmoothMs * 0.001 * sr_)));
        irLength_ = n;
        reset();
    }

    void reset() noexcept {
        for (auto& c : ch_) {
            std::fill(c.xRe.begin(), c.xRe.end(), 0.0f);
            std::fill(c.xIm.begin(), c.xIm.end(), 0.0f);
            std::fill(c.in.begin(), c.in.end(), 0.0f);
            std::fill(c.prev.begin(), c.prev.end(), 0.0f);
            std::fill(c.out.begin(), c.out.end(), 0.0f);
        }
        pos_ = 0;
        head_ = 0;
        wet_ = 0.0f;
        zeroBlocks_ = p_ + 1;
        stale_ = false;
    }

    /// Add the room of (inL, inR) to (outL, outR). Buffers may alias (in == out: dry + wet).
    /// `on` ramps the wet level to 0.09 or 0 over ~20 ms.
    void process(const float* inL, const float* inR, float* outL, float* outR, int n, bool on) noexcept {
        if (b_ == 0) {
            return;
        }
        const float target = on ? kWetGain : 0.0f;
        if (!on && wet_ < 1e-6f) { // fully off: skip the work, forget the tail
            wet_ = 0.0f;
            stale_ = true;
            return;
        }
        if (stale_) {
            reset();
        }
        auto& l = ch_[0];
        auto& r = ch_[1];
        for (int i = 0; i < n; ++i) {
            wet_ += (target - wet_) * smooth_;
            const float xl = inL[i];
            const float xr = inR[i];
            const float yl = l.out[static_cast<std::size_t>(pos_)];
            const float yr = r.out[static_cast<std::size_t>(pos_)];
            l.in[static_cast<std::size_t>(pos_)] = xl;
            r.in[static_cast<std::size_t>(pos_)] = xr;
            outL[i] += wet_ * yl;
            outR[i] += wet_ * yr;
            if (++pos_ == b_) {
                pos_ = 0;
                block();
            }
        }
    }

    /// True while the room may still output something (input arrived within the last IR length).
    [[nodiscard]] bool ringing() const noexcept { return b_ > 0 && zeroBlocks_ <= p_ && !stale_; }
    [[nodiscard]] int blockSize() const noexcept { return b_; }
    [[nodiscard]] int partitions() const noexcept { return p_; }
    [[nodiscard]] int irLength() const noexcept { return irLength_; }
    /// The scaled impulse response of a channel (before the 0.09 wet gain), for tests and plots.
    [[nodiscard]] const std::vector<float>& impulse(int ch) const noexcept {
        return impulse_[static_cast<std::size_t>(ch & 1)];
    }

private:
    struct Channel {
        std::vector<float> hRe, hIm; ///< IR partition spectra, p_ × k_
        std::vector<float> xRe, xIm; ///< frequency-domain delay line, p_ × k_
        std::vector<float> in, prev, out;
    };

    void block() noexcept {
        bool silent = true;
        for (const auto& c : ch_) {
            for (const float v : c.in) {
                if (v != 0.0f) {
                    silent = false;
                    break;
                }
            }
        }
        zeroBlocks_ = silent ? std::min(zeroBlocks_ + 1, p_ + 1) : 0;
        if (zeroBlocks_ > p_) { // every spectrum in the delay line is zero: nothing to compute
            for (auto& c : ch_) {
                std::fill(c.out.begin(), c.out.end(), 0.0f);
                std::fill(c.prev.begin(), c.prev.end(), 0.0f);
            }
            return;
        }
        const int nfft = 2 * b_;
        for (auto& c : ch_) {
            for (int i = 0; i < b_; ++i) {
                re_[static_cast<std::size_t>(i)] = c.prev[static_cast<std::size_t>(i)];
                re_[static_cast<std::size_t>(b_ + i)] = c.in[static_cast<std::size_t>(i)];
            }
            std::fill(im_.begin(), im_.end(), 0.0f);
            fft_.forward(re_.data(), im_.data());
            float* xr = c.xRe.data() + static_cast<std::ptrdiff_t>(head_ * k_);
            float* xi = c.xIm.data() + static_cast<std::ptrdiff_t>(head_ * k_);
            for (int k = 0; k < k_; ++k) {
                xr[k] = re_[static_cast<std::size_t>(k)];
                xi[k] = im_[static_cast<std::size_t>(k)];
            }
            std::fill(yRe_.begin(), yRe_.end(), 0.0f);
            std::fill(yIm_.begin(), yIm_.end(), 0.0f);
            for (int p = 0; p < p_; ++p) {
                const int slot = (head_ - p + p_) % p_;
                const float* ar = c.xRe.data() + static_cast<std::ptrdiff_t>(slot * k_);
                const float* ai = c.xIm.data() + static_cast<std::ptrdiff_t>(slot * k_);
                const float* hr = c.hRe.data() + static_cast<std::ptrdiff_t>(p * k_);
                const float* hi = c.hIm.data() + static_cast<std::ptrdiff_t>(p * k_);
                float* yr = yRe_.data();
                float* yi = yIm_.data();
                for (int k = 0; k < k_; ++k) {
                    yr[k] += ar[k] * hr[k] - ai[k] * hi[k];
                    yi[k] += ar[k] * hi[k] + ai[k] * hr[k];
                }
            }
            for (int k = 0; k < k_; ++k) {
                re_[static_cast<std::size_t>(k)] = yRe_[static_cast<std::size_t>(k)];
                im_[static_cast<std::size_t>(k)] = yIm_[static_cast<std::size_t>(k)];
            }
            for (int k = 1; k < b_; ++k) { // real signal: conjugate-symmetric upper half
                re_[static_cast<std::size_t>(nfft - k)] = yRe_[static_cast<std::size_t>(k)];
                im_[static_cast<std::size_t>(nfft - k)] = -yIm_[static_cast<std::size_t>(k)];
            }
            fft_.inverse(re_.data(), im_.data());
            for (int i = 0; i < b_; ++i) {
                c.out[static_cast<std::size_t>(i)] = re_[static_cast<std::size_t>(b_ + i)];
            }
            c.prev.swap(c.in);
        }
        head_ = (head_ + 1) % p_;
    }

    Fft fft_;
    std::array<Channel, 2> ch_;
    std::array<std::vector<float>, 2> impulse_;
    std::vector<float> re_, im_, yRe_, yIm_;
    double sr_ = 48000.0;
    float wet_ = 0.0f;
    float smooth_ = 0.001f;
    int b_ = 0;
    int k_ = 0;
    int p_ = 1;
    int pos_ = 0;
    int head_ = 0;
    int zeroBlocks_ = 0;
    int irLength_ = 0;
    bool stale_ = false;
};

} // namespace cuicui::dsp
