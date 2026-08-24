# Versioned performance captures

`prism5_samples_7.json`, `15`, `21`, and `31` are the reference Prism-5
benchmark outputs for the RTX 4060 Laptop capture documented in
`docs/prism5-validation.md`.

Each file records the fixed resolution/MSAA configuration, CPU optics and frame
P50/P95, GPU frame and Beam Pass P50/P95, measurement counts, Draw Calls, and an
explainable GPU-memory estimate. Re-running the `prism5-benchmark` target writes
new machine-specific results to the build directory instead of overwriting
these versioned reference files.
