#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cuicui::model {

/// Air density (kg/m³) and sound speed (m/s) for the upper tract. 01_research.md §4.7.3.
inline constexpr double kUvtRho = 1.15;
inline constexpr double kUvtSoundSpeed = 350.0;
inline constexpr double kGlottisLengthM = 0.002;
inline constexpr double kGlottisRadiusM = 0.0004;
inline constexpr double kBeakLengthM = 0.008;
inline constexpr double kBeakArea0M2 = 1.0e-6;
inline constexpr double kBeakArea1M2 = 20.0e-6;

/// Output-stage calibration so the reference note peaks near −6 dBFS (WP-B3 §4.4).
/// Tuned at 48 kHz, α=0.2, β=0.2, trachea defaults, f_H=3000 Hz, gape=0.5.
/// Output-stage calibration so the reference note peaks near −6 dBFS (WP-B3 §4.4).
/// Tuned at 48 kHz, α=0.2, β=0.2, trachea defaults, f_H=3000 Hz, gape=0.5, ρ=1.
/// kUvtFlowCal is chosen so ρ=0 RMS matches ρ=1 RMS.
inline constexpr double kUvtFlowCal = 25649535.0;
inline constexpr double kUvtRadCal = 1534.636;

struct UvtOut {
    double flow = 0.0;
    double dflow = 0.0;
};

struct UvtPhysical {
    double Lg = 0.0;
    double Lb = 0.0;
    double Leq = 0.0;
    double Ch = 0.0;
    double Rh = 0.0;
    double Rb = 0.0;
    double fH = 0.0;
    double volumeM3 = 0.0;
};

/// Geometry and Helmholtz compliance from musician parameters. Lengths ∝ σ, areas ∝ σ²,
/// f_H ∝ 1/σ (σ = sizeScale). 01_research.md §4.7.3 / §4.9.
[[nodiscard]] inline UvtPhysical uvtPhysical(double oecFreqHz, double oecDamping, double beakGape,
                                             double beakDamping, double sizeScale = 1.0) noexcept {
    const double sigma = std::clamp(sizeScale, 0.25, 4.0);
    const double fHctrl = std::clamp(oecFreqHz, 300.0, 10000.0);
    const double zh = std::clamp(oecDamping, 0.01, 1.0);
    const double zb = std::clamp(beakDamping, 0.01, 1.0);
    const double g = std::clamp(beakGape, 0.0, 1.0);

    const double lgLen = kGlottisLengthM * sigma;
    const double lgR = kGlottisRadiusM * sigma;
    const double Ag = std::numbers::pi * lgR * lgR;
    const double Lg = kUvtRho * lgLen / Ag;

    const double Ab = (kBeakArea0M2 + g * (kBeakArea1M2 - kBeakArea0M2)) * sigma * sigma;
    const double Lb = kUvtRho * (kBeakLengthM * sigma) / Ab;
    const double Leq = (Lg * Lb) / (Lg + Lb);

    const double fH = fHctrl / sigma;
    const double wH = 2.0 * std::numbers::pi * fH;
    const double Ch = 1.0 / (wH * wH * Leq);
    const double Z0 = std::sqrt(Leq / Ch);

    UvtPhysical p;
    p.Lg = Lg;
    p.Lb = Lb;
    p.Leq = Leq;
    p.Ch = Ch;
    p.Rh = zh * Z0;
    p.Rb = zb * Z0;
    p.fH = fH;
    p.volumeM3 = Ch * kUvtRho * kUvtSoundSpeed * kUvtSoundSpeed;
    return p;
}

[[nodiscard]] inline bool invert3(const double a[3][3], double out[3][3]) noexcept {
    const double a00 = a[0][0], a01 = a[0][1], a02 = a[0][2];
    const double a10 = a[1][0], a11 = a[1][1], a12 = a[1][2];
    const double a20 = a[2][0], a21 = a[2][1], a22 = a[2][2];
    const double det = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) +
                       a02 * (a10 * a21 - a11 * a20);
    if (!(std::abs(det) > 1.0e-30) || !std::isfinite(det)) {
        return false;
    }
    const double inv = 1.0 / det;
    out[0][0] = (a11 * a22 - a12 * a21) * inv;
    out[0][1] = (a02 * a21 - a01 * a22) * inv;
    out[0][2] = (a01 * a12 - a02 * a11) * inv;
    out[1][0] = (a12 * a20 - a10 * a22) * inv;
    out[1][1] = (a00 * a22 - a02 * a20) * inv;
    out[1][2] = (a02 * a10 - a00 * a12) * inv;
    out[2][0] = (a10 * a21 - a11 * a20) * inv;
    out[2][1] = (a01 * a20 - a00 * a21) * inv;
    out[2][2] = (a00 * a11 - a01 * a10) * inv;
    return true;
}

/// Glottis–OEC–beak 3rd-order circuit, trapezoidal discretisation. 01_research.md §4.7.2.
class UpperVocalTract {
public:
    void prepare(double internalRate) noexcept {
        rate_ = internalRate > 0.0 ? internalRate : 192000.0;
        h_ = 1.0 / rate_;
        since_update_ = kUpdatePeriod;
        rebuild();
        reset();
    }

    void reset() noexcept {
        q_[0] = q_[1] = q_[2] = 0.0;
        last_u_ = 0.0;
        last_ = {};
        since_update_ = kUpdatePeriod;
    }

    /// Steady DC for constant tracheal pressure (flow = p_t / R_b, radiation 0).
    void prime(double ptSteady) noexcept {
        rebuild();
        const double pt = ptSteady;
        const double ib = (std::abs(phys_.Rb) > 1.0e-18) ? (pt / phys_.Rb) : 0.0;
        q_[0] = ib;
        q_[1] = pt;
        q_[2] = ib;
        last_u_ = pt;
        last_.flow = ib;
        last_.dflow = 0.0;
        since_update_ = kUpdatePeriod;
    }

    void setParams(double oecFreqHz, double oecDamping, double beakGape, double beakDamping,
                   double sizeScale = 1.0) noexcept {
        const double df = std::abs(oecFreqHz - oec_freq_);
        const double dzh = std::abs(oecDamping - oec_damp_);
        const double dg = std::abs(beakGape - gape_);
        const double dzb = std::abs(beakDamping - beak_damp_);
        const double ds = std::abs(sizeScale - size_);
        oec_freq_ = oecFreqHz;
        oec_damp_ = oecDamping;
        gape_ = beakGape;
        beak_damp_ = beakDamping;
        size_ = sizeScale;
        const bool changed = df > kParamEps || dzh > kParamEps || dg > kParamEps || dzb > kParamEps ||
                             ds > kParamEps;
        if (!changed) {
            return;
        }
        dirty_ = true;
        if (since_update_ >= kUpdatePeriod) {
            rebuild();
            dirty_ = false;
            since_update_ = 0;
        }
    }

    [[nodiscard]] UvtOut process(double pt) noexcept {
        if (dirty_ && since_update_ >= kUpdatePeriod) {
            rebuild();
            dirty_ = false;
            since_update_ = 0;
        }
        const double uSum = last_u_ + pt;
        last_u_ = pt;
        double tmp[3];
        const double inj = 0.5 * h_ * b0_ * uSum;
        tmp[0] = N_[0][0] * q_[0] + N_[0][1] * q_[1] + N_[0][2] * q_[2] + inj;
        tmp[1] = N_[1][0] * q_[0] + N_[1][1] * q_[1] + N_[1][2] * q_[2];
        tmp[2] = N_[2][0] * q_[0] + N_[2][1] * q_[1] + N_[2][2] * q_[2];
        double nq[3];
        nq[0] = Minv_[0][0] * tmp[0] + Minv_[0][1] * tmp[1] + Minv_[0][2] * tmp[2];
        nq[1] = Minv_[1][0] * tmp[0] + Minv_[1][1] * tmp[1] + Minv_[1][2] * tmp[2];
        nq[2] = Minv_[2][0] * tmp[0] + Minv_[2][1] * tmp[1] + Minv_[2][2] * tmp[2];
        q_[0] = nq[0];
        q_[1] = nq[1];
        q_[2] = nq[2];
        last_.flow = q_[2];
        last_.dflow = (q_[1] + phys_.Rh * q_[0] - (phys_.Rh + phys_.Rb) * q_[2]) / phys_.Lb;
        ++since_update_;
        if (since_update_ > kUpdatePeriod) {
            since_update_ = kUpdatePeriod;
        }
        return last_;
    }

    [[nodiscard]] double impliedOecVolumeCm3() const noexcept { return phys_.volumeM3 * 1.0e6; }
    [[nodiscard]] UvtOut last() const noexcept { return last_; }
    [[nodiscard]] const UvtPhysical& physical() const noexcept { return phys_; }
    [[nodiscard]] double internalRate() const noexcept { return rate_; }

private:
    static constexpr int kUpdatePeriod = 8;
    static constexpr double kParamEps = 1.0e-12;

    void rebuild() noexcept {
        phys_ = uvtPhysical(oec_freq_, oec_damp_, gape_, beak_damp_, size_);
        const double Lg = phys_.Lg;
        const double Lb = phys_.Lb;
        const double Ch = phys_.Ch;
        const double Rh = phys_.Rh;
        const double Rb = phys_.Rb;
        // A, 01_research.md §4.7.2 / WP-B3 §4.2
        double A[3][3] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
        A[0][0] = -Rh / Lg;
        A[0][1] = -1.0 / Lg;
        A[0][2] = Rh / Lg;
        A[1][0] = 1.0 / Ch;
        A[1][1] = 0.0;
        A[1][2] = -1.0 / Ch;
        A[2][0] = Rh / Lb;
        A[2][1] = 1.0 / Lb;
        A[2][2] = -(Rh + Rb) / Lb;
        b0_ = 1.0 / Lg;
        const double hh = 0.5 * h_;
        double M[3][3];
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                const double d = (i == j) ? 1.0 : 0.0;
                M[i][j] = d - hh * A[i][j];
                N_[i][j] = d + hh * A[i][j];
            }
        }
        double inv[3][3];
        if (invert3(M, inv)) {
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    Minv_[i][j] = inv[i][j];
                }
            }
        }
    }

    double rate_ = 192000.0;
    double h_ = 1.0 / 192000.0;
    double oec_freq_ = 3000.0;
    double oec_damp_ = 0.1;
    double gape_ = 0.5;
    double beak_damp_ = 0.3;
    double size_ = 1.0;
    UvtPhysical phys_{};
    double Minv_[3][3]{};
    double N_[3][3]{};
    double b0_ = 0.0;
    double q_[3]{};
    double last_u_ = 0.0;
    UvtOut last_{};
    int since_update_ = kUpdatePeriod;
    bool dirty_ = true;
};

[[nodiscard]] inline double uvtMix(double flow, double dflow, double radiation) noexcept {
    const double rho = std::clamp(radiation, 0.0, 1.0);
    return (1.0 - rho) * kUvtFlowCal * flow + rho * kUvtRadCal * dflow;
}

} // namespace cuicui::model
