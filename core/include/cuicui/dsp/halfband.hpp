#pragma once

#include "cuicui/model/voice_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace cuicui::dsp {

/// Modified Bessel I0 (series). Used by the Kaiser window.
inline double besselI0(double x) noexcept {
    const double y = x * x * 0.25;
    double term = 1.0;
    double sum = 1.0;
    for (int k = 1; k < 48; ++k) {
        term *= y / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term < 1e-18 * sum) {
            break;
        }
    }
    return sum;
}

/// Linear-phase FIR half-band decimator (Kaiser window, designed at `prepare`).
/// Every other tap except the centre is zero; the inner loop skips those zeros.
/// See 01_research.md §6.1 and WP-A2 §4.3.
class HalfbandDecimator {
public:
    static constexpr double kPassHz = 20000.0;
    static constexpr double kAttenDb = 90.0;

    void prepare(double inputRate) {
        fin_ = inputRate > 0.0 ? inputRate : 96000.0;
        const double fout = fin_ * 0.5;
        // Transition 20 kHz → fout − 20 kHz (aliases of the stop band land at 20 kHz → 0).
        double df = fout - 2.0 * kPassHz;
        if (df < 400.0) {
            df = 400.0;
        }
        const double dw = 2.0 * 3.14159265358979323846 * df / fin_;
        const double beta = 0.1102 * (kAttenDb - 8.7); // A > 50
        int n = static_cast<int>(std::ceil((kAttenDb - 8.0) / (2.285 * dw))) + 1;
        if (n < 5) {
            n = 5;
        }
        if ((n % 2) == 0) {
            ++n;
        }
        // N = 4K+1 so the centre index is even and even-offset taps (except centre) are zero.
        while (n % 4 != 1) {
            n += 2;
        }
        // Extra length: last stages (narrow 20 kHz → f_out−20 kHz) need a flatter passband.
        n += (df < 10000.0) ? 48 : 16;
        while (n % 4 != 1) {
            n += 2;
        }

        n_ = n;
        const int m = (n_ - 1) / 2;
        h_.assign(static_cast<std::size_t>(n_), 0.0);
        z_.assign(static_cast<std::size_t>(n_), 0.0);
        const double i0b = besselI0(beta);
        for (int i = 0; i < n_; ++i) {
            const double k = static_cast<double>(i - m);
            const double win = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - (k / m) * (k / m)))) / i0b;
            double sinc;
            if (k == 0.0) {
                sinc = 0.5;
            } else {
                sinc = std::sin(0.5 * 3.14159265358979323846 * k) / (3.14159265358979323846 * k);
            }
            h_[static_cast<std::size_t>(i)] = sinc * win;
        }
        // Force exact half-band zeros and centre 0.5 (numerical noise on even offsets).
        h_[static_cast<std::size_t>(m)] = 0.5;
        for (int k = 2; k <= m; k += 2) {
            h_[static_cast<std::size_t>(m + k)] = 0.0;
            h_[static_cast<std::size_t>(m - k)] = 0.0;
        }
        double dc = 0.0;
        for (double t : h_) {
            dc += t;
        }
        if (std::abs(dc) > 1e-18) {
            for (double& t : h_) {
                t /= dc;
            }
        }
        w_ = 0;
        pair_ = 0;
        designed_beta_ = beta;
        reset();
    }

    void reset() noexcept {
        std::fill(z_.begin(), z_.end(), 0.0);
        w_ = 0;
        pair_ = 0;
    }

    /// Push one input sample. When a pair is complete, `hadOutput` is true and the return is the decimated sample.
    float push(float x, bool& hadOutput) noexcept {
        z_[static_cast<std::size_t>(w_)] = x;
        w_ = (w_ + 1) % n_;
        pair_ ^= 1;
        if (pair_ != 0) {
            hadOutput = false;
            return 0.0f;
        }
        hadOutput = true;
        return static_cast<float>(dot());
    }

    [[nodiscard]] int numTaps() const noexcept { return n_; }
    [[nodiscard]] int groupDelayInputSamples() const noexcept { return (n_ - 1) / 2; }
    [[nodiscard]] double inputRate() const noexcept { return fin_; }
    [[nodiscard]] double kaiserBeta() const noexcept { return designed_beta_; }
    [[nodiscard]] const std::vector<double>& taps() const noexcept { return h_; }

    /// DTFT magnitude in dB at frequency `freqHz` (input-rate Hertz).
    [[nodiscard]] double magnitudeDb(double freqHz) const noexcept {
        const double w = 2.0 * 3.14159265358979323846 * freqHz / fin_;
        double re = 0.0;
        double im = 0.0;
        for (int i = 0; i < n_; ++i) {
            const double t = h_[static_cast<std::size_t>(i)];
            re += t * std::cos(w * static_cast<double>(i));
            im -= t * std::sin(w * static_cast<double>(i));
        }
        const double mag = std::hypot(re, im);
        if (mag <= 1e-20) {
            return -400.0;
        }
        return 20.0 * std::log10(mag);
    }

    void save(HalfbandState& s) const noexcept {
        s.n = n_;
        s.w = w_;
        s.pair = pair_;
        const int m = std::min(n_, kMaxHalfbandTaps);
        for (int i = 0; i < m; ++i) {
            s.z[i] = z_[static_cast<std::size_t>(i)];
        }
    }

    void load(const HalfbandState& s) noexcept {
        w_ = s.w;
        pair_ = s.pair;
        const int m = std::min(n_, kMaxHalfbandTaps);
        for (int i = 0; i < m; ++i) {
            z_[static_cast<std::size_t>(i)] = s.z[i];
        }
        if (n_ > 0) {
            w_ %= n_;
            if (w_ < 0) {
                w_ += n_;
            }
        }
    }

private:
    [[nodiscard]] double dot() const noexcept {
        // z_[w_] is the oldest sample (next write position); centre tap at delay (N-1)/2.
        const int m = (n_ - 1) / 2;
        double acc = 0.0;
        for (int k = 1; k <= m; k += 2) {
            const double hk = h_[static_cast<std::size_t>(m + k)]; // = h[m-k]
            acc += hk * (tap(m + k) + tap(m - k));
        }
        acc += 0.5 * tap(m);
        return acc;
    }

    [[nodiscard]] double tap(int age) const noexcept {
        // age 0 = newest sample (just written, then w_ advanced).
        int idx = w_ - 1 - age;
        if (idx < 0) {
            idx += n_;
        }
        return z_[static_cast<std::size_t>(idx)];
    }

    int n_ = 0;
    int w_ = 0;
    int pair_ = 0;
    double fin_ = 96000.0;
    double designed_beta_ = 0.0;
    std::vector<double> h_;
    std::vector<double> z_;
};

} // namespace cuicui::dsp
