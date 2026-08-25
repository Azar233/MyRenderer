# Glass-4 graphics capture

`glass4_caustics.nsys-rep` was recorded at 1920×1080, 4x MSAA on an NVIDIA GeForce RTX 4060 Laptop GPU with Nsight Systems 2024.5.1. Open it in Nsight Systems and inspect the OpenGL GPU workload row; the renderer's `KHR_debug` groups label every top-level render pass.

The capture was validated with:

```powershell
nsys stats --report opengl_khr_gpu_range_sum,opengl_khr_range_sum glass4_caustics.nsys-rep
```

The exported GPU range summary contains six named passes. Median captured GPU ranges were approximately 0.905 ms for Opaque HDR, 0.927 ms for Forward transparent/refractive, 1.015 ms for Bloom/tone map, 0.131 ms for Light-space RGB caustics, 0.054 ms for colored transmission shadow, and 0.012 ms for the shadow map. The short six-frame capture includes initialization effects; use `glass4-benchmark` for stable 90-frame P50/P95 measurements.
