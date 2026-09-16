#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>

namespace cuicui::test {

inline int g_alloc_count = 0;

inline void reset_alloc_count() noexcept { g_alloc_count = 0; }
inline int alloc_count() noexcept { return g_alloc_count; }

} // namespace cuicui::test

// Replacement new/delete — include this header from exactly one translation unit.
void* operator new(std::size_t n) {
    ++cuicui::test::g_alloc_count;
    void* p = std::malloc(n);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}

void* operator new[](std::size_t n) { return ::operator new(n); }

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

#if defined(__cpp_aligned_new)
void* operator new(std::size_t n, std::align_val_t a) {
    ++cuicui::test::g_alloc_count;
    void* p = nullptr;
    if (posix_memalign(&p, static_cast<std::size_t>(a), n) != 0) {
        throw std::bad_alloc();
    }
    return p;
}
void* operator new[](std::size_t n, std::align_val_t a) { return ::operator new(n, a); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
#endif
