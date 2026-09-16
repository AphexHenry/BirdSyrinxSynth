#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cuicui::io {

enum class WavFormat { Float32, Pcm24 };

/// Minimal WAV writer (offline only). RIFF/WAVE, little-endian.
/// Float32 = IEEE format 3; Pcm24 = integer format 1, 24-bit packed.
inline void write_wav(const std::filesystem::path& path,
                      const float* samples,
                      int num_frames,
                      int num_channels,
                      int sample_rate,
                      WavFormat format = WavFormat::Float32) {
    if (samples == nullptr || num_frames < 0 || num_channels < 1 || num_channels > 2 || sample_rate <= 0) {
        throw std::invalid_argument("write_wav: invalid arguments");
    }

    const bool ieee = format == WavFormat::Float32;
    const std::uint16_t audio_format = ieee ? 3 : 1;
    const std::uint16_t bits = ieee ? 32 : 24;
    const std::uint16_t block_align = static_cast<std::uint16_t>(num_channels * (bits / 8));
    const std::uint32_t byte_rate = static_cast<std::uint32_t>(sample_rate) * block_align;
    const std::uint32_t data_size = static_cast<std::uint32_t>(num_frames) * block_align;
    const std::uint32_t fmt_size = 16;
    const std::uint32_t riff_size = 4 + (8 + fmt_size) + (8 + data_size);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("write_wav: cannot open " + path.string());
    }

    auto wr4 = [&](const char* s) { out.write(s, 4); };
    auto wr_u16 = [&](std::uint16_t v) {
        char b[2]{static_cast<char>(v & 0xff), static_cast<char>((v >> 8) & 0xff)};
        out.write(b, 2);
    };
    auto wr_u32 = [&](std::uint32_t v) {
        char b[4]{static_cast<char>(v & 0xff),
                  static_cast<char>((v >> 8) & 0xff),
                  static_cast<char>((v >> 16) & 0xff),
                  static_cast<char>((v >> 24) & 0xff)};
        out.write(b, 4);
    };

    wr4("RIFF");
    wr_u32(riff_size);
    wr4("WAVE");
    wr4("fmt ");
    wr_u32(fmt_size);
    wr_u16(audio_format);
    wr_u16(static_cast<std::uint16_t>(num_channels));
    wr_u32(static_cast<std::uint32_t>(sample_rate));
    wr_u32(byte_rate);
    wr_u16(block_align);
    wr_u16(bits);
    wr4("data");
    wr_u32(data_size);

    const int n = num_frames * num_channels;
    if (ieee) {
        for (int i = 0; i < n; ++i) {
            float x = samples[i];
            char b[4];
            std::memcpy(b, &x, 4);
            out.write(b, 4);
        }
    } else {
        constexpr float scale = 8388607.0f; // 2^23 - 1
        for (int i = 0; i < n; ++i) {
            float x = std::clamp(samples[i], -1.0f, 1.0f);
            int v = static_cast<int>(std::lround(x * scale));
            v = std::clamp(v, -8388608, 8388607);
            char b[3]{static_cast<char>(v & 0xff),
                      static_cast<char>((v >> 8) & 0xff),
                      static_cast<char>((v >> 16) & 0xff)};
            out.write(b, 3);
        }
    }

    if (!out) {
        throw std::runtime_error("write_wav: write failed for " + path.string());
    }
}

inline void write_wav(const std::filesystem::path& path,
                      const std::vector<float>& samples,
                      int num_channels,
                      int sample_rate,
                      WavFormat format = WavFormat::Float32) {
    if (num_channels < 1 || (samples.size() % static_cast<size_t>(num_channels)) != 0) {
        throw std::invalid_argument("write_wav: sample count is not a multiple of channel count");
    }
    const int frames = static_cast<int>(samples.size() / static_cast<size_t>(num_channels));
    write_wav(path, samples.data(), frames, num_channels, sample_rate, format);
}

} // namespace cuicui::io
