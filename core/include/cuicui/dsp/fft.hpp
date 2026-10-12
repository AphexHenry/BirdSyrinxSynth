#pragma once

#include <cmath>
#include <cstddef>
#include <numbers>
#include <utility>
#include <vector>

namespace cuicui::dsp {

/// In-place iterative radix-2 complex FFT on split real/imaginary float arrays.
/// prepare() allocates the tables; forward()/inverse() are real-time safe.
class Fft {
public:
    void prepare(int n) {
        n_ = n;
        rev_.assign(static_cast<std::size_t>(n), 0);
        int bits = 0;
        while ((1 << bits) < n) {
            ++bits;
        }
        for (int i = 0; i < n; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b) {
                r |= ((i >> b) & 1) << (bits - 1 - b);
            }
            rev_[static_cast<std::size_t>(i)] = r;
        }
        cos_.resize(static_cast<std::size_t>(n / 2));
        sin_.resize(static_cast<std::size_t>(n / 2));
        for (int k = 0; k < n / 2; ++k) {
            const double a = -2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(n);
            cos_[static_cast<std::size_t>(k)] = static_cast<float>(std::cos(a));
            sin_[static_cast<std::size_t>(k)] = static_cast<float>(std::sin(a));
        }
    }

    [[nodiscard]] int size() const noexcept { return n_; }

    /// X[k] = Σ x[t] e^{−2πikt/n}.
    void forward(float* re, float* im) const noexcept { transform(re, im, false); }

    /// x[t] = (1/n) Σ X[k] e^{+2πikt/n}.
    void inverse(float* re, float* im) const noexcept {
        transform(re, im, true);
        const float s = 1.0f / static_cast<float>(n_);
        for (int i = 0; i < n_; ++i) {
            re[i] *= s;
            im[i] *= s;
        }
    }

private:
    void transform(float* re, float* im, bool inv) const noexcept {
        for (int i = 0; i < n_; ++i) {
            const int r = rev_[static_cast<std::size_t>(i)];
            if (r > i) {
                std::swap(re[i], re[r]);
                std::swap(im[i], im[r]);
            }
        }
        for (int len = 2; len <= n_; len <<= 1) {
            const int half = len >> 1;
            const int step = n_ / len;
            for (int i = 0; i < n_; i += len) {
                for (int k = 0; k < half; ++k) {
                    const float wr = cos_[static_cast<std::size_t>(k * step)];
                    const float wi = inv ? -sin_[static_cast<std::size_t>(k * step)] : sin_[static_cast<std::size_t>(k * step)];
                    const int a = i + k;
                    const int b = a + half;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
            }
        }
    }

    int n_ = 0;
    std::vector<int> rev_;
    std::vector<float> cos_, sin_;
};

} // namespace cuicui::dsp
