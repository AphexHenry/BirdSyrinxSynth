#include "cuicui/model/bird_voice.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

struct Scenario {
    const char* name;
    int voices;
    bool two_sides;
    bool noise;
    bool fast_beta;
};

void run_scenario(double hostRate, const Scenario& sc, int block, int repeats) {
    cuicui::ProcessSpec spec;
    spec.hostSampleRate = hostRate;
    spec.maxBlockSize = block;
    spec.oversampling = 0;

    std::vector<cuicui::BirdVoice> voices(static_cast<std::size_t>(sc.voices));
    for (auto& v : voices) {
        v.prepare(spec);
        v.setSource(cuicui::VoiceSource::Labial);
        cuicui::VoiceSettings st;
        st.noise_aspiration = sc.noise ? 0.02 : 0.0;
        st.seed = 1;
        v.setSettings(st);
        v.reset(1);
        v.primeLabial(cuicui::model::kAlphaRest, 0.3);
        v.setOutGainRamp(0.0f, 0.0f);
        v.setOutGainSmoothingMs(0.0);
    }

    const int nBlocks = std::max(1, static_cast<int>(std::lround(2.0 * hostRate / static_cast<double>(block))));
    std::vector<float> buf(static_cast<std::size_t>(block), 0.0f);
    cuicui::ControlFrame from, to;
    from.resp_pressure = to.resp_pressure = 0.55f;
    from.src_L_beta = 0.2f;
    to.src_L_beta = sc.fast_beta ? 0.6f : 0.2f;
    from.src_R_beta = 0.35f;
    to.src_R_beta = sc.fast_beta ? 0.7f : 0.35f;
    from.src_L_gate = to.src_L_gate = 1.0f;
    from.src_R_gate = to.src_R_gate = sc.two_sides ? 1.0f : 0.0f;
    from.noise_beta_sigma = to.noise_beta_sigma = sc.noise ? 0.01f : 0.0f;

    // Warm-up.
    for (auto& v : voices) {
        v.process(from, to, buf.data(), block);
    }

    double worstBlockS = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < repeats; ++r) {
        for (int b = 0; b < nBlocks; ++b) {
            const auto b0 = std::chrono::steady_clock::now();
            for (auto& v : voices) {
                v.process(from, to, buf.data(), block);
            }
            const auto b1 = std::chrono::steady_clock::now();
            const double dt = std::chrono::duration<double>(b1 - b0).count();
            worstBlockS = std::max(worstBlockS, dt);
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(t1 - t0).count();
    const double audioS = static_cast<double>(repeats) * 2.0;
    const double rt = elapsed / audioS;
    const double corePct = rt * 100.0;
    const double blockDur = static_cast<double>(block) / hostRate;
    const double worstFrac = (blockDur > 0.0) ? (worstBlockS / blockDur) : 0.0;
    std::printf("%8.1f  %-28s  voices=%2d  RT=%.4f  core%%=%.3f  worst=%.1f%% of block\n", hostRate, sc.name,
                sc.voices, rt, corePct, worstFrac * 100.0);
}

} // namespace

int main() {
    std::printf("bird_bench — Syrinx Tier-1 voice (block=256, OS auto, 2 s × repeats)\n");
    const Scenario scenarios[] = {
        {"default note", 1, false, false, false},
        {"two-side + noise + β", 1, true, true, true},
        {"16 voices", 16, true, true, true},
    };
    const double rates[] = {44100.0, 48000.0, 96000.0};
    for (double rate : rates) {
        for (const auto& sc : scenarios) {
            const int repeats = (sc.voices >= 16) ? 2 : 4;
            run_scenario(rate, sc, 256, repeats);
        }
    }
    return 0;
}
