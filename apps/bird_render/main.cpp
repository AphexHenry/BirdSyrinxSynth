#include "cuicui/control/score_player.hpp"
#include "cuicui/io/gesture_score.hpp"
#include "cuicui/io/offline_render.hpp"
#include "cuicui/io/param_dump.hpp"
#include "cuicui/io/render_spec.hpp"
#include "cuicui/io/wav_writer.hpp"
#include "cuicui/version.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void usage(const char* argv0) {
    std::cerr << "bird_render " << cuicui::kVersion << " (" << cuicui::kGitHash << ")\n"
              << "usage: " << argv0
              << " <spec.render.json | score.gscore.json> -o <out.wav> [--seed N] [--format float32|pcm24]\n"
              << "                    [--oversampling auto|1|2|4|8] [--smoothing on|off] [--sr HZ]\n"
              << "       " << argv0 << " --dump-params\n";
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
        cuicui::io::RenderOptions opt;
        bool dump_params = false;
        int sample_rate_override = 0;

        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "-h" || a == "--help") {
                usage(argv[0]);
                return 0;
            }
            if (a == "--dump-params") {
                dump_params = true;
            } else if (a == "-o") {
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
            } else if (a == "--oversampling") {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    return 1;
                }
                int os = 0;
                if (!cuicui::parseOversamplingToken(argv[++i], os)) {
                    throw std::runtime_error("unknown --oversampling (use auto, 1, 2, 4 or 8)");
                }
                opt.oversampling_override = os;
            } else if (a == "--smoothing") {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    return 1;
                }
                const std::string v = argv[++i];
                if (v == "off" || v == "0") {
                    opt.apply_smoothing = false;
                } else if (v == "on" || v == "1") {
                    opt.apply_smoothing = true;
                } else {
                    throw std::runtime_error("unknown --smoothing (use on or off)");
                }
            } else if (a == "--sr") {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    return 1;
                }
                sample_rate_override = std::stoi(argv[++i]);
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

        if (dump_params) {
            std::cout << cuicui::io::dump_params_json_string() << "\n";
            return 0;
        }

        if (spec_path.empty() || out_path.empty()) {
            usage(argv[0]);
            return 1;
        }

        std::ifstream in(spec_path);
        if (!in) {
            throw std::runtime_error("cannot open " + spec_path.string());
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        nlohmann::json peek;
        try {
            peek = nlohmann::json::parse(text);
        } catch (const nlohmann::json::parse_error& e) {
            throw std::runtime_error(std::string("invalid JSON: ") + e.what());
        }
        const std::string fmt = peek.contains("format") && peek.at("format").is_string()
                                    ? peek.at("format").get<std::string>()
                                    : "";

        std::vector<float> buf;
        int sr = 48000;
        if (fmt == "cuicui.gesture_score") {
            auto score = cuicui::io::parse_gesture_score_string(text);
            if (seed_override) {
                score.settings["engine.seed"] = static_cast<double>(seed);
            }
            if (score.ignore_gestures) {
                std::cerr << "warning: physio tracks present; gesture events ignored\n";
            }
            const int os = opt.oversampling_override >= 0 ? opt.oversampling_override : 0;
            const double rate = sample_rate_override > 0 ? static_cast<double>(sample_rate_override) : 48000.0;
            cuicui::control::ScorePlayer player;
            player.load(score);
            buf = player.render(rate, os);
            sr = static_cast<int>(std::lround(rate));
        } else {
            auto spec = cuicui::io::parse_render_spec_string(text);
            if (seed_override) {
                spec.seed = seed;
            }
            if (sample_rate_override > 0) {
                spec.sample_rate = sample_rate_override;
            }
            buf = cuicui::io::render_offline(spec, opt);
            sr = spec.sample_rate;
            if (spec.normalize_peak_dbfs) {
                normalize_peak(buf, *spec.normalize_peak_dbfs);
            }
        }

        if (const auto parent = out_path.parent_path(); !parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        cuicui::io::write_wav(out_path, buf, 1, sr, wav_format);
        return 0;
    } catch (const cuicui::io::GestureScoreError& e) {
        std::cerr << e.what() << "\n";
        return e.exit_code;
    } catch (const cuicui::io::RenderSpecError& e) {
        std::cerr << e.what() << "\n";
        return e.exit_code;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
