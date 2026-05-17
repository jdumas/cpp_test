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
#include <vector>

static int g_misalign_count = 0;
#define eigen_assert(x) do { if (!(x)) ++g_misalign_count; } while (0)

#include <Eigen/Core>

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

using Vec2d = Eigen::Matrix<double, 1, 2>;

// Non-Eigen counterpart: a plain struct with the same storage shape
// (two doubles) and the same extended alignment as Vec2d, so the
// same braced-init-list initializes either type via the lambda
// parameter below.
struct alignas(16) S16 {
    double a, b;
    S16(double x, double y) : a(x), b(y) {
        eigen_assert((reinterpret_cast<std::uintptr_t>(this) & 0xFu) == 0);
    }
};

// Opaque sinks the lambda probes forward to. Marking them NOINLINE
// prevents the lambda bodies (and the run() instantiations) from
// collapsing to a no-op, which is what masked the codegen defect in
// earlier iterations of this MWE.
NOINLINE void sink_vec_by_value(Vec2d        p) { (void)p; }
NOINLINE void sink_vec_by_ref  (const Vec2d& p) { (void)p; }
NOINLINE void sink_s16_by_value(S16          p) { (void)p; }
NOINLINE void sink_s16_by_ref  (const S16&   p) { (void)p; }

// Templated driver: the prvalue temporary is materialized in this
// frame from a braced-init-list at the fn() call site, then used to
// initialize fn's parameter object. Two std::vector locals make the
// frame layout non-trivial so the temporary's stack slot lands at a
// representative offset rather than a happenstance 16-aligned one.
template <class F>
NOINLINE void run(const char* label, F&& fn)
{
    const int before = g_misalign_count;
    std::vector<double> pad_a(8, 0.0);
    std::vector<int>    pad_b(8, 0);
    for (int i = 0; i < 8; ++i) {
        const double t = double(i) / 8.0;
        fn({1.0 - t, 0.0});
    }
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
        [](Vec2d        p) { sink_vec_by_value(p); });
    run("Eigen::Matrix<double,1,2>  by const&",
        [](const Vec2d& p) { sink_vec_by_ref  (p); });
    run("alignas(16) struct S16     by value",
        [](S16          p) { sink_s16_by_value(p); });
    run("alignas(16) struct S16     by const&",
        [](const S16&   p) { sink_s16_by_ref  (p); });

    std::printf("\nTOTAL misalignments = %d  -- %s\n",
                g_misalign_count, g_misalign_count == 0 ? "PASS" : "FAIL");
    return g_misalign_count == 0 ? 0 : 1;
}
