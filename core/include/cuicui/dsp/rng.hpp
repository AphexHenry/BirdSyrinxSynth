#pragma once

#include <cmath>
#include <cstdint>

namespace cuicui::dsp {

/// xoshiro128+ PRNG (Blackman & Vigna). Seedable, no allocation after construction.
/// `uniform()` → [0, 1); `gaussian()` → N(0, 1) via polar Box–Muller.
class Rng {
public:
    explicit Rng(std::uint64_t seed = 1) noexcept { setSeed(seed); }

    void setSeed(std::uint64_t seed) noexcept {
        // splitmix64 to fill four 32-bit words; a zero state is illegal for xoshiro.
        auto splitmix = [](std::uint64_t& x) noexcept {
            x += 0x9e3779b97f4a7c15ULL;
            std::uint64_t z = x;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            return z ^ (z >> 31);
        };
        std::uint64_t x = seed;
        const std::uint64_t a = splitmix(x);
        const std::uint64_t b = splitmix(x);
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
