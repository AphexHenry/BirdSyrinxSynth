#include "cuicui/io/render_spec.hpp"
#include "cuicui/io/wav_writer.hpp"
#include "cuicui/model/test_tone.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

std::vector<float> render_tone(double freq_hz, double gain_db, double duration_s, int sr) {
    const int n = static_cast<int>(std::llround(duration_s * sr));
    std::vector<float> buf(static_cast<size_t>(n), 0.0f);
    cuicui::io::Automation freq;
    freq.fallback = freq_hz;
    cuicui::io::Automation gain;
    gain.fallback = gain_db;
    cuicui::model::TestTone tone;
    tone.prepare(static_cast<double>(sr));
    tone.process(buf.data(), n, freq, gain, 0.0);
    return buf;
}

double estimate_freq_zero_crossings(const std::vector<float>& x, double sr) {
    std::vector<double> zeros;
    zeros.reserve(x.size() / 16);
    for (size_t i = 1; i < x.size(); ++i) {
        if (x[i - 1] <= 0.0f && x[i] > 0.0f) {
            const double y0 = x[i - 1];
            const double y1 = x[i];
            const double frac = y0 / (y0 - y1);
            zeros.push_back((static_cast<double>(i - 1) + frac) / sr);
        }
    }
    REQUIRE(zeros.size() >= 2);
    return static_cast<double>(zeros.size() - 1) / (zeros.back() - zeros.front());
}

std::vector<unsigned char> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("test tone 1000 Hz at 48 kHz is within 0.1 Hz") {
    constexpr int sr = 48000;
    const auto buf = render_tone(1000.0, -6.0, 1.0, sr);
    const double f = estimate_freq_zero_crossings(buf, sr);
    REQUIRE_THAT(f, WithinAbs(1000.0, 0.1));
}

TEST_CASE("test tone is phase-continuous across process calls") {
    cuicui::io::Automation freq;
    freq.fallback = 1000.0;
    cuicui::io::Automation gain;
    gain.fallback = -6.0;
    cuicui::model::TestTone tone;
    tone.prepare(48000.0);

    std::vector<float> a(64);
    std::vector<float> b(64);
    tone.process(a.data(), 32, freq, gain, 0.0);
    tone.process(a.data() + 32, 32, freq, gain, 32.0 / 48000.0);

    cuicui::model::TestTone once;
    once.prepare(48000.0);
    once.process(b.data(), 64, freq, gain, 0.0);

    for (size_t i = 0; i < a.size(); ++i) {
        REQUIRE_THAT(a[i], WithinAbs(b[i], 1e-7));
    }
}

TEST_CASE("same render written twice is byte-identical") {
    const auto dir = std::filesystem::temp_directory_path() / "cuicui_a1_det";
    std::filesystem::create_directories(dir);
    const auto p1 = dir / "a.wav";
    const auto p2 = dir / "b.wav";

    const auto buf = render_tone(440.0, -12.0, 0.25, 48000);
    cuicui::io::write_wav(p1, buf, 1, 48000, cuicui::io::WavFormat::Float32);
    cuicui::io::write_wav(p2, buf, 1, 48000, cuicui::io::WavFormat::Float32);
    REQUIRE(read_bytes(p1) == read_bytes(p2));

    // Re-render from scratch: same spec → same samples → same file bytes.
    const auto buf2 = render_tone(440.0, -12.0, 0.25, 48000);
    REQUIRE(buf == buf2);
}

TEST_CASE("gain of 0 dB peak is near 1") {
    const auto buf = render_tone(1000.0, 0.0, 0.05, 48000);
    float peak = 0.0f;
    for (float x : buf) {
        peak = std::max(peak, std::abs(x));
    }
    REQUIRE_THAT(peak, WithinAbs(1.0f, 1e-5));
}
