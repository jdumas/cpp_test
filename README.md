# Embree UBSan dispatch reproducer

This branch uses the entire repository as a minimal native Embree reproducer.
It creates one triangle, intersects one ray, and performs one point query.
There is no Lagrange, Eigen, TBB, ISPC, or GPU dependency.

## Findings

With `-fsanitize=undefined` and Clang 18, unmodified Embree reports:

- `kernels/common/accel.h:270`: intersection through an incompatible function-pointer type.
- `kernels/common/accel.h:258`: point query through an incompatible function-pointer type.
- `kernels/geometry/primitive.h:39`: downcast of `TriangleMeshISA` to `AccelSet`.

GCC 13 reports the downcast but does not implement Clang's function-pointer check.
The recoverable run still returns the expected intersection and one point-query callback.
A fail-fast run exits unsuccessfully on the diagnostic.

The workflow checks these revisions:

| Revision | Purpose |
|---|---|
| `v4.4.0` | Lagrange's current source dependency |
| `v4.4.1` | Updated release |
| `3d9cb89b9ea099c630e6272d37767e7dd4e78e74` | Post-4.4.1 update candidate |

## Local reproduction

Requirements: Linux x86-64, GCC or Clang, CMake 3.20+, Ninja, and Git.

```sh
git clone --branch embree-ubsan-repro --single-branch https://github.com/jdumas/cpp_test.git
cd cpp_test
git clone https://github.com/RenderKit/embree.git embree
git -C embree checkout 3d9cb89b9ea099c630e6272d37767e7dd4e78e74
CC=clang-18 CXX=clang++-18 cmake -S . -B build/raw -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/raw --target probe -j 2
UBSAN_OPTIONS=halt_on_error=0:print_stacktrace=1 ./build/raw/probe
```

The output includes the diagnostics above, followed by:

```text
intersect geomID=0 distance=1.000000
point query callbacks=1
```

To reproduce the failure instead of recovering:

```sh
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./build/raw/probe
```

For GCC, replace `CC=clang-18 CXX=clang++-18` with `CC=gcc-13 CXX=g++-13`.
You can substitute either release tag in the `git checkout` command.

## Narrow-exclusion control

This is a diagnostic workaround, not a source fix. Enable it in a separate build:

```sh
CC=clang-18 CXX=clang++-18 cmake -S . -B build/control -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DREPRO_NARROW_EXCLUSIONS=ON
cmake --build build/control --target probe -j 2
ctest --test-dir build/control --verbose --no-tests=error
```

The control disables only `vptr` and, on Clang, `function`, privately on Embree's
kernel targets. All other UBSan checks remain enabled. The application and auxiliary
Embree libraries remain fully instrumented. The control must run without diagnostics.

## GitHub Actions

[The workflow](.github/workflows/embree-ubsan.yml) runs on pushes to this branch
and supports manual dispatch. It tests all three revisions with GCC 13 and Clang 18
on Ubuntu 24.04.

A green job means all of the following were verified:

1. The unmodified build reports the exact expected downcast and, on Clang, both
   incompatible function-pointer calls.
2. The valid query still returns the expected results with sanitizer recovery enabled.
3. Fail-fast UBSan rejects the unmodified executable.
4. The narrow-exclusion control passes without diagnostics.
5. Compiler-command checks confirm the intended instrumentation boundary.

Each job uploads diagnostic logs, resolved revisions, compiler commands, and CMake
configuration as artifacts. Infrastructure/build failures are not counted as successful
reproductions.

## Potential upstream changes

For `primitive.h`, `Geometry` already declares the `pointQuery` method being called;
using the original `Geometry*` avoids the invalid `AccelSet*` downcast.
For dispatch, matching function-pointer signatures or typed adapters would avoid
incompatible calls without disabling sanitizer checks.
