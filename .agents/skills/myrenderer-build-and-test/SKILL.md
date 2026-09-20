---
name: myrenderer-build-and-test
description: Build and verify MyRenderer changes with the smallest meaningful CMake, CTest, GPU-smoke, regression, and packaging checks. Use when implementing a change, diagnosing build/test failures, or reporting verification in this repository.
---

# MyRenderer build and test

Select checks from the changed behavior, not just the edited filenames. Read [references/verification-matrix.md](references/verification-matrix.md) for target routing.

## Reuse a compatible build tree

Inspect `CMakeCache.txt` before choosing an existing `build*` directory. Confirm its generator, configuration model, compiler, and `BUILD_TESTING` value. Do not delete or reconfigure an unrelated build tree to make a check pass.

Configured trees in this workspace, and what each one is for:

| Tree | Generator / config | Use it for |
| --- | --- | --- |
| `build-ci-msvc` | Visual Studio 17 2022, Release, `BUILD_TESTING=ON` | the default tree: CTest, `gpu-smoke`, `package`, most visual targets |
| `build-mingw` | MinGW Makefiles, Debug | GCC Debug build and CTest |
| `build-release` | MinGW Makefiles, Release | GCC Release build; the tree the Glass/Prism visual and benchmark targets were captured with |

`build` and `build-vs` contain no `CMakeCache.txt` and cannot be used.

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
8. Build one GCC tree before reporting "no new warnings" or handing work off (see below).

Use `--config Release` for multi-config MSVC builds and omit it for single-config MinGW builds. Always include `--output-on-failure` with CTest. Report the exact build directory, configuration, commands, and failures or skipped hardware-dependent checks.

Do not treat compilation alone as validation, and do not claim GPU validation from CPU-only tests.

## Cover both compilers

MSVC and GCC do not share their warning sets. GCC's `-Wextra` adds warnings MSVC never
reports, most notably `-Wmissing-field-initializers`: one warning per omitted member of an
aggregate built with a partial braced initializer, even when every omitted member has a
default member initializer and the code is correct. Adding one field to a widely
constructed struct such as `EditorCommand` can therefore turn a handful of construction
sites into hundreds of warnings on MinGW while the MSVC tree stays silent.

- Build one GCC tree (`build-mingw` or `build-release`) before claiming "no new warnings",
  whenever the change adds fields to widely constructed structs or aggregates, adds or
  removes functions, or changes includes.
- GCC only warns for translation units it actually recompiles, so an incremental build can
  hide the evidence. To audit a whole tree, rebuild it and count:
  `cmake --build build-release --clean-first --parallel`, then look at the `warning:` lines.
- `warning:` lines pointing into `_deps/` are third-party headers. Wrap those at the
  include site with `#pragma GCC diagnostic ignored` following
  `src/pathtracer/TextureSampling.cpp` and `src/pathtracer/EnvironmentSampling.cpp`; never
  silence project code that way, fix the cause instead.
- When a warning comes from a new pattern in project code, prefer removing the pattern
  (for example by giving the aggregate explicit constructors) over disabling the check for
  the whole target.
