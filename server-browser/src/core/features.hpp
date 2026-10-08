#pragma once

#include <cstdio>
#include <cstdlib>
#include <version>

#if SB_HAS_REFLECTION
#include <meta>
#endif

namespace sb::detail {
[[noreturn]] inline void assert_fail(const char* expr, const char* file, int line) noexcept {
    std::fprintf(stderr, "assertion failed: %s (%s:%d)\n", expr, file, line);
    std::abort();
}
}  // namespace sb::detail

// SB_PRE goes in declarator position; SB_ASSERT is a statement. Both map to P2900 contracts when available.
#if SB_HAS_CONTRACTS
#define SB_PRE(...) pre(__VA_ARGS__)
#define SB_ASSERT(...) contract_assert(__VA_ARGS__)
#else
#define SB_PRE(...)
#define SB_ASSERT(...) \
    ((__VA_ARGS__) ? static_cast<void>(0) : ::sb::detail::assert_fail(#__VA_ARGS__, __FILE__, __LINE__))
#endif

#ifndef NDEBUG
#define SB_DASSERT(...) SB_ASSERT(__VA_ARGS__)
#else
#define SB_DASSERT(...) static_cast<void>(0)
#endif

// The launcher builds these headers with MSVC.
#if defined(_MSC_VER) && !defined(__clang__)
#define SB_LIKELY(x) (x)
#define SB_UNLIKELY(x) (x)
#define SB_NOINLINE __declspec(noinline)
#define SB_ALWAYS_INLINE __forceinline
#else
#define SB_LIKELY(x) __builtin_expect(!!(x), 1)
#define SB_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define SB_NOINLINE __attribute__((noinline))
#define SB_ALWAYS_INLINE inline __attribute__((always_inline))
#endif

namespace sb {
inline constexpr std::size_t kCacheLine = 64;
}
