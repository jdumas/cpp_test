// SPDX-License-Identifier: MPL-2.0
//
// Minimal reproducer for an MSVC ARM64 Debug codegen defect.
//
// A brace-initialized prvalue of a type with extended alignment
// (alignas(16) or stricter), materialized to pass a by-value function
// argument, is placed at an address that does not satisfy the type's
// alignment requirement.
//
// Per the C++ standard this is undefined behavior:
//
//   [class.temporary]/1 -- "A temporary object is an object created
//     ... when needed by the implementation to pass or return an
//     object of suitable type."
//   [basic.align]/1     -- "Stricter alignment can be requested using
//     the alignment-specifier ([dcl.align]). Attempting to create an
//     object in storage that does not meet the alignment requirements
//     of the object's type is undefined behavior."
//
// Both the Eigen and non-Eigen probes below fail on MSVC 14.44.35207
// (_MSC_VER=1944) targeting ARM64 in Debug; the by-const-ref controls
// pass. The defect is therefore type-agnostic and not Eigen-specific.

#include <cstdint>
#include <cstdio>

static int g_misalign_count = 0;
#define eigen_assert(x) do { if (!(x)) ++g_misalign_count; } while (0)

#include <Eigen/Core>

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

using Vec2d = Eigen::Matrix<double, 1, 2>;

struct alignas(16) S16 {
    std::uint64_t a, b;
    S16(std::uint64_t x, std::uint64_t y) : a(x), b(y) {
        if ((reinterpret_cast<std::uintptr_t>(this) & 0xFu) != 0)
            ++g_misalign_count;
    }
};

// By-value sinks: caller must materialize a temporary for the argument.
NOINLINE void sink_by_value(Vec2d p) { (void)p; }
NOINLINE void sink_by_value(S16   p) { (void)p; }

// By-const-ref controls: no parameter materialization in the caller.
NOINLINE void sink_by_ref(const Vec2d& p) { (void)p; }
NOINLINE void sink_by_ref(const S16&   p) { (void)p; }

template <class F>
NOINLINE void run(const char* label, F&& fn)
{
    const int before = g_misalign_count;
    for (int i = 0; i < 8; ++i)
        fn(i);
    const int delta = g_misalign_count - before;
    std::printf("  %-40s misalignments=%d  %s\n",
                label, delta, delta == 0 ? "PASS" : "FAIL");
    std::fflush(stdout);
}

int main()
{
    std::printf("== prvalue temporary alignment probe ==\n");
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
    std::printf("  alignof(Eigen::Matrix<double,1,2>) = %zu\n", alignof(Vec2d));
    std::printf("  alignof(S16)                       = %zu\n\n", alignof(S16));

    run("Eigen::Matrix<double,1,2>  by value",
        [](int i) { sink_by_value(Vec2d{1.0 - double(i) / 8.0, 0.0}); });
    run("Eigen::Matrix<double,1,2>  by const&",
        [](int i) { sink_by_ref  (Vec2d{1.0 - double(i) / 8.0, 0.0}); });
    run("alignas(16) struct S16     by value",
        [](int i) { sink_by_value(S16  {std::uint64_t(i), std::uint64_t(i) + 1}); });
    run("alignas(16) struct S16     by const&",
        [](int i) { sink_by_ref  (S16  {std::uint64_t(i), std::uint64_t(i) + 1}); });

    std::printf("\nTOTAL misalignments = %d  -- %s\n",
                g_misalign_count, g_misalign_count == 0 ? "PASS" : "FAIL");
    return g_misalign_count == 0 ? 0 : 1;
}
