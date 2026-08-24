# Prism-5 Validation and Portfolio Evidence

This report closes the Prism Spectrum feature with repeatable correctness,
performance, and presentation evidence. Raw benchmark captures are versioned in
`docs/performance/prism5_samples_*.json`.

## Test environment

- Build: MinGW Release, OpenGL 3.3 Core
- GPU: NVIDIA GeForce RTX 4060 Laptop GPU
- Driver: NVIDIA 591.44
- Resolution: 1920 × 1080
- MSAA: 4x
- Timing: VSync off, 60 warm-up frames, 180 measured frames
- GPU timing: timestamp queries around the Beam Pass; full-frame timing uses a
  four-slot elapsed-time query ring
- CPU optics timing: 256 repeated `solvePrismDemo` calls per quality tier

P50 is the median measurement. P95 exposes the slower tail without letting one
isolated outlier dominate the report.

## Spectral quality benchmark

| Samples | CPU optics P50 / P95 | GPU Beam P50 / P95 | GPU frame P50 / P95 | Draw calls | Estimated GPU working set |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 7 | 0.0010 / 0.0010 ms | 0.0031 / 0.0205 ms | 1.6056 / 2.6388 ms | 14 | 203.396 MiB |
| 15 | 0.0019 / 0.0019 ms | 0.0041 / 0.0287 ms | 1.6620 / 2.8262 ms | 14 | 203.397 MiB |
| 21 | 0.0027 / 0.0028 ms | 0.0051 / 0.0317 ms | 1.6343 / 2.7535 ms | 14 | 203.398 MiB |
| 31 | 0.0040 / 0.0041 ms | 0.0061 / 0.0328 ms | 1.6968 / 2.8324 ms | 14 | 203.400 MiB |

The worst measured Beam P95 is 0.0328 ms, well below the 2 ms investigation
threshold. Increasing spectral samples mainly grows the small dynamic ribbon
VBO and CPU solve work; it does not add Draw Calls because adjacent wavelengths
are submitted in the same three Incident/Internal/Exit batches. The working-set
estimate includes RenderTargets, Bloom ping-pong textures, MSAA attachments,
Shadow Map, environment Cubemap, Beam VBO, prism vertex/index data, and imported
textures. It excludes opaque driver bookkeeping and allocator padding.

## Visual regression matrix

The `prism5-visual-regression` target captures ten 1920 × 1080 fixed-camera
images and compares them with the versioned baselines. The comparison reports
normalized RGBA mean absolute error (MAE) plus the fraction of pixels whose
largest channel difference exceeds 8/255. The default cross-driver tolerance is
MAE ≤ 0.015 and changed pixels ≤ 8%.

| Baseline | Evidence |
| --- | --- |
| `prism5_white_beam_no_prism.png` | White-beam stage with hidden prism, IOR 1, and zero dispersion |
| `prism5_prism_no_dispersion.png` | Visible prism with all wavelength paths spatially coincident |
| `prism5_continuous_21.png` | Physical-mode 21-sample continuous spectrum |
| `prism5_seven_band.png` | Seven-band art-direction mode |
| `prism5_tir_debug.png` | High-IOR TIR path and interface normals |
| `prism5_angle_minus_8.png` / `prism5_angle_plus_12.png` | World-space beam direction and prism-face topology coverage |
| `prism5_msaa1.png` / `prism5_msaa4.png` | Raster edge coverage comparison |
| `prism5_hero_exaggerated.png` | Final portfolio Hero Shot |

On the capture machine, a clean rebuild and recapture produced MAE 0 and 0%
changed pixels for all ten images. The CPU test additionally sweeps beam angles
from 2° to 12° in 0.5° steps and asserts finite paths, stable red/violet order,
and continuous adjacent directions. A ray exactly crossing a prism vertex can
legitimately switch exit faces; the wider -8°/+12° pair is retained as explicit
topology coverage rather than treated as an interpolation range.

## Presentation sequence

The three-stage portfolio explanation uses:

1. `prism5_white_beam_no_prism.png` — White Beam.
2. `prism5_prism_no_dispersion.png` — Prism with coincident wavelengths.
3. `prism5_hero_exaggerated.png` — separated Spectrum and final art direction.

`docs/media/prism5_demo_reel.mp4` is a deterministic 15-second, 24 fps,
1280 × 720 capture. It moves from zero dispersion through a smooth dispersion
ramp, sweeps a continuous 2°–12° incident-angle interval, and finishes on the
seven-band Exaggerated Cover preset.

## Reproduction

```powershell
cmake --build build-release --target prism5-visual-regression
cmake --build build-release --target prism5-benchmark
cmake --build build-release --target prism5-reel-frames
python tools/encode_prism5_reel.py build-release/prism5-reel-frames docs/media/prism5_demo_reel.mp4 --fps 24
```

The benchmark target writes JSON into `build-release/prism5-benchmarks` so a
new machine does not overwrite the versioned reference measurements.
