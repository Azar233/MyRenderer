# Glass-4 portfolio validation

Glass-4 closes the glass roadmap with two deterministic presentation scenes, independent feature controls, visual regression, pass-level GPU evidence, and a saved graphics capture. The target is a reproducible real-time portfolio case study rather than a single hand-tuned screenshot.

## Presentation scenes

### Volume validation

`View / Volume glass preset` loads `glass_volume_sphere.gltf`, creates two independent `RenderItem` instances, places them in front of the checker backdrop, enables the Radiance HDRI/Split-Sum IBL path, and restores a fixed camera. This scene validates curved exit normals, object-level entry/exit pairing, double-interface refraction, and thickness-dependent Beer-Lambert absorption.

![Glass-4 volume validation](images/glass4_volume_final.png)

### Caustics hero

`View / Glass caustics preset` loads the Crystal preset over a white receiver and black background, with Light-space RGB caustics and colored transmission shadows enabled. It uses its own fixed camera so Glass, Dispersion, and Caustics can be toggled without changing composition.

![Glass-4 caustics hero](images/glass4_caustics_final.png)

## Reusable controls and presets

The Renderer inspector exposes three independent switches:

- `Glass transmission` controls the refractive/transmissive material path.
- `Dispersion` is a true boolean gate. When disabled, both the override and glTF material dispersion are ignored by the glass and light-space caustics shaders.
- `HDR caustics` controls only the caustics pass and keeps the glass/camera unchanged.

Four reusable volume presets share one shader and only change material parameters:

| Preset | Attenuation RGB | Distance | Roughness | Dispersion | Intended use |
| --- | --- | ---: | ---: | ---: | --- |
| Clear | 1.00, 1.00, 1.00 | 8.00 | 0.04 | material/0 | neutral product glass |
| Olive | 0.68, 0.86, 0.22 | 0.85 | 0.06 | material/0 | thickness/absorption validation |
| Amber | 1.00, 0.48, 0.12 | 0.72 | 0.08 | material/0 | art-directed warm glass |
| Crystal | 0.78, 0.92, 1.00 | 2.00 | 0.06 | 2.00 | dispersion/caustics hero |

Automation uses `MYRENDERER_GLASS_PRESET=0|1|2|3`, `MYRENDERER_TRANSMISSION=0|1`, `MYRENDERER_DISPERSION_ENABLED=0|1`, `MYRENDERER_DISPERSION`, `MYRENDERER_CAUSTICS=0|1`, and `MYRENDERER_IOR`.

## Visual regression matrix

`glass4-visual-regression` captures and compares 14 reviewed 1920×1080 images. Every variable uses the same scene camera and explicit environment defaults.

| Coverage | Baselines |
| --- | --- |
| final / Glass Off | `glass4_volume_final`, `glass4_volume_glass_off` |
| IOR | `glass4_volume_ior_low`, `glass4_volume_ior_high` |
| Thickness / Attenuation | `glass4_volume_thickness_low`, `glass4_volume_attenuation_clear` |
| true exit data | `glass4_volume_exit_normal`, `glass4_volume_approximate` |
| dual-object pairing | `glass4_volume_object_id` |
| Dispersion / Caustics | `glass4_caustics_dispersion_off`, `glass4_caustics_off`, `glass4_caustics_final` |
| 1x / 4x MSAA | volume and caustics `msaa1` images against their 4x final images |

The lightweight comparator accepts a mean absolute error of 0.015 and at most 8% changed pixels, allowing small cross-driver differences while still catching material, pairing, pass-order, and edge regressions.

## 1080p performance evidence

Hardware: NVIDIA GeForce RTX 4060 Laptop GPU, OpenGL 3.3 driver 591.44. Each benchmark uses 30 warm-up frames and 90 measured frames at 1920×1080, 4x MSAA, VSync off. Times below are GPU timestamp-query medians from this run; transient OS/GPU load can affect whole-frame values.

| Scenario | Draw calls | GPU frame P50 | Forward glass P50 | Caustics P50 | Measured memory |
| --- | ---: | ---: | ---: | ---: | ---: |
| Volume, local-parallel exit | 23 | 2.190 ms | 0.551 ms | — | 291.3 MiB |
| Volume, true curved exit | 23 | 2.464 ms | 0.703 ms | — | 291.3 MiB |
| Caustics Off | 16 | 2.348 ms | 0.798 ms | — | 291.3 MiB |
| Light-space RGB Caustics | 21 | 2.458 ms | 0.806 ms | 0.094 ms | 291.3 MiB |

The true curved-exit path costs about 0.153 ms more in the Forward glass pass than the local-parallel approximation in this capture. The visual pair shows why the higher-quality path is the default. Enabling Light-space caustics adds five draw calls; its isolated pass costs about 0.094 ms. Caustics resources are preallocated, so the reported memory does not change when the pass is toggled.

All top-level passes now receive a `KHR_debug` range and a non-blocking timestamp pair. The GUI lists the latest smoothed time next to each active pass, while benchmark JSON includes per-pass P50/P95 values.

## Capture and failure boundaries

The versioned [Nsight Systems capture](captures/glass4_caustics.nsys-rep) contains `Shadow map`, `Colored transmission shadow`, `Light-space RGB caustics`, `Opaque HDR scene`, `Forward transparent / refractive scene`, and `Bloom + tone map` ranges. See [capture instructions](captures/README.md).

Known boundaries are intentionally kept visible:

- Entry/exit pairing is object-level and screen-space. Nested or concave shells inside one `RenderItem`, thin grazing surfaces, and off-screen exit points can use the stable local-parallel fallback.
- The caustics solver performs one refraction from a directional light onto one horizontal receiver plane. Vertical/curved receivers and off-screen hits are unsupported.
- Light-space caustics use three RGB IOR samples rather than a full spectral transport solution; low-poly input produces sparse splats.
- Two-pass spatial filtering improves stability but softens very fine caustic detail. It is a deliberate quality/stability trade-off rather than temporal accumulation.

Reproduce the evidence with:

```powershell
cmake --build build-release --target glass4-visual-regression
cmake --build build-release --target glass4-benchmark
cmake --build build-release --target glass4-nsight-capture
```
