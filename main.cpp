// SPDX-License-Identifier: MPL-2.0
//
// Minimal reproducer: Eigen's plain_array<> alignment assertion fires
// for a stack-allocated, fixed-size Matrix<double, 1, 2> when MSVC for
// Windows ARM64 (Debug) materializes a temporary from a brace-initialized
// function argument without 16-byte aligning it.
//
// Build:
//     cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
//     cmake --build build
//     build\probe.exe
//
// Expected on every configuration: "EIGEN_ASSERT total = 0".
// Actual on win-arm64 Debug: only the HFA-eligible variants
// (Matrix<double,1,{2,3,4}>) FAIL; Matrix<double,1,5> and
// Matrix<int64_t,1,2> PASS. This pattern is consistent with MSVC's
// ARM64 Debug codegen skipping alignas(16) on the stack temporary
// it materializes for HFA-classified prvalue arguments
// (https://learn.microsoft.com/en-us/cpp/build/arm64-windows-abi-conventions).

#include <cstdint>
#include <cstdio>
#include <vector>

// Override eigen_assert to count failures instead of aborting, so the
// program runs to completion and we can report a single PASS/FAIL line.
static int g_assert_count = 0;
#define eigen_assert(x) \
    do { if (!(x)) ++g_assert_count; } while (0)

#include <Eigen/Core>

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

// Type aliases for each variant under test. Per the Windows ARM64 ABI,
// a struct/array of 1..4 identical floating-point members is a
// Homogeneous Floating-point Aggregate (HFA) and is passed in v0..v7
// rather than via a stack slot. Variants marked HFA below are the ones
// the ABI would route through FP registers; the >4-element double
// variant and the integer variant are NOT HFAs.
using M_d2 = Eigen::Matrix<double,  1, 2>; // HFA(2 doubles)
using M_d3 = Eigen::Matrix<double,  1, 3>; // HFA(3 doubles)
using M_d4 = Eigen::Matrix<double,  1, 4>; // HFA(4 doubles)
using M_d5 = Eigen::Matrix<double,  1, 5>; // NOT HFA (>4 fields)
using M_i2 = Eigen::Matrix<int64_t, 1, 2>; // NOT HFA (not FP)

// Sink: takes a fixed-size Eigen vector by value, forcing the caller
// to materialize a temporary for a brace-init prvalue argument.
template <typename M>
NOINLINE void sink(M p) { (void)p; }

// One probe per variant. Each builds a non-trivial stack frame
// (matching the original caller()'s shape: a couple of std::vector
// locals + an 8-iter loop) and:
//   1. Reports alignof(M) and the runtime alignment of a default-
//      constructed local M, to check whether plain stack locals are
//      themselves misaligned (independent of any prvalue temporary).
//   2. Calls sink<M>({...}) 8 times with a brace-init prvalue whose
//      first element is the runtime-computed value `1.0 - t` (resp.
//      `int64_t(i)` for the integer variant). The runtime dependency
//      is what forces MSVC to actually materialize a stack temporary
//      for the prvalue, rather than folding it into immediate FP
//      register loads -- and that is exactly the construct that
//      misbehaves on win-arm64 Debug for HFA-eligible Eigen matrices.
//   3. Reports the assert delta attributable to this variant.
#define PROBE(NAME, M, HFA_TAG, ...)                                          \
    NOINLINE static void NAME() {                                             \
        const int before = g_assert_count;                                    \
        M local;                                                              \
        const auto addr = reinterpret_cast<std::uintptr_t>(&local);           \
        std::vector<double> a(8, 0.0);                                        \
        std::vector<int>    b(8, 0);                                          \
        for (int i = 0; i < 8; ++i) {                                         \
            const double t = double(i) / 8.0; (void)t;                        \
            sink<M>({__VA_ARGS__});                                           \
        }                                                                     \
        const int delta = g_assert_count - before;                            \
        std::printf("  %-26s %-7s alignof=%2zu  &local%%align=%2llu  "        \
                    "asserts=%d  %s\n",                                       \
                    #M, HFA_TAG, alignof(M),                                  \
                    static_cast<unsigned long long>(addr % alignof(M)),       \
                    delta, delta ? "FAIL" : "PASS");                          \
    }

PROBE(probe_d2, M_d2, "HFA",     1.0 - t, 0.0)
PROBE(probe_d3, M_d3, "HFA",     1.0 - t, 0.0, 0.0)
PROBE(probe_d4, M_d4, "HFA",     1.0 - t, 0.0, 0.0, 0.0)
PROBE(probe_d5, M_d5, "non-HFA", 1.0 - t, 0.0, 0.0, 0.0, 0.0)
PROBE(probe_i2, M_i2, "non-HFA", int64_t(i), int64_t(0))

int main()
{
    std::printf("== Eigen alignment-assert reproducer ==\n");
#if defined(_MSC_VER)
    std::printf("  _MSC_VER = %d\n", _MSC_VER);
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
    std::printf("  arch     = arm64\n");
#elif defined(_M_X64) || defined(__x86_64__)
    std::printf("  arch     = x86_64\n");
#endif
    std::printf("  Eigen    = %d.%d.%d\n\n",
                EIGEN_WORLD_VERSION, EIGEN_MAJOR_VERSION, EIGEN_MINOR_VERSION);

    // HFA hypothesis: if the ARM64 Windows ABI's HFA classification is
    // what causes MSVC Debug to skip alignas(16) on the materialized
    // temporary, then probe_d2/d3/d4 should FAIL and probe_d5/i2 should
    // PASS on win-arm64 Debug. On every other configuration, all five
    // should PASS.
    probe_d2();
    probe_d3();
    probe_d4();
    probe_d5();
    probe_i2();

    std::printf("\nEIGEN_ASSERT total = %d  -- %s\n",
                g_assert_count, g_assert_count == 0 ? "PASS" : "FAIL");
    return g_assert_count == 0 ? 0 : 1;
}
