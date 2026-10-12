// WP-R1: whistle voice, room, syllable engine.
#include "cuicui/control/syllable_bank.hpp"
#include "cuicui/control/syllable_engine.hpp"
#include "cuicui/dsp/room.hpp"
#include "cuicui/io/syllable_bank_io.hpp"
#include "cuicui/model/whistle_voice.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <string>
#include <vector>

using namespace cuicui;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kSr = 48000.0;

/// One phrase, one syllable: constant `hz` for `ms` milliseconds, amplitude 1 (0 at both ends).
control::SyllableBank steadyBank(double hz, int ms) {
    control::SyllableBankBuilder b;
    b.setInfo("steady", "test", "");
    b.setCentres({6900.0, 6900.0, 6900.0, 6900.0});
    b.beginPhrase(0.0);
    std::vector<double> f(static_cast<std::size_t>(ms), hz);
    std::vector<float> a(static_cast<std::size_t>(ms), 1.0f);
    a.front() = 0.0f;
    a.back() = 0.0f;
    const double c = 1200.0 * std::log2(hz / 440.0) + 6900.0;
    b.addSyllable(0, f, a, {c, c, c, c}, 0, ms, "steady");
    return std::move(b.bank);
}

/// Goertzel magnitude of x[from, from+n) at f (Hann window).
double toneLevel(const std::vector<float>& x, int from, int n, double f) {
    std::complex<double> acc{0.0, 0.0};
    for (int i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (n - 1));
        acc += w * x[static_cast<std::size_t>(from + i)] * std::polar(1.0, -2.0 * std::numbers::pi * f * i / kSr);
    }
    return std::abs(acc);
}

/// Frequency from interpolated zero crossings over x[from, to).
double zeroCrossingHz(const std::vector<float>& x, int from, int to) {
    double first = -1.0, last = -1.0;
    int count = 0;
    for (int i = from + 1; i < to; ++i) {
        const float a = x[static_cast<std::size_t>(i - 1)];
        const float b = x[static_cast<std::size_t>(i)];
        if (a < 0.0f && b >= 0.0f) {
            const double t = (i - 1) + static_cast<double>(-a / (b - a));
            if (first < 0.0) {
                first = t;
            } else {
                ++count;
            }
            last = t;
        }
    }
    return count > 0 ? count * kSr / (last - first) : 0.0;
}

std::vector<float> renderVoice(const control::SyllableBank& bank, double shift, bool harmonics, int delay,
                               int total) {
    model::WhistleVoice v;
    v.prepare(kSr);
    model::WhistleVoice::Colour c;
    c.harmonics = harmonics;
    v.start(bank, 0, shift, 0.5f, 0.0f, delay, c, 1);
    std::vector<float> l(static_cast<std::size_t>(total), 0.0f), r(static_cast<std::size_t>(total), 0.0f);
    for (int pos = 0; pos < total; pos += 100) {
        const int n = std::min(100, total - pos);
        v.render(l.data() + pos, r.data() + pos, n);
    }
    return l;
}

} // namespace

TEST_CASE("whistle voice: follows the syllable's pitch times the shift") {
    const auto bank = steadyBank(3000.0, 200);
    const auto y = renderVoice(bank, 100.0, false, 0, 10000);
    const double f = zeroCrossingHz(y, 1200, 8400);
    const double expect = 3000.0 * std::pow(2.0, 100.0 / 1200.0);
    CHECK(std::abs(1200.0 * std::log2(f / expect)) < 0.2); // cents
}

TEST_CASE("whistle voice: the bird's harmonics sit at -30 / -33 dB") {
    const auto bank = steadyBank(3200.0, 200);
    const auto y = renderVoice(bank, 0.0, true, 0, 9600);
    const double h1 = toneLevel(y, 1200, 4800, 3200.0);
    const double h2 = toneLevel(y, 1200, 4800, 6400.0);
    const double h3 = toneLevel(y, 1200, 4800, 9600.0);
    CHECK_THAT(20.0 * std::log10(h2 / h1), WithinAbs(-30.0, 0.1));
    CHECK_THAT(20.0 * std::log10(h3 / h1), WithinAbs(-33.0, 0.1));
    const auto pure = renderVoice(bank, 0.0, false, 0, 9600);
    CHECK(20.0 * std::log10(toneLevel(pure, 1200, 4800, 6400.0) / h1) < -90.0);
}

TEST_CASE("whistle voice: starts after its delay, fades in and out, lasts the syllable") {
    const auto bank = steadyBank(3000.0, 100);
    const auto y = renderVoice(bank, 0.0, true, 37, 8000);
    for (int i = 0; i < 37; ++i) {
        REQUIRE(y[static_cast<std::size_t>(i)] == 0.0f);
    }
    // 2.5 ms raised-cosine edge: tiny first sample, then up to full level
    CHECK(std::abs(y[37]) < 1e-3f);
    float peak = 0.0f;
    int lastNonZero = 0;
    for (int i = 0; i < 8000; ++i) {
        peak = std::max(peak, std::abs(y[static_cast<std::size_t>(i)]));
        if (y[static_cast<std::size_t>(i)] != 0.0f) {
            lastNonZero = i;
        }
    }
    CHECK(lastNonZero < 37 + 4800);
    CHECK(lastNonZero > 37 + 4800 - 60); // the contour's last ms has zero amplitude
    CHECK_THAT(peak, WithinAbs(0.5 * std::sqrt(0.5) * (1.0 + 0.0316 + 0.0224), 0.03)); // gain 0.5, centre pan
}

TEST_CASE("whistle voice: fadeOut stops within the fade length") {
    const auto bank = steadyBank(3000.0, 400);
    model::WhistleVoice v;
    v.prepare(kSr);
    v.start(bank, 0, 0.0, 0.5f, 0.0f, 0, {}, 1);
    std::vector<float> l(4800, 0.0f), r(4800, 0.0f);
    v.render(l.data(), r.data(), 2400);
    v.fadeOut(240);
    v.render(l.data() + 2400, r.data() + 2400, 2400);
    CHECK_FALSE(v.active());
    for (int i = 2400 + 241; i < 4800; ++i) {
        REQUIRE(l[static_cast<std::size_t>(i)] == 0.0f);
    }
}

TEST_CASE("room: wet output is 0.09 x the impulse response, sample-aligned, any block size") {
    for (int block : {1, 50, 128, 300, 1024}) {
        dsp::Room room;
        room.prepare(kSr);
        REQUIRE(room.blockSize() == 128);
        const int pre = 24000; // let the wet level settle at 0.09
        const int total = pre + room.irLength() + 2000;
        std::vector<float> inL(static_cast<std::size_t>(total), 0.0f), inR = inL, outL = inL, outR = inL;
        inL[static_cast<std::size_t>(pre)] = 1.0f;
        inR[static_cast<std::size_t>(pre)] = 1.0f;
        for (int pos = 0; pos < total; pos += block) {
            const int n = std::min(block, total - pos);
            room.process(inL.data() + pos, inR.data() + pos, outL.data() + pos, outR.data() + pos, n, true);
        }
        double err = 0.0, ref = 0.0;
        for (int ch = 0; ch < 2; ++ch) {
            const auto& ir = room.impulse(ch);
            const auto& out = ch == 0 ? outL : outR;
            for (int i = 0; i < room.irLength(); ++i) {
                const double e = 0.09 * ir[static_cast<std::size_t>(i)];
                err = std::max(err, std::abs(out[static_cast<std::size_t>(pre + i)] - e));
                ref = std::max(ref, std::abs(e));
            }
            for (int i = 0; i < pre; ++i) {
                REQUIRE(out[static_cast<std::size_t>(i)] == 0.0f);
            }
        }
        CHECK(err < 1e-4 * ref);
        // the first 4 ms of the response are silent (pre-delay; FFT round-off only), the next sample is not
        CHECK(std::abs(outL[static_cast<std::size_t>(pre + 191)]) < 1e-9f);
        CHECK(std::abs(outL[static_cast<std::size_t>(pre + 192)]) > 1e-6f);
    }
}

TEST_CASE("room: Web Audio level (normalize = true): energy gain is the calibrated value") {
    dsp::Room room;
    room.prepare(kSr);
    double e = 0.0;
    for (int ch = 0; ch < 2; ++ch) {
        for (float v : room.impulse(ch)) {
            e += static_cast<double>(v) * v;
        }
    }
    const int n = room.irLength();
    // RMS of the scaled IR = 0.00125 · 44100 / 48000 by construction
    CHECK_THAT(std::sqrt(e / (2.0 * n)), WithinAbs(0.00125 * 44100.0 / 48000.0, 1e-9));
}

TEST_CASE("syllable engine: a tap sounds exactly one syllable from the note-on sample") {
    const auto bank = io::loadSyllableBank(std::string(CUICUI_SOURCE_DIR) + "/assets/voicebanks/zf_22938/bank.json");
    control::SyllableEngine eng;
    eng.prepare(kSr);
    eng.setBank(&bank);
    eng.reset(1234);
    std::vector<control::SyllableLogEntry> log(64);
    eng.setLog(log.data(), 64);
    control::WhistleSettings s;
    s.room = false;
    const int total = 48000;
    std::vector<float> l(static_cast<std::size_t>(total)), r(static_cast<std::size_t>(total));
    control::PlayEvent on;
    on.type = control::PlayEvent::Type::NoteOn;
    on.note = 69;
    on.value = 0.8f;
    on.offset = 300;
    control::PlayEvent off = on;
    off.type = control::PlayEvent::Type::NoteOff;
    off.offset = 300 + 1440;
    const control::PlayEvent evs[2] = {on, off};
    eng.process(evs, 2, 4096, s, l.data(), r.data());
    eng.process(nullptr, 0, total - 4096, s, l.data() + 4096, r.data() + 4096);
    REQUIRE(eng.logCount() == 1);
    CHECK(log[0].sample == 300);
    const auto len = static_cast<int>(std::llround(bank.syllable(log[0].request.syllable).lengthMs * 48.0));
    int first = -1, last = -1;
    for (int i = 0; i < total; ++i) {
        if (l[static_cast<std::size_t>(i)] != 0.0f) {
            first = first < 0 ? i : first;
            last = i;
        }
    }
    CHECK(first >= 300);
    CHECK(first <= 300 + 2);
    CHECK(last < 300 + len);
    CHECK(last > 300 + len - 130);
}
