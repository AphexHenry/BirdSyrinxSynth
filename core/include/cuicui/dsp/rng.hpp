#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cuicui::dsp {

/// SplitMix64. Advances `state` and returns a 64-bit mix. Used to derive independent
/// noise-stream seeds from `engine.seed` (01_research.md §6.4).
[[nodiscard]] inline std::uint64_t splitmix64(std::uint64_t& state) noexcept {
    state += 0x9e3779b97f4a7c15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

/// Stream indices reserved so adding R later does not change L or aspiration seeds.
inline constexpr int kStreamTensionL = 0;
inline constexpr int kStreamTensionR = 1;
inline constexpr int kStreamAspiration = 2;

/// Independent 64-bit seed for stream `index` (0 = L, 1 = R, 2 = aspiration).
[[nodiscard]] inline std::uint64_t deriveStreamSeed(std::uint64_t master, int index) noexcept {
    std::uint64_t x = master == 0 ? 1ULL : master;
    std::uint64_t out = 1;
    const int n = std::max(0, index);
    for (int i = 0; i <= n; ++i) {
        out = splitmix64(x);
    }
    return out == 0 ? 1ULL : out;
}

/// xoshiro128+ PRNG (Blackman & Vigna). Seedable, no allocation after construction.
/// `uniform()` → [0, 1); `gaussian()` → N(0, 1) via polar Box–Muller.
class Rng {
public:
    explicit Rng(std::uint64_t seed = 1) noexcept { setSeed(seed); }

    void setSeed(std::uint64_t seed) noexcept {
        // splitmix64 to fill four 32-bit words; a zero state is illegal for xoshiro.
        std::uint64_t x = seed == 0 ? 1ULL : seed;
        const std::uint64_t a = splitmix64(x);
        const std::uint64_t b = splitmix64(x);
        s_[0] = static_cast<std::uint32_t>(a);
        s_[1] = static_cast<std::uint32_t>(a >> 32);
        s_[2] = static_cast<std::uint32_t>(b);
        s_[3] = static_cast<std::uint32_t>(b >> 32);
        if (s_[0] == 0 && s_[1] == 0 && s_[2] == 0 && s_[3] == 0) {
            s_[0] = 1;
        }
        has_spare_ = false;
        spare_ = 0.0f;
    }

    [[nodiscard]] std::uint32_t next() noexcept {
        const std::uint32_t result = s_[0] + s_[3];
        const std::uint32_t t = s_[1] << 9;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 11);
        return result;
    }

    /// Uniform in [0, 1).
    [[nodiscard]] float uniform() noexcept {
        return static_cast<float>(next() >> 8) * 0x1.0p-24f;
    }

    /// Standard normal. Real-time safe (no allocation).
    [[nodiscard]] float gaussian() noexcept {
        if (has_spare_) {
            has_spare_ = false;
            return spare_;
        }
        float u = 0.0f;
        float v = 0.0f;
        float s = 0.0f;
        do {
            u = 2.0f * uniform() - 1.0f;
            v = 2.0f * uniform() - 1.0f;
            s = u * u + v * v;
        } while (s == 0.0f || s >= 1.0f);
        const float mul = std::sqrt(-2.0f * std::log(s) / s);
        spare_ = v * mul;
        has_spare_ = true;
        return u * mul;
    }

private:
    static std::uint32_t rotl(std::uint32_t x, int k) noexcept { return (x << k) | (x >> (32 - k)); }

    std::uint32_t s_[4]{};
    float spare_ = 0.0f;
    bool has_spare_ = false;
};

} // namespace cuicui::dsp
