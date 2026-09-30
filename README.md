# Embree Emscripten SSE2 compile failure

This repository is a minimal reproducer for Embree 4.4.1 failing to compile
with Emscripten SIMD enabled. It has one translation unit (`main.cpp`) that
includes Embree's SSE2 header directly.

## Reproduce

Requirements: `git` and Emscripten 4.0.14 (`em++`).

```sh
git clone https://github.com/RenderKit/embree.git embree
git -C embree checkout 3d9cb89b9ea099c630e6272d37767e7dd4e78e74

em++ -std=c++17 -msse -msse2 -msimd128 \
    -DEMBREE_TARGET_SSE2 \
    -I"$PWD/embree" \
    -c main.cpp -o mwe.o
```

The unpatched compile fails with `no member named 'm128i' in
'embree::vboolf_impl<4>'`. The workflow verifies this expected failure, applies
[`fix.patch`](fix.patch), then verifies that the same translation unit compiles.

Run the check in GitHub Actions by pushing to `embree-emscripten-repro` or using
the workflow dispatch. The latest run is
[here](https://github.com/jdumas/cpp_test/actions/runs/36739556892); the unpatched
compile emitted 11 `m128i` errors and the patched compile passed.

## Draft report for Embree (not posted)

**Title:** Emscripten SSE2 build omits the `m128i()` accessor used by Embree SIMD headers

**Environment**

- Embree 4.4.1, commit `3d9cb89b9ea099c630e6272d37767e7dd4e78e74`
- Emscripten 4.0.14
- `em++ -std=c++17 -msse -msse2 -msimd128`

**Description**

In `common/simd/vboolf4_sse2.h`, the `#if !defined(__EMSCRIPTEN__)` guard
excludes both implicit `__m128i`/`__m128d` conversion operators and the named
`m128i()` / `m128d()` accessors. The implicit conversions can remain excluded,
but Embree's SSE2 headers call the named accessors unconditionally. For example,
the equality operator in `vboolf4_sse2.h` calls `a.m128i()`, while `vint4_sse2.h`
and `vuint4_sse2.h` call `mask.m128i()` in masked loads.

Compiling `main.cpp` fails with diagnostics such as:

```text
error: no member named 'm128i' in 'embree::vboolf_impl<4>'
```

This is a compile-time header failure; no application link or runtime is needed.
The GitHub Actions run above confirms both the unpatched failure and successful
compilation after applying `fix.patch`.

**Expected behavior**

Embree's SSE2 headers compile with Emscripten SIMD enabled.

**Observed behavior**

Compilation fails because `m128i()` is hidden by the Emscripten guard even
though Embree's own SSE2 headers call it.

**Suggested fix**

Keep the implicit conversion operators guarded by `!__EMSCRIPTEN__`, but declare
the explicit `m128i()` / `m128d()` accessors outside that guard. `fix.patch`
makes the MWE compile.
