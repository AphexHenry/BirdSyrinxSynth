#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace cuicui::io {

struct AudioFeatures {
    double median_f0_hz = 0.0;
    double rms_db = -120.0;
    double spectral_centroid_hz = 0.0;
    double peaks_hz[3] = {0.0, 0.0, 0.0};
};

[[nodiscard]] inline double rms_db(const float* x, int n, int skip = 0) noexcept {
    if (x == nullptr || n <= skip) {
        return -120.0;
    }
    double acc = 0.0;
    int c = 0;
    for (int i = skip; i < n; ++i) {
        const double v = static_cast<double>(x[i]);
        acc += v * v;
        ++c;
    }
    if (c <= 0) {
        return -120.0;
    }
    const double r = std::sqrt(acc / static_cast<double>(c));
    if (r <= 1.0e-12) {
        return -120.0;
    }
    return 20.0 * std::log10(r);
}

[[nodiscard]] inline double zc_f0_hz(const float* x, int n, double sr, int skip) noexcept {
    if (x == nullptr || n - skip < 32 || sr <= 0.0) {
        return 0.0;
    }
    double mu = 0.0;
    const int m = n - skip;
    for (int i = skip; i < n; ++i) {
        mu += static_cast<double>(x[i]);
    }
    mu /= static_cast<double>(m);
    double t0 = -1.0;
    double t1 = -1.0;
    int count = 0;
    for (int i = skip + 1; i < n; ++i) {
        const double a = static_cast<double>(x[i - 1]) - mu;
        const double b = static_cast<double>(x[i]) - mu;
        if (a <= 0.0 && b > 0.0) {
            const double frac = (b != a) ? (-a / (b - a)) : 0.0;
            const double t = (static_cast<double>(i - 1) + frac) / sr;
            if (count == 0) {
                t0 = t;
            }
            t1 = t;
            ++count;
        }
    }
    if (count < 4 || t1 <= t0) {
        return 0.0;
    }
    return static_cast<double>(count - 1) / (t1 - t0);
}

/// Hann-windowed rFFT magnitude on the last `nfft` samples (naive DFT; nfft is small).
inline void mag_spectrum(const float* x, int n, int nfft, std::vector<double>& mag) {
    mag.assign(static_cast<std::size_t>(nfft / 2 + 1), 0.0);
    if (x == nullptr || n < nfft || nfft < 16) {
        return;
    }
    int i0 = 0;
    double bestE = -1.0;
    const int hop = nfft / 4;
    for (int start = 0; start + nfft <= n; start += std::max(1, hop)) {
        double e = 0.0;
        for (int i = 0; i < nfft; ++i) {
            const double v = static_cast<double>(x[start + i]);
            e += v * v;
        }
        if (e > bestE) {
            bestE = e;
            i0 = start;
        }
    }
    const double two_pi = 6.28318530717958647692;
    for (int k = 0; k <= nfft / 2; ++k) {
        double re = 0.0;
        double im = 0.0;
        for (int i = 0; i < nfft; ++i) {
            const double w = 0.5 - 0.5 * std::cos(two_pi * static_cast<double>(i) / static_cast<double>(nfft - 1));
            const double v = static_cast<double>(x[i0 + i]) * w;
            const double ph = two_pi * static_cast<double>(k) * static_cast<double>(i) / static_cast<double>(nfft);
            re += v * std::cos(ph);
            im -= v * std::sin(ph);
        }
        mag[static_cast<std::size_t>(k)] = std::hypot(re, im);
    }
}

inline AudioFeatures measure_features(const float* x, int n, double sr) {
    AudioFeatures f;
    const int skip = std::min(n / 4, static_cast<int>(std::lround(0.05 * sr)));
    f.median_f0_hz = zc_f0_hz(x, n, sr, skip);
    f.rms_db = rms_db(x, n, skip);
    constexpr int nfft = 4096;
    std::vector<double> mag;
    mag_spectrum(x, n, nfft, mag);
    double num = 0.0;
    double den = 0.0;
    for (int k = 1; k < static_cast<int>(mag.size()); ++k) {
        const double freq = static_cast<double>(k) * sr / static_cast<double>(nfft);
        if (freq > 12000.0) {
            break;
        }
        num += freq * mag[static_cast<std::size_t>(k)];
        den += mag[static_cast<std::size_t>(k)];
    }
    f.spectral_centroid_hz = den > 1.0e-18 ? num / den : 0.0;

    struct Peak {
        double mag = 0.0;
        double hz = 0.0;
    };
    Peak peaks[3]{};
    for (int k = 2; k + 1 < static_cast<int>(mag.size()); ++k) {
        const double freq = static_cast<double>(k) * sr / static_cast<double>(nfft);
        if (freq < 80.0 || freq > 12000.0) {
            continue;
        }
        const double m0 = mag[static_cast<std::size_t>(k - 1)];
        const double m1 = mag[static_cast<std::size_t>(k)];
        const double m2 = mag[static_cast<std::size_t>(k + 1)];
        if (m1 >= m0 && m1 >= m2 && m1 > 1.0e-9) {
            Peak p{m1, freq};
            if (p.mag > peaks[0].mag) {
                peaks[2] = peaks[1];
                peaks[1] = peaks[0];
                peaks[0] = p;
            } else if (p.mag > peaks[1].mag) {
                peaks[2] = peaks[1];
                peaks[1] = p;
            } else if (p.mag > peaks[2].mag) {
                peaks[2] = p;
            }
        }
    }
    f.peaks_hz[0] = peaks[0].hz;
    f.peaks_hz[1] = peaks[1].hz;
    f.peaks_hz[2] = peaks[2].hz;
    return f;
}

/// High-band (18 kHz–Nyquist) energy vs a band around `f0` (STFT frames). Returns min dB down.
inline double min_highband_below_f0_db(const float* x, int n, double sr, double f0HintHz) {
    const int nfft = 1024;
    const int hop = 256;
    if (n < nfft) {
        return 120.0;
    }
    const double two_pi = 6.28318530717958647692;
    double worst = 120.0;
    for (int i0 = 0; i0 + nfft <= n; i0 += hop) {
        std::vector<double> mag(static_cast<std::size_t>(nfft / 2 + 1), 0.0);
        for (int k = 0; k <= nfft / 2; ++k) {
            double re = 0.0, im = 0.0;
            for (int i = 0; i < nfft; ++i) {
                const double w = 0.5 - 0.5 * std::cos(two_pi * static_cast<double>(i) / static_cast<double>(nfft - 1));
                const double v = static_cast<double>(x[i0 + i]) * w;
                const double ph = two_pi * static_cast<double>(k) * static_cast<double>(i) / static_cast<double>(nfft);
                re += v * std::cos(ph);
                im -= v * std::sin(ph);
            }
            mag[static_cast<std::size_t>(k)] = re * re + im * im;
        }
        const int kF0 = [&]() {
            int best = 1;
            double bestm = 0.0;
            const int kMax = std::min(nfft / 2, static_cast<int>(12000.0 * static_cast<double>(nfft) / sr));
            for (int k = 2; k <= kMax; ++k) {
                if (mag[static_cast<std::size_t>(k)] > bestm) {
                    bestm = mag[static_cast<std::size_t>(k)];
                    best = k;
                }
            }
            (void)f0HintHz;
            return best;
        }();
        const int kLo = std::max(1, kF0 - 2);
        const int kHi = std::min(nfft / 2, kF0 + 2);
        double eF0 = 0.0;
        for (int k = kLo; k <= kHi; ++k) {
            eF0 += mag[static_cast<std::size_t>(k)];
        }
        const int k18 = std::min(nfft / 2, static_cast<int>(std::ceil(18000.0 * static_cast<double>(nfft) / sr)));
        double eHi = 0.0;
        for (int k = k18; k <= nfft / 2; ++k) {
            eHi += mag[static_cast<std::size_t>(k)];
        }
        if (eF0 <= 1.0e-30) {
            continue;
        }
        const double db = 10.0 * std::log10(std::max(eHi, 1.0e-30) / eF0);
        worst = std::min(worst, -db); // how far below; positive means highband quieter
    }
    return worst;
}

} // namespace cuicui::io
