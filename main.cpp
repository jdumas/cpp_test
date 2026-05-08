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
// Expected output: "EIGEN_ASSERT count = 0"
// Actual on win-arm64 Debug: "EIGEN_ASSERT count = 8"
//
// Same Eigen + same C++ source on win-arm64 Release, win-x64 (Debug
// and Release), linux-x64 and macOS-arm64: 0 asserts. The bug is
// specific to the MSVC ARM64 Debug code generator's handling of
// alignas-requiring temporaries materialized to bind to function
// parameters.

#include <cstdio>
#include <vector>

#include <Eigen/Core>

using Vec2d = Eigen::Matrix<double, 1, 2>; // alignof == 16, requires 16

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

// Take a fixed-size Eigen vector by value. (Same behaviour with a
// `const Vec2d&` parameter -- the misalignment is in the caller's
// temporary materialization for the brace-init prvalue, not in the
// parameter slot itself.)
NOINLINE void use(Vec2d p) { (void)p; }

// Caller with a non-trivial stack frame (a few std::vector locals and
// a loop with a runtime-computed expression).
NOINLINE void caller()
{
    std::vector<double> a(8, 0.0);
    std::vector<int>    b(8, 0);
    for (int i = 0; i < 8; ++i) {
        const double t = double(i) / 8.0;
        // Brace-initialized prvalue passed as a function argument.
        // MSVC ARM64 Debug allocates a stack temporary to materialize
        // the Matrix<double,1,2>, but does NOT 16-byte align it,
        // tripping plain_array<2,16>'s ctor assertion.
        use({1.0 - t, 0.0});
    }
}

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
    std::printf("  Eigen    = %d.%d.%d\n",
                EIGEN_WORLD_VERSION, EIGEN_MAJOR_VERSION, EIGEN_MINOR_VERSION);
    std::printf("  alignof(Matrix<double,1,2>) = %zu (Eigen requires %d)\n",
                alignof(Vec2d),
                int(Eigen::internal::compute_default_alignment<double, 2>::value));

    caller();

    return 0;
}
