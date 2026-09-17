#pragma once

#include "cuicui/generated/atlas_tables.hpp"

/// Compiled-in source atlas tables (WP-C2). See 01_research.md §7.4 and 02_architecture.md §9.3.
/// The binary/JSON copies live in assets/atlas/; this header is what the plugin uses (no file I/O).

namespace cuicui::control {

inline constexpr int kSourceAtlasNAlpha = generated::kAtlasNAlpha;
inline constexpr int kSourceAtlasNNu = generated::kAtlasNNu;
inline constexpr float kSourceAtlasGamma0 = generated::kAtlasGamma0;

} // namespace cuicui::control
