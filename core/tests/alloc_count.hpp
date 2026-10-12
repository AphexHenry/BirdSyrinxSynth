#pragma once

// The allocation counter itself. alloc_counter.hpp (one translation unit per test binary) replaces
// operator new to increment it; other test files include only this header to read it.
namespace cuicui::test {

inline int g_alloc_count = 0;

inline void reset_alloc_count() noexcept { g_alloc_count = 0; }
inline int alloc_count() noexcept { return g_alloc_count; }

} // namespace cuicui::test
