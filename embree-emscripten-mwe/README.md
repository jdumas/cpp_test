# Embree 4 Emscripten MWE

This minimal translation unit reproduces an SSE2 header compilation failure
with Embree 4.4.1 and Emscripten. The GitHub Actions workflow first verifies
that the unpatched source fails with missing `m128i()` diagnostics, then applies
`fix.patch` and verifies compilation succeeds.

The workflow pins Embree to commit `3d9cb89b9ea099c630e6272d37767e7dd4e78e74`
and uses Emscripten 4.0.14.

To reproduce locally with `em++` available:

```sh
git clone https://github.com/RenderKit/embree.git embree
git -C embree checkout 3d9cb89b9ea099c630e6272d37767e7dd4e78e74

em++ -std=c++17 -msse -msse2 -msimd128 \
    -DEMBREE_TARGET_SSE2 \
    -I"$PWD/embree" \
    -c embree-emscripten-mwe/mwe.cpp -o mwe.o
```

The unpatched compile is expected to fail with `no member named 'm128i'` in
`embree::vboolf_impl<4>`. Applying `fix.patch` before compiling makes it pass.
