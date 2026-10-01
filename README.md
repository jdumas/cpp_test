# UBSan reports undefined behavior in triangle intersection and point-query dispatch

A single triangle, one `rtcIntersect1` call, and one `rtcPointQuery` call trigger
UndefinedBehaviorSanitizer diagnostics in Embree. Both queries return the correct
results, but UBSan flags:

1. **Incompatible function-pointer calls** in `kernels/common/accel.h`:
   - line 270 (`Intersectors::intersect`): the pointer type is
     `void (*)(Accel::Intersectors*, RTCRayHit&, RayQueryContext*)`, but the registered
     `BVHNIntersector1<...>::intersect` takes `const Accel::Intersectors*` and `RayHitK<1>&`.
   - line 258 (`Intersectors::pointQuery`): the pointer type is
     `bool (*)(Accel::Intersectors*, PointQueryK<1>*, PointQueryContext*)`, but the registered
     `BVHNIntersector1<...>::pointQuery` takes `const Accel::Intersectors*`.
2. **Invalid downcast** in `kernels/geometry/primitive.h:39`: `PrimitivePointQuery1`
   casts the triangle's `Geometry*` to `AccelSet*`, but the object is a `TriangleMeshISA`.

Calling a function through a pointer of an incompatible type and downcasting to a type
the object does not have are undefined behavior ([expr.call], [expr.static.cast]).

## Environment

- Embree `master` at `3d9cb89b9ea099c630e6272d37767e7dd4e78e74`
- Ubuntu 24.04, x86-64
- Clang 18.1.3 and GCC 13 (GCC does not implement the `function` check, so it reports
  only the downcast)
- Debug build, `-fsanitize=undefined`, static library, SSE2, internal tasking system;
  no TBB, ISPC, or SYCL

## Reproducer

This repository is a self-contained reproducer. [`main.cpp`](main.cpp) builds the scene and
runs the two queries; [`CMakeLists.txt`](CMakeLists.txt) builds Embree from source with UBSan.
It disables unused geometry types and ray packets to keep the build small, and uses `-O0`
for speed.

Requirements: Linux x86-64, Clang or GCC, CMake 3.20+, Ninja, and Git.

```sh
git clone --branch embree-ubsan-repro --single-branch https://github.com/jdumas/cpp_test.git
cd cpp_test
git clone https://github.com/RenderKit/embree.git embree
git -C embree checkout 3d9cb89b9ea099c630e6272d37767e7dd4e78e74
CC=clang-18 CXX=clang++-18 cmake -S . -B build/raw -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/raw --target probe
UBSAN_OPTIONS=halt_on_error=0:print_stacktrace=1 ./build/raw/probe
```

To build with GCC, use `CC=gcc-13 CXX=g++-13` instead.

## Observed output (Clang 18)

Template arguments are abbreviated; the full log is in the CI artifacts.

```text
embree/kernels/common/accel.h:270:9: runtime error: call to function embree::sse2::BVHNIntersector1<4, 1, false, ...>::intersect(embree::Accel::Intersectors const*, embree::RayHitK<1>&, embree::RayQueryContext*) through pointer to incorrect function type 'void (*)(embree::Accel::Intersectors *, RTCRayHit &, embree::RayQueryContext *)'
embree/kernels/common/accel.h:258:16: runtime error: call to function embree::sse2::BVHNIntersector1<4, 1, false, ...>::pointQuery(embree::Accel::Intersectors const*, embree::PointQueryK<1>*, embree::PointQueryContext*) through pointer to incorrect function type 'bool (*)(embree::Accel::Intersectors *, embree::PointQueryK<1> *, embree::PointQueryContext *)'
embree/kernels/bvh/../geometry/primitive.h:39:27: runtime error: downcast of address 0x... which does not point to an object of type 'AccelSet'
0x...: note: object is of type 'embree::sse2::TriangleMeshISA'
intersect geomID=0 distance=1.000000
point query callbacks=1
```

With `UBSAN_OPTIONS=halt_on_error=1`, the program aborts at the first diagnostic.

## Expected behavior

Valid intersection and point queries on a triangle mesh should run without UBSan
diagnostics.

## Possible fixes

- **Downcast:** `Geometry` already declares the `pointQuery` method that is called, so calling
  it through the original `Geometry*` instead of casting to `AccelSet*` should be enough.
- **Dispatch:** Make the function-pointer types match the signatures of the registered
  implementations, or register thin adapters with the exact pointer signature.

Would you accept a patch along these lines? If there is an officially supported way to build
Embree with UBSan, please let me know.

## Workaround

Disabling only the `vptr` check (and, on Clang, the `function` check) on Embree's kernel
targets avoids the diagnostics. All other UBSan checks remain enabled, and the application
remains fully instrumented. This hides the problem rather than fixing it. To try it:

```sh
CC=clang-18 CXX=clang++-18 cmake -S . -B build/control -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DREPRO_NARROW_EXCLUSIONS=ON
cmake --build build/control --target probe
ctest --test-dir build/control --verbose --no-tests=error
```

## Continuous integration

[The workflow](.github/workflows/embree-ubsan.yml) runs the reproducer with Clang 18 and
GCC 13 on Ubuntu 24.04. It checks that:

1. The unmodified build reports the downcast and, with Clang, both incompatible
   function-pointer calls.
2. The queries still return the expected results when UBSan recovery is enabled.
3. The unmodified build fails when `halt_on_error=1` is set.
4. The workaround build runs without diagnostics.
5. The compile commands match the intended instrumentation scope.

Each job uploads the logs, compile commands, and CMake cache as artifacts.
