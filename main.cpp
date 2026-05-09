// SPDX-License-Identifier: MPL-2.0
//
// Minimal reproducer for an MSVC ARM64 Debug codegen bug:
//
//     When the caller materializes a stack temporary to bind a
//     brace-initialized prvalue to a function argument, MSVC ARM64
//     Debug allocates an 8-byte-aligned slot regardless of the
//     destination type's alignas requirement. Plain stack locals of
//     the same type are aligned correctly; only the materialized
//     prvalue temporary is misaligned.
//
// Reproduces on win-arm64 Debug for any type with alignof >= 16 used
// as a brace-init prvalue argument -- both an Eigen fixed-size matrix
// (the original real-world site) and a plain non-Eigen alignas(16)
// struct. Does not reproduce on win-arm64 Release, win-x64 (Debug or
// Release), linux-x64, or macOS-arm64.
//
// Build:
//     cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
//     cmake --build build
//     build\probe.exe
//
// Expected on every configuration:        "total = 0  -- PASS".
// Actual on win-arm64 Debug:               every alignof=16 variant
//                                          (Eigen and non-Eigen alike)
//                                          reports asserts > 0 and FAILs.

#include <cstdint>
#include <cstdio>
#include <vector>

// Shared misalignment counter, incremented both by Eigen's assertion
// (overridden below) and by S16::check(). Counting instead of aborting
// lets the program run to completion and report a single per-variant
// PASS/FAIL line.
static int g_assert_count = 0;
#define eigen_assert(x) \
    do { if (!(x)) ++g_assert_count; } while (0)

#include <Eigen/Core>

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

// Eigen variants under test. The Eigen fixed-size matrices stamp
// alignas(16) on their internal plain_array<> when the byte size is
// a power of two, and assert in the array's constructor that `this`
// is correctly aligned. That assertion is what fires when a stack
// temporary is materialized at an under-aligned address.
using M_d2 = Eigen::Matrix<double,  1, 2>; // alignof = 16
using M_d3 = Eigen::Matrix<double,  1, 3>; // alignof =  8 (size not pow2)
using M_d4 = Eigen::Matrix<double,  1, 4>; // alignof = 16
using M_d5 = Eigen::Matrix<double,  1, 5>; // alignof =  8 (size not pow2)
using M_i2 = Eigen::Matrix<int64_t, 1, 2>; // alignof = 16

// Non-Eigen control: a plain alignas(16) trivial-ish aggregate with a
// constructor that checks `this`'s alignment. Demonstrates that the
// bug is not Eigen-specific -- any 16-byte-aligned type used as a
// brace-init prvalue argument is affected.
struct alignas(16) S16 {
    std::uint64_t a, b;
    S16() : a(0), b(0) { check(); }
    S16(std::uint64_t x, std::uint64_t y) : a(x), b(y) { check(); }
    void check() const {
        if ((reinterpret_cast<std::uintptr_t>(this) & 15) != 0) {
            ++g_assert_count;
        }
    }
};

// Sink: takes the value by-value, forcing the caller to materialize a
// stack temporary for a brace-init prvalue argument.
template <typename T>
NOINLINE void sink(T p) { (void)p; }

// One probe per variant. Each builds a non-trivial stack frame and:
//   1. Reports alignof(T) and the runtime alignment of a default-
//      constructed local T, to confirm that plain stack locals are
//      themselves correctly aligned (independent of the prvalue path).
//   2. Calls sink<T>({...}) 8 times with a brace-init prvalue whose
//      first element is a runtime-computed value (`1.0 - t`, or
//      `i` for integer variants). The runtime dependency is what
//      forces MSVC to actually materialize a stack temporary for the
//      prvalue -- pure literal initializers are folded directly into
//      immediate register loads and silently sidestep the bug.
//   3. Reports the assert delta attributable to this variant.
#define PROBE(NAME, T, ...)                                                   \
    NOINLINE static void NAME() {                                             \
        const int before = g_assert_count;                                    \
        T local;                                                              \
        const auto addr = reinterpret_cast<std::uintptr_t>(&local);           \
        std::vector<double> a(8, 0.0);                                        \
        std::vector<int>    b(8, 0);                                          \
        for (int i = 0; i < 8; ++i) {                                         \
            const double t = double(i) / 8.0; (void)t; (void)i;               \
            sink<T>({__VA_ARGS__});                                           \
        }                                                                     \
        const int delta = g_assert_count - before;                            \
        std::printf("  %-26s alignof=%2zu  &local%%align=%2llu  "             \
                    "asserts=%d  %s\n",                                       \
                    #T, alignof(T),                                           \
                    static_cast<unsigned long long>(addr % alignof(T)),       \
                    delta, delta ? "FAIL" : "PASS");                          \
    }

PROBE(probe_d2,  M_d2, 1.0 - t, 0.0)
PROBE(probe_d3,  M_d3, 1.0 - t, 0.0, 0.0)
PROBE(probe_d4,  M_d4, 1.0 - t, 0.0, 0.0, 0.0)
PROBE(probe_d5,  M_d5, 1.0 - t, 0.0, 0.0, 0.0, 0.0)
PROBE(probe_i2,  M_i2, int64_t(i), int64_t(0))
PROBE(probe_s16, S16,  std::uint64_t(i), std::uint64_t(0))

int main()
{
    std::printf("== prvalue-temporary alignment reproducer ==\n");
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

    // On win-arm64 Debug, every alignof=16 variant FAILs (Eigen and
    // non-Eigen alike), while alignof=8 variants PASS -- consistent
    // with MSVC allocating an 8-byte-aligned slot for the materialized
    // prvalue temporary regardless of the destination type's alignas
    // requirement. The &local%align column shows that plain stack
    // locals are correctly aligned in every case, isolating the bug
    // to the prvalue-materialization code path.
    probe_d2();
    probe_d3();
    probe_d4();
    probe_d5();
    probe_i2();
    probe_s16();

    std::printf("\ntotal asserts = %d  -- %s\n",
                g_assert_count, g_assert_count == 0 ? "PASS" : "FAIL");
    return g_assert_count == 0 ? 0 : 1;
}
