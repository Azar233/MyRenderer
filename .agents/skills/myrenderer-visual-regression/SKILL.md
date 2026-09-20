---
name: myrenderer-visual-regression
description: Capture, compare, diagnose, or intentionally update MyRenderer fixed-camera visual regressions and performance evidence. Use for shader/render-pass changes, image drift, baseline maintenance, GPU benchmarks, or renderer acceptance; do not use for CPU-only unit-test work.
---

# MyRenderer visual regression

Treat a versioned image as reviewed evidence, not as disposable test output.

## Select the existing acceptance surface

- Stylized/PBR/outline: `stylized-acceptance`
- Prism: `prism5-visual-regression` and `prism5-benchmark`
- Curved volume glass: `glass2c-visual-regression` and `glass2c-benchmark`
- Caustics: `glass3-visual-regression` or `glass4-visual-regression` and the matching benchmark
- Forward/Deferred and G-Buffer: `deferred-visual-regression` and `deferred-benchmark`
- Local lights: `local-lights-visual-regression` and `local-lights-benchmark`
- Instancing/culling/LOD: `instance-stress-visual-regression` and `instance-stress-benchmark`
- SSAO/TAA: `screen-space-visual-regression` and `screen-space-benchmark`
- Skinning/motion: `skinning-visual-regression` and `skinning-benchmark`
- Scene entities/object motion: `foundation-visual-regression`
- CPU reference output: `path-tracing-regression`
- Shared raster/path-traced scene comparison: `path-tracing-raster-comparison`
- Broad pixel confidence: `renderer-regression-suite`

Build a target through CMake so its renderer, comparator, fixed environment, and output directory stay consistent:

```powershell
cmake --build <build-dir> --config Release --target <target-name>
```

## Diagnose drift before changing a baseline

1. Confirm the intended build, GPU/driver, resolution, MSAA, scene, camera, seed/time, render path, and feature flags.
2. Inspect the generated current images and comparator metrics in the build directory. Use debug/AOV views to distinguish scene or state drift from algorithm changes.
3. Check pass order, state restoration, history reset, color space, asset selection, and deterministic time before loosening a threshold.
4. Compare the feature's on/off or Forward/Deferred pair. A passing image threshold does not replace semantic checks that the feature visibly changes the intended pixels.
5. Keep benchmark conclusions separate from correctness. Record CPU/GPU P50/P95, resolution, quality tier, draw calls, memory estimate, and hardware when performance is part of acceptance.

The visual comparator intentionally uses GPU-tolerant MAE and changed-pixel thresholds rather than byte equality. Raster and path tracing have different integration goals, so their comparison records differences and catches broken artifacts; it is not a zero-error parity test.

## Baseline policy

- Normal regression runs write under the selected build directory and must not modify `docs/images`, `docs/reference-images`, or `docs/performance`.
- Do not regenerate a versioned baseline merely to make a failure pass. Update it only when the user explicitly accepts the intended visual change or baseline maintenance is the task.
- When an update is authorized, isolate baseline changes from implementation changes when practical. Preserve fixed scene/camera/settings, review old/new/difference images, document the reason and test environment, and update the relevant technical document plus `docs/images/README.md` or `docs/performance/README.md` when their inventories change.
- Never overwrite historical path-tracer stage images that are documented as historical evidence. Add or advance the active reference deliberately.
- New acceptance outputs need a stable fixture, deterministic controls, an output location under the build tree, meaningful failure thresholds or artifact assertions, and a documented reproduction command.

## Illustration captures are not baselines

A screenshot taken to explain a feature in a document belongs in `docs/media/`, never in `docs/images/`. `docs/images/` and `docs/reference-images/` are inventories that regression targets compare pixel-wise, so an explanatory capture placed there silently becomes a baseline that later runs must match. Capture illustrations freely, recapture them whenever the feature changes, and record the command that produced each one next to its caption. The writing and caption conventions live in `$myrenderer-documentation` and `docs/README.md`.

Use `$myrenderer-build-and-test` for build-tree selection and CPU/GPU test coverage around the visual run.
