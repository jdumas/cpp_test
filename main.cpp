// SPDX-License-Identifier: MPL-2.0
//
// Minimal reproducer for an MSVC ARM64 Debug codegen bug.
//
// Symptom
// -------
// When the caller materializes a stack temporary to pass a brace-
// initialized prvalue as a by-value function argument, MSVC ARM64
// Debug allocates an 8-byte-aligned slot regardless of the
// destination type's alignment requirement. Plain stack locals of
// the same type are aligned correctly; only the materialized prvalue
// temporary is misaligned. The bug is not Eigen-specific: any type
// with alignof >= 16 used as a brace-init prvalue argument is
// affected, as shown below by the two probes (one Eigen, one plain).
//
// Why this is a defect (C++23 references, draft N4950)
// ----------------------------------------------------
//   [class.temporary]/1  "A temporary object is an object created
//     ... when needed by the implementation to pass or return an
//     object of suitable type." A materialized prvalue temporary is
//     therefore an object in the sense of the standard.
//
//   [basic.align]/1      "Object types have alignment requirements
//     ... which place restrictions on the addresses at which an
//     object of that type may be allocated. ... An object type
//     imposes an alignment requirement on every object of that type;
//     stricter alignment can be requested using the alignment-
//     specifier ([dcl.align]). Attempting to create an object in
//     storage that does not meet the alignment requirements of the
//     object's type is undefined behavior."
//
// Taken together: the implementation is responsible for honoring the
// type's alignas requirement when it materializes a temporary for a
// function argument. MSVC ARM64 Debug does not, which causes every
// affected call site below to exhibit undefined behavior.
//
// Scope
// -----
// Reproduces on win-arm64 Debug with MSVC 14.44.35207 (_MSC_VER
// 1944). Does not reproduce on win-arm64 Release, win-x64 (Debug or
// Release), linux-x64, or macOS-arm64.
//
// Build
// -----
//     cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
//     cmake --build build
//     build\probe.exe
//
// Expected on every configuration:    "total asserts = 0  -- PASS".
// Actual on win-arm64 Debug:           both probes report asserts > 0
//                                      and the program returns 1.

#include <cstdint>
#include <cstdio>

// Eigen exposes its internal assertion as a configurable macro;
// override it to count misalignments instead of aborting so the
// program runs to completion and we get one PASS/FAIL line per probe.
static int g_assert_count = 0;
#define eigen_assert(x) do { if (!(x)) ++g_assert_count; } while (0)

#include <Eigen/Core>

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

// --- Non-Eigen probe type ----------------------------------------------
// Plain alignas(16) aggregate whose constructor checks `this`'s
// alignment. Demonstrates that the bug is not Eigen-specific.
struct alignas(16) S16 {
    std::uint64_t a, b;
    S16(std::uint64_t x, std::uint64_t y) : a(x), b(y) {
        if ((reinterpret_cast<std::uintptr_t>(this) & 15) != 0) {
            ++g_assert_count;
        }
    }
};

// --- Sinks --------------------------------------------------------------
// Pass by value to force the caller to materialize a stack temporary
// for the brace-init prvalue argument. NOINLINE keeps the call site
// honest even if some pass elides it.
using EigenMat = Eigen::Matrix<double, 1, 2>;  // alignof = 16

NOINLINE static void sink_eigen(EigenMat) {}
NOINLINE static void sink_s16(S16) {}

// --- Probes -------------------------------------------------------------
// Each probe:
//   1. Default-constructs a plain stack local of the type and reports
//      `&local % alignof(T)`, confirming that locals are aligned
//      correctly and isolating the defect to the prvalue path.
//   2. Calls its sink 8 times with a brace-init prvalue whose first
//      element is a runtime-dependent value. The runtime dependency
//      forces MSVC to actually materialize a stack temporary -- pure
//      literal initializers are folded into immediate register loads
//      and silently sidestep the bug.
//   3. Reports the per-probe assert delta.

NOINLINE static void probe_eigen() {
    const int before = g_assert_count;
    EigenMat local;
    const auto local_misalign =
        reinterpret_cast<std::uintptr_t>(&local) % alignof(EigenMat);
    for (int i = 0; i < 8; ++i) {
        const double t = double(i) / 8.0;
        sink_eigen({1.0 - t, 0.0});
    }
    const int delta = g_assert_count - before;
    std::printf("  Eigen::Matrix<double,1,2>  alignof=%zu  "
                "&local%%align=%llu  asserts=%d  %s\n",
                alignof(EigenMat),
                static_cast<unsigned long long>(local_misalign),
                delta, delta ? "FAIL" : "PASS");
}

NOINLINE static void probe_s16() {
    const int before = g_assert_count;
    S16 local{0, 0};
    const auto local_misalign =
        reinterpret_cast<std::uintptr_t>(&local) % alignof(S16);
    for (int i = 0; i < 8; ++i) {
        sink_s16({std::uint64_t(i), 0});
    }
    const int delta = g_assert_count - before;
    std::printf("  alignas(16) struct S16     alignof=%zu  "
                "&local%%align=%llu  asserts=%d  %s\n",
                alignof(S16),
                static_cast<unsigned long long>(local_misalign),
                delta, delta ? "FAIL" : "PASS");
}

int main() {
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
                EIGEN_WORLD_VERSION, EIGEN_MAJOR_VERSION,
                EIGEN_MINOR_VERSION);

    probe_eigen();
    probe_s16();

    std::printf("\ntotal asserts = %d  -- %s\n",
                g_assert_count, g_assert_count == 0 ? "PASS" : "FAIL");
    return g_assert_count == 0 ? 0 : 1;
}
