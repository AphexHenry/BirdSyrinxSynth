#include "cuicui/io/wav_writer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct WavInfo {
    std::uint16_t format{};
    std::uint16_t channels{};
    std::uint32_t sample_rate{};
    std::uint32_t byte_rate{};
    std::uint16_t block_align{};
    std::uint16_t bits{};
    std::uint32_t data_size{};
    std::uint32_t riff_size{};
    std::uintmax_t file_size{};
};

WavInfo read_wav_info(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);

    auto rd4 = [&]() {
        char b[4];
        in.read(b, 4);
        REQUIRE(in);
        return std::string(b, 4);
    };
    auto u16 = [&]() {
        unsigned char b[2];
        in.read(reinterpret_cast<char*>(b), 2);
        REQUIRE(in);
        return static_cast<std::uint16_t>(b[0] | (b[1] << 8));
    };
    auto u32 = [&]() {
        unsigned char b[4];
        in.read(reinterpret_cast<char*>(b), 4);
        REQUIRE(in);
        return static_cast<std::uint32_t>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
    };

    WavInfo w;
    REQUIRE(rd4() == "RIFF");
    w.riff_size = u32();
    REQUIRE(rd4() == "WAVE");
    REQUIRE(rd4() == "fmt ");
    REQUIRE(u32() == 16);
    w.format = u16();
    w.channels = u16();
    w.sample_rate = u32();
    w.byte_rate = u32();
    w.block_align = u16();
    w.bits = u16();
    REQUIRE(rd4() == "data");
    w.data_size = u32();
    w.file_size = std::filesystem::file_size(path);
    return w;
}

} // namespace

TEST_CASE("WAV float32 header chunk sizes") {
    const auto dir = std::filesystem::temp_directory_path() / "cuicui_a1_wav";
    std::filesystem::create_directories(dir);
    const auto path = dir / "f32.wav";

    constexpr int sr = 48000;
    constexpr int n = 480;
    std::vector<float> x(n, 0.25f);
    cuicui::io::write_wav(path, x, 1, sr, cuicui::io::WavFormat::Float32);

    const WavInfo w = read_wav_info(path);
    REQUIRE(w.format == 3);
    REQUIRE(w.channels == 1);
    REQUIRE(w.sample_rate == 48000);
    REQUIRE(w.bits == 32);
    REQUIRE(w.block_align == 4);
    REQUIRE(w.byte_rate == 48000 * 4);
    REQUIRE(w.data_size == n * 4);
    REQUIRE(w.riff_size == 36 + w.data_size);
    REQUIRE(w.file_size == 8 + w.riff_size);
}

TEST_CASE("WAV PCM24 header chunk sizes") {
    const auto dir = std::filesystem::temp_directory_path() / "cuicui_a1_wav";
    std::filesystem::create_directories(dir);
    const auto path = dir / "pcm24.wav";

    constexpr int sr = 44100;
    constexpr int n = 100;
    std::vector<float> x(static_cast<size_t>(n * 2), 0.5f);
    cuicui::io::write_wav(path, x, 2, sr, cuicui::io::WavFormat::Pcm24);

    const WavInfo w = read_wav_info(path);
    REQUIRE(w.format == 1);
    REQUIRE(w.channels == 2);
    REQUIRE(w.sample_rate == 44100);
    REQUIRE(w.bits == 24);
    REQUIRE(w.block_align == 6);
    REQUIRE(w.byte_rate == 44100 * 6);
    REQUIRE(w.data_size == static_cast<std::uint32_t>(n * 6));
    REQUIRE(w.riff_size == 36 + w.data_size);
    REQUIRE(w.file_size == 8 + w.riff_size);
}
