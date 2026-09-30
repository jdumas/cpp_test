# Embree Emscripten SSE2 compile failure

This reproduces an Emscripten SIMD compilation regression at Embree commit
[`3d9cb89b9`](https://github.com/RenderKit/embree/commit/3d9cb89b9ea099c630e6272d37767e7dd4e78e74),
19 commits after the `v4.4.1` release. The released `v4.4.1` tag compiles this
MWE successfully.

The single translation unit, [`main.cpp`](main.cpp), includes Embree's internal
SSE2 headers. No Embree library build, application linking, or runtime is needed.

## Reproduce

Requirements: `git` and Emscripten 4.0.14 (`em++`).

```sh
git clone --branch embree-emscripten-repro --single-branch \
    https://github.com/jdumas/cpp_test.git
cd cpp_test

git clone https://github.com/RenderKit/embree.git embree
git -C embree checkout 3d9cb89b9ea099c630e6272d37767e7dd4e78e74

em++ -std=c++17 -msse -msse2 -msimd128 \
    -DEMBREE_TARGET_SSE2 \
    -I"$PWD/embree" \
    -c main.cpp -o mwe.o
```

Compilation is expected to fail with 11 errors, including:

```text
error: no member named 'm128i' in 'embree::vboolf_impl<4>'
```

Apply [`fix.patch`](fix.patch) and repeat the same compile command:

```sh
(cd embree && git apply ../fix.patch)
em++ -std=c++17 -msse -msse2 -msimd128 \
    -DEMBREE_TARGET_SSE2 \
    -I"$PWD/embree" \
    -c main.cpp -o mwe.o
```

Compilation now succeeds.

## Cause and fix

In `common/simd/vboolf4_sse2.h`, the `#if !defined(__EMSCRIPTEN__)` guard
excludes the named `m128i()` / `m128d()` accessors along with the implicit
conversion operators. However, Embree's SSE2 headers call `m128i()`
unconditionally: the boolean equality operator uses `a.m128i()`, and the
integer SIMD headers use `mask.m128i()` in masked loads.

The named accessors were introduced by the
[Windows ARM64 support change](https://github.com/RenderKit/embree/commit/b282335867a2cdcff518fe14a159defd8a7bd458).
A subsequent [SIMD wrapper fix](https://github.com/RenderKit/embree/commit/0bdf3c3a468bc9a8d0bd0ed9938849519ae12c2e)
kept them inside the Emscripten exclusion guard on the non-MSVC path.
The proposed patch moves the explicit accessors outside the guard while keeping
the implicit conversion operators excluded for Emscripten.

## GitHub Actions

The [workflow](.github/workflows/embree-emscripten.yml) runs on pushes to this
branch. It succeeds only if the unpatched compile fails with the expected
`m128i` diagnostic and the patched compile succeeds.

[Verified run](https://github.com/jdumas/cpp_test/actions/runs/36740131414)
using Ubuntu 24.04 and Emscripten 4.0.14.
