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
the workflow dispatch. A successful run is available
[here](https://github.com/jdumas/cpp_test/actions/runs/36738938946).

[`BUG_REPORT.md`](BUG_REPORT.md) is a draft for the Embree maintainers; it has
not been posted.
