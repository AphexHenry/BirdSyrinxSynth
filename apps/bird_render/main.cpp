#include "cuicui/dsp/rng.hpp"
#include "cuicui/io/render_spec.hpp"
#include "cuicui/io/wav_writer.hpp"
#include "cuicui/model/test_tone.hpp"
#include "cuicui/version.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void usage(const char* argv0) {
    std::cerr << "bird_render " << cuicui::kVersion << " (" << cuicui::kGitHash << ")\n"
              << "usage: " << argv0
              << " <spec.render.json> -o <out.wav> [--seed N] [--format float32|pcm24]\n";
}

cuicui::io::WavFormat parse_format(const std::string& s) {
    if (s == "float32" || s == "f32") {
        return cuicui::io::WavFormat::Float32;
    }
    if (s == "pcm24" || s == "24") {
        return cuicui::io::WavFormat::Pcm24;
    }
    throw std::runtime_error("unknown --format (use float32 or pcm24)");
}

void normalize_peak(std::vector<float>& buf, double peak_dbfs) {
    float peak = 0.0f;
    for (float x : buf) {
        peak = std::max(peak, std::abs(x));
    }
    if (peak <= 0.0f) {
        return;
    }
    const float target = static_cast<float>(std::pow(10.0, peak_dbfs / 20.0));
    const float g = target / peak;
    for (float& x : buf) {
        x *= g;
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::filesystem::path spec_path;
        std::filesystem::path out_path;
        bool seed_override = false;
        std::uint64_t seed = 1;
        auto wav_format = cuicui::io::WavFormat::Float32;

        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "-h" || a == "--help") {
                usage(argv[0]);
                return 0;
            }
            if (a == "-o") {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    return 1;
                }
                out_path = argv[++i];
            } else if (a == "--seed") {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    return 1;
                }
                seed = static_cast<std::uint64_t>(std::stoull(argv[++i]));
                seed_override = true;
            } else if (a == "--format") {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    return 1;
                }
                wav_format = parse_format(argv[++i]);
            } else if (!a.empty() && a[0] == '-') {
                std::cerr << "unknown option: " << a << "\n";
                usage(argv[0]);
                return 1;
            } else if (spec_path.empty()) {
                spec_path = a;
            } else {
                std::cerr << "unexpected argument: " << a << "\n";
                usage(argv[0]);
                return 1;
            }
        }

        if (spec_path.empty() || out_path.empty()) {
            usage(argv[0]);
            return 1;
        }

        auto spec = cuicui::io::parse_render_spec_file(spec_path);
        if (seed_override) {
            spec.seed = seed;
        }

        const int sr = spec.sample_rate;
        const int n = static_cast<int>(std::llround(spec.duration_s * sr));
        if (n <= 0) {
            throw std::runtime_error("render length is zero");
        }

        std::vector<float> buf(static_cast<size_t>(n), 0.0f);
        constexpr double kDefaultFreq = 1000.0;
        constexpr double kDefaultGainDb = -12.0;
        const auto freq = spec.automationFor("test.freq_hz", kDefaultFreq);
        const auto gain = spec.automationFor("test.gain_db", kDefaultGainDb);

        if (spec.waveform == "noise") {
            cuicui::dsp::Rng rng(spec.seed);
            const double dt = 1.0 / static_cast<double>(sr);
            for (int i = 0; i < n; ++i) {
                const double t = static_cast<double>(i) * dt;
                const double amp = std::pow(10.0, gain.valueAt(t) / 20.0);
                buf[static_cast<size_t>(i)] =
                    static_cast<float>(amp * (2.0 * static_cast<double>(rng.uniform()) - 1.0));
            }
        } else {
            cuicui::model::TestTone tone;
            tone.prepare(static_cast<double>(sr));
            tone.process(buf.data(), n, freq, gain, 0.0);
        }

        if (spec.normalize_peak_dbfs) {
            normalize_peak(buf, *spec.normalize_peak_dbfs);
        }

        if (const auto parent = out_path.parent_path(); !parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        cuicui::io::write_wav(out_path, buf, 1, sr, wav_format);
        return 0;
    } catch (const cuicui::io::RenderSpecError& e) {
        std::cerr << e.what() << "\n";
        return e.exit_code;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
