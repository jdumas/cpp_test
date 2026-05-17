// SPDX-License-Identifier: MPL-2.0
//
// Variant-test version of the MWE: identify which kinds of callee body
// trigger Eigen's plain_array<> 16-byte alignment assertion on MSVC for
// Windows ARM64 in Debug mode, when a brace-initialized Matrix<double,
// 1, 2> prvalue is passed by value.

#include <cstdint>
#include <cstdio>
#include <vector>

static int g_assert_count = 0;
#define eigen_assert(x) \
    do { if (!(x)) ++g_assert_count; } while (0)

#include <Eigen/Core>

using Vec2d = Eigen::Matrix<double, 1, 2>;

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

// ----- variant callees ------------------------------------------------------
// V1: discard the parameter (parameter is dead in the function body).
NOINLINE void use_discard(Vec2d p) { (void)p; }

// V2: read one component into a volatile sink.
NOINLINE void use_read_one(Vec2d p)
{
    volatile double s = p(0);
    (void)s;
}

// V3: read both components.
NOINLINE void use_read_both(Vec2d p)
{
    volatile double s = p(0) + p(1);
    (void)s;
}

// V4: take the address of the parameter (no read of data).
NOINLINE void use_addr(Vec2d p)
{
    volatile auto* ptr = &p;
    (void)ptr;
}

// V5: forward by-value to another by-value function.
NOINLINE void use_forward_inner(Vec2d p) { (void)p; }
NOINLINE void use_forward(Vec2d p) { use_forward_inner(p); }

// V6: pass-by-const-ref control (no by-value parameter at all).
NOINLINE void use_ref(const Vec2d& p) { (void)p; }

// ----- caller --------------------------------------------------------------
template <typename F>
NOINLINE void run_variant(const char* name, F&& fn)
{
    const int before = g_assert_count;
    std::vector<double> a(8, 0.0);
    std::vector<int>    b(8, 0);
    for (int i = 0; i < 8; ++i) {
        const double t = double(i) / 8.0;
        fn({1.0 - t, 0.0});
    }
    const int delta = g_assert_count - before;
    std::printf("  %-20s asserts=%d  %s\n",
                name, delta, delta == 0 ? "PASS" : "FAIL");
    std::fflush(stdout);
}

int main()
{
    std::printf("== Eigen alignment-assert variant probe ==\n");
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
    std::printf("  alignof(Matrix<double,1,2>) = %zu (Eigen requires %d)\n\n",
                alignof(Vec2d),
                int(Eigen::internal::compute_default_alignment<double, 2>::value));

    run_variant("use_discard",   [](Vec2d p) { use_discard(p); });
    run_variant("use_read_one",  [](Vec2d p) { use_read_one(p); });
    run_variant("use_read_both", [](Vec2d p) { use_read_both(p); });
    run_variant("use_addr",      [](Vec2d p) { use_addr(p); });
    run_variant("use_forward",   [](Vec2d p) { use_forward(p); });
    run_variant("use_ref",       [](const Vec2d& p) { use_ref(p); });

    std::printf("\nTOTAL EIGEN_ASSERT count = %d  -- %s\n",
                g_assert_count, g_assert_count == 0 ? "PASS" : "FAIL");
    return g_assert_count == 0 ? 0 : 1;
}
