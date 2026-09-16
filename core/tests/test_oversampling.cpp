#include "cuicui/dsp/oversampling.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

TEST_CASE("resolveOversampling auto policy") {
    REQUIRE(cuicui::dsp::resolveOversampling(44100.0, 0) == 4);
    REQUIRE(cuicui::dsp::resolveOversampling(48000.0, 0) == 4);
    REQUIRE(cuicui::dsp::resolveOversampling(88200.0, 0) == 2);
    REQUIRE(cuicui::dsp::resolveOversampling(96000.0, 0) == 2);
    REQUIRE(cuicui::dsp::resolveOversampling(176400.0, 0) == 1);
    REQUIRE(cuicui::dsp::resolveOversampling(192000.0, 0) == 1);
    REQUIRE(cuicui::dsp::resolveOversampling(48000.0, 1) == 1);
    REQUIRE(cuicui::dsp::resolveOversampling(48000.0, 8) == 8);
}

namespace {

std::vector<float> sine(int n, double freq, double rate, double amp = 1.0) {
    std::vector<float> x(static_cast<std::size_t>(n));
    const double w = 2.0 * 3.14159265358979323846 * freq / rate;
    for (int i = 0; i < n; ++i) {
        x[static_cast<std::size_t>(i)] = static_cast<float>(amp * std::sin(w * static_cast<double>(i)));
    }
    return x;
}

double rms(const std::vector<float>& x, int skip) {
    const int n = static_cast<int>(x.size());
    if (skip >= n - 8) {
        return 0.0;
    }
    double acc = 0.0;
    int c = 0;
    for (int i = skip; i < n; ++i) {
        acc += static_cast<double>(x[static_cast<std::size_t>(i)]) * static_cast<double>(x[static_cast<std::size_t>(i)]);
        ++c;
    }
    return std::sqrt(acc / static_cast<double>(c));
}

double tone_amp(const std::vector<float>& x, double freq, double sr, int skip) {
    const int n = static_cast<int>(x.size());
    if (skip >= n - 32) {
        return 0.0;
    }
    double re = 0.0;
    double im = 0.0;
    const int count = n - skip;
    const double w = 2.0 * 3.14159265358979323846 * freq / sr;
    for (int i = 0; i < count; ++i) {
        const double v = static_cast<double>(x[static_cast<std::size_t>(skip + i)]);
        re += v * std::cos(w * static_cast<double>(i));
        im += v * std::sin(w * static_cast<double>(i));
    }
    return 2.0 * std::hypot(re, im) / static_cast<double>(count);
}

struct Decimated {
    std::vector<float> y;
    int latency = 0;
    int os = 1;
};

Decimated run_os(double hostRate, int requested, double freqInternal, double durationS) {
    cuicui::ProcessSpec spec;
    spec.hostSampleRate = hostRate;
    spec.maxBlockSize = 2048;
    spec.oversampling = requested;
    cuicui::dsp::Oversampling os;
    os.prepare(spec);
    const int osf = spec.resolvedOversampling();
    const int nHost = std::max(256, static_cast<int>(std::lround(durationS * hostRate)));
    const int nInt = nHost * osf;
    const auto x = sine(nInt, freqInternal, hostRate * osf);
    std::vector<float> y(static_cast<std::size_t>(nHost), 0.0f);
    int done = 0;
    while (done < nHost) {
        const int n = std::min(spec.maxBlockSize, nHost - done);
        os.decimate(x.data() + done * osf, y.data() + done, n);
        done += n;
    }
    return {std::move(y), os.latencySamples(), osf};
}

} // namespace

TEST_CASE("decimator passband ripple <= 0.1 dB (20 Hz–20 kHz)") {
    const double freqs[] = {20.0, 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0, 20000.0};
    const double hosts[] = {44100.0, 48000.0};
    const int oss[] = {2, 4, 8};
    for (double host : hosts) {
        for (int requested : oss) {
            INFO("host=" << host << " os=" << requested);
            auto ref = run_os(host, requested, 1000.0, 0.5);
            const int skip = ref.latency * 2 + 64;
            const double a1k = tone_amp(ref.y, 1000.0, host, skip);
            REQUIRE(a1k > 0.1);
            for (double f : freqs) {
                if (f >= host * 0.5 - 50.0) {
                    continue;
                }
                const double dur = std::max(0.5, 16.0 / f);
                auto m = run_os(host, requested, f, dur);
                const double a = tone_amp(m.y, f, host, skip);
                const double db = 20.0 * std::log10(a / a1k);
                INFO("f=" << f << " db=" << db << " a=" << a << " a1k=" << a1k);
                REQUIRE(std::abs(db) <= 0.1);
            }
        }
    }
}

TEST_CASE("decimator aliases into 0-20 kHz are <= -90 dB vs 1 kHz") {
    struct Case {
        double host;
        int os;
        double fInt;
    };
    const Case cases[] = {
        {48000.0, 2, 30000.0}, {48000.0, 2, 40000.0}, {44100.0, 2, 30000.0},
        {48000.0, 4, 30000.0}, {48000.0, 4, 40000.0}, {48000.0, 4, 80000.0},
        {44100.0, 4, 30000.0}, {48000.0, 8, 40000.0}, {48000.0, 8, 80000.0},
    };
    for (const auto& c : cases) {
        INFO("host=" << c.host << " os=" << c.os << " f=" << c.fInt);
        auto ref = run_os(c.host, c.os, 1000.0, 0.15);
        auto alias = run_os(c.host, c.os, c.fInt, 0.15);
        const int skip = ref.latency + 128;
        const double r1k = rms(ref.y, skip);
        const double ra = rms(alias.y, skip);
        REQUIRE(r1k > 0.1);
        const double db = 20.0 * std::log10(std::max(ra, 1e-12) / r1k);
        REQUIRE(db <= -90.0);
    }
}

TEST_CASE("reported latency matches impulse-response peak") {
    const double hosts[] = {44100.0, 48000.0};
    const int oss[] = {2, 4, 8};
    for (double host : hosts) {
        for (int requested : oss) {
            cuicui::ProcessSpec spec;
            spec.hostSampleRate = host;
            spec.maxBlockSize = 4096;
            spec.oversampling = requested;
            cuicui::dsp::Oversampling os;
            os.prepare(spec);
            const int osf = spec.resolvedOversampling();
            const int nHost = 2048;
            std::vector<float> x(static_cast<std::size_t>(nHost * osf), 0.0f);
            x[0] = 1.0f;
            std::vector<float> y(static_cast<std::size_t>(nHost), 0.0f);
            os.decimate(x.data(), y.data(), nHost);
            int peak = 0;
            float best = 0.0f;
            for (int i = 0; i < nHost; ++i) {
                const float a = std::abs(y[static_cast<std::size_t>(i)]);
                if (a > best) {
                    best = a;
                    peak = i;
                }
            }
            INFO("host=" << host << " os=" << osf << " reported=" << os.latencySamples() << " peak=" << peak);
            REQUIRE(std::abs(peak - os.latencySamples()) <= 1);
            REQUIRE(best > 0.02f);
        }
    }
}
