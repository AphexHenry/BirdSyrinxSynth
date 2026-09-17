#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace cuicui {

/// Max trachea delay line: OS 8 at 96 kHz, Lmax = 120 mm × σ_max. WP-B6.
inline constexpr int kVoiceDelayCapacity = 4096;
/// Half-band FIR delay: OS 8 has 3 stages; design length stays well under 512.
inline constexpr int kMaxHalfbandStages = 4;
inline constexpr int kMaxHalfbandTaps = 512;

struct RngState {
    std::uint32_t s0 = 1, s1 = 0, s2 = 0, s3 = 0;
    float spare = 0.0f;
    std::uint32_t has_spare = 0;
};

struct SmootherState {
    float y = 0.0f;
    float target = 0.0f;
};

struct DcState {
    double x1 = 0.0;
    double y1 = 0.0;
};

struct OnePoleState {
    double y = 0.0;
};

struct BiquadState {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;
};

struct HalfbandState {
    std::int32_t n = 0;
    std::int32_t w = 0;
    std::int32_t pair = 0;
    double z[kMaxHalfbandTaps]{};
};

/// POD snapshot of every BirdVoice state variable. memcpy-safe. Version 1 (WP-B6).
struct VoiceState {
    std::uint32_t version = 1;
    std::uint32_t reserved = 0;

    double sine_phase = 0.0;
    double saw_phase = 0.0;

    double labial_x_l = 0.0, labial_y_l = 0.0;
    double labial_x_r = 0.0, labial_y_r = 0.0;
    std::uint32_t labial_resets_l = 0, labial_resets_r = 0;

    DcState dc_out{}, dc_l{}, dc_r{};

    double resp_p = 0.0;

    OnePoleState tension_lp_l{}, tension_lp_r{}, trachea_lp{};
    BiquadState aspiration_bp{};
    RngState rng_voice{}, rng_tension_l{}, rng_tension_r{}, rng_aspiration{};

    double trachea_buf[kVoiceDelayCapacity]{};
    std::int32_t trachea_write = 0;
    std::int32_t trachea_size = 0;
    double trachea_last_out = 0.0;

    double uvt_q0 = 0.0, uvt_q1 = 0.0, uvt_q2 = 0.0;
    double uvt_last_u = 0.0;
    double uvt_flow = 0.0, uvt_dflow = 0.0;
    std::int32_t uvt_since_update = 0;
    std::uint32_t uvt_dirty = 0;
    std::uint32_t uvt_counter = 0;

    SmootherState out_gain{}, len{}, ref{}, loss{}, tgain{};
    SmootherState oec_damp{}, beak_damp{}, rad{}, asp{}, coupling{};
    SmootherState size{}, gamma{};

    std::int32_t os_stages = 0;
    HalfbandState hb[kMaxHalfbandStages]{};

    double f0_zc_prev = 0.0;
    std::int32_t f0_zc_count = 0;
    std::int32_t f0_zc_samples = 0;
    float f0_hz = 0.0f;

    std::uint32_t nonFiniteResets = 0;
};

static_assert(std::is_trivially_copyable_v<VoiceState>, "VoiceState must be memcpy-safe");

} // namespace cuicui
