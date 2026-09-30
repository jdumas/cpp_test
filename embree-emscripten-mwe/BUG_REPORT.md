# Embree issue report draft (not posted)

**Title:** Emscripten SSE2 build omits the `m128i()` accessor used by Embree SIMD headers

**Environment**

- Embree 4.4.1, commit `3d9cb89b9ea099c630e6272d37767e7dd4e78e74`
- Emscripten 4.0.14 (GitHub Actions reproduction)
- `em++ -std=c++17 -msse -msse2 -msimd128`

**Description**

In `common/simd/vboolf4_sse2.h`, the `#if !defined(__EMSCRIPTEN__)` guard
excludes both implicit `__m128i`/`__m128d` conversion operators and the named
`m128i()` / `m128d()` accessors. The implicit conversions can remain excluded,
but Embree's SSE2 headers call the named accessors unconditionally. For example,
the equality operator in `vboolf4_sse2.h` calls `a.m128i()`, while `vint4_sse2.h`
and `vuint4_sse2.h` call `mask.m128i()` in masked loads.

Compiling the attached `mwe.cpp` with Emscripten fails with diagnostics such as:

```text
error: no member named 'm128i' in 'embree::vboolf_impl<4>'
```

The failure occurs while compiling the header; no application link or runtime
is needed.

**Reproduction**

See the adjacent `README.md` and the `Embree Emscripten repro` GitHub Actions
workflow. The workflow checks out the exact Embree revision, verifies that the
unpatched source fails with the expected diagnostic, then applies the attached
`fix.patch` and verifies that the translation unit compiles.

The reproduction passed in [GitHub Actions run 36738728650](https://github.com/jdumas/cpp_test/actions/runs/36738728650):
the unpatched compile emitted 11 `m128i` errors and the patched compile passed.

**Expected behavior**

Embree's SSE2 headers compile with Emscripten SIMD enabled.

**Observed behavior**

Compilation fails because `m128i()` is hidden by the Emscripten guard even
though Embree's own SSE2 headers call it.

**Suggested fix**

Keep the implicit conversion operators guarded by `!__EMSCRIPTEN__`, but declare
the explicit `m128i()` / `m128d()` accessors outside that guard. The attached
patch makes the MWE compile.
