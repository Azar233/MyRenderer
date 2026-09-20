# Verification matrix

Use the narrowest matching row, then expand when the change crosses contracts.

| Change area | Focused CTest / target | Add when relevant |
|---|---|---|
| OBJ, DAE, glTF/GLB import; materials; textures; skin data | `ctest -R asset-import` | `gpu-smoke` when GPU upload or draw behavior changes |
| Scene hierarchy, transforms, visibility, duplication/deletion | `ctest -R "scene-graph|scene-draw-list"` | `foundation-visual-regression` for rendered entity/motion changes |
| `.myscene` schema, defaults, paths, load/save | `ctest -R scene-document-repeat-load` | full CTest; `path-tracing-raster-comparison` if shared scene semantics changed |
| Ray intersections, BVH/TLAS, snapshot data, BSDF | `ctest -R path-tracing-foundation` | `path-tracing-instancing-benchmark` only for acceleration work |
| Progressive accumulation, scheduling, cancellation, AOV/output | `ctest -R path-tracing-progressive` | `path-tracing-regression`; `path-tracing-adaptive-benchmark` for sampling work |
| Prism CPU optics or spectral mesh generation | `ctest -R prism-optics` | `prism5-visual-regression`; `prism5-benchmark` for performance-sensitive work |
| Shader loading/reload failure safety | `shader-hot-reload-smoke` | affected visual target and `gpu-smoke` |
| Forward/Deferred, G-Buffer, PBR lighting | `deferred-visual-regression` | `local-lights-visual-regression`, `gpu-smoke`, affected benchmark |
| SSAO, TAA, motion vectors, post-processing | `screen-space-visual-regression` | `foundation-visual-regression`, `gpu-smoke` |
| Skinning/animation | `ctest -R asset-import`; `skinning-visual-regression` | `gpu-smoke`; `skinning-benchmark` |
| Glass thickness/refraction/caustics | matching `glass2c-`, `glass3-`, or `glass4-visual-regression` target | matching benchmark; `gpu-smoke` |
| Stylized shading or outline | `stylized-acceptance` | `deferred-visual-regression`, `screen-space-visual-regression` if shared code changed |
| Broad pass/state/resource integration | full CTest; `gpu-smoke`; `renderer-regression-suite` | `renderer-benchmark-suite` only for performance acceptance |

## Compiler coverage

The rows above are compiler-independent. These are not: MSVC and GCC report different
warning sets, so a green `build-ci-msvc` does not prove a clean GCC build.

| Change area | Add this check |
|---|---|
| Widely constructed aggregates or structs that gain fields (`EditorSession.h` / `EditorCommand` and its payloads, `BatchFrameResult`, module payloads) | `cmake --build build-mingw --parallel`; read the `warning:` lines |
| New or removed functions, changed includes, new translation units | same GCC build as above |
| Any "no new warnings" statement or hand-off | one GCC tree, because MSVC does not implement `-Wmissing-field-initializers` |
| Release-only or optimisation-dependent warnings | `cmake --build build-release --clean-first --parallel` |
| Behaviour that can differ per compiler (libstdc++ vs MSVC STL, `std::filesystem` paths, floating point formatting) | `ctest --test-dir build-mingw --output-on-failure` |

GCC only warns for translation units it actually recompiles, so a clean incremental build is
not evidence: rebuild the tree (or touch the affected files) before concluding it is quiet.
`warning:` lines under `_deps/` are third-party headers; wrap them at the include site with
`#pragma GCC diagnostic ignored` as `src/pathtracer/TextureSampling.cpp` and
`src/pathtracer/EnvironmentSampling.cpp` do, and fix project-code warnings at the cause.

Build custom targets with:

```powershell
cmake --build <build-dir> --config Release --target <target-name>
```

`path-tracing-acceptance` produces deterministic CPU reference outputs. `path-tracing-regression` compares Beauty plus seven AOVs against the fixed volume-glass reference. `path-tracing-raster-comparison` renders the three fixed `.myscene` inputs through both raster and CPU paths and records difference metrics; it does not require the two algorithms to match pixel-for-pixel.

Visual and benchmark targets write current artifacts under the selected build directory. They should not rewrite versioned evidence under `docs/`.
