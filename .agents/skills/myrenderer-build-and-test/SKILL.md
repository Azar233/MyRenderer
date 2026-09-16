---
name: myrenderer-build-and-test
description: Build and verify MyRenderer changes with the smallest meaningful CMake, CTest, GPU-smoke, regression, and packaging checks. Use when implementing a change, diagnosing build/test failures, or reporting verification in this repository.
---

# MyRenderer build and test

Select checks from the changed behavior, not just the edited filenames. Read [references/verification-matrix.md](references/verification-matrix.md) for target routing.

## Reuse a compatible build tree

Inspect `CMakeCache.txt` before choosing an existing `build*` directory. Confirm its generator, configuration model, compiler, and `BUILD_TESTING` value. Do not delete or reconfigure an unrelated build tree to make a check pass.

When a fresh MSVC tree is needed:

```powershell
cmake -S . -B build-ai-msvc -G "Visual Studio 17 2022" -A x64 -T host=x64 -DBUILD_TESTING=ON
cmake --build build-ai-msvc --config Release --parallel
ctest --test-dir build-ai-msvc -C Release --output-on-failure
```

For a single-config MinGW tree:

```powershell
cmake -S . -B build-ai-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-ai-mingw --parallel
ctest --test-dir build-ai-mingw --output-on-failure
```

The first configure fetches pinned dependencies and needs Git/Python/network access. Reuse an already configured tree when it is compatible.

## Verification order

1. Build the narrow owning target while iterating.
2. Run the focused CTest name or executable for the affected contract.
3. Run the full CTest suite after cross-cutting C++ or serialization changes.
4. Run `gpu-smoke` for renderer, shader, resource-lifetime, import-to-GPU, or application-loop changes. It creates a real OpenGL context and is not covered by CPU-only CTest.
5. Run the relevant visual target when rendered pixels or temporal behavior can change.
6. Run `renderer-regression-suite` only for broad renderer integration confidence; run `renderer-benchmark-suite` only when performance evidence is requested or performance-sensitive work is being accepted.
7. Build `package` only for release/package changes or an explicit release check.

Use `--config Release` for multi-config MSVC builds and omit it for single-config MinGW builds. Always include `--output-on-failure` with CTest. Report the exact build directory, configuration, commands, and failures or skipped hardware-dependent checks.

Do not treat compilation alone as validation, and do not claim GPU validation from CPU-only tests.
