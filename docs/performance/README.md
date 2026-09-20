# docs/performance：版本化性能记录清单

本目录是**版本化性能记录的清单**：这里的每个 JSON 都是人工复核过的参考机采集结果，用来支撑阶段文档里的性能结论，不参与任何自动逐像素比对（逐像素基线在 [`../images/`](../images/) 与 [`../reference-images/`](../reference-images/)）。写作规范见 [`../README.md`](../README.md) 第 3 节。本页是清单类文档而不是阶段文档，因此按规范不配图，证据形式就是下面的表格与复现命令。

## 本目录的规则

- **这里是参考测量记录，不是像素基线。** 这些文件记录的是某一次采集的数值，不会被 visual-regression 目标比对；比对的默认阈值与基线维护规则见 [`../images/README.md`](../images/README.md)。
- **重跑不会覆盖这里的文件。** `prism5-benchmark` 目标把新结果写进构建目录 `build-release/prism5-benchmarks/`（参考机器上实测写的是 `build-ci-msvc/prism5-benchmarks/`），因此新机器的结果不会覆盖版本化参考值。
- **不要用新采集悄悄改写参考值。** 只有在明确批准的基线/参考更新中才改写这里的 JSON，并在同一次变更里同步本清单、[`../prism5-validation.md`](../prism5-validation.md) 与 [`../regression-baseline-audit.md`](../regression-baseline-audit.md)。
- 新增记录时，本清单要给出「文件名 + 固定配置 + 这张记录证明什么」，并保证能按记录的命令重拍。

## 条目清单

四个文件都是 Prism-5 光谱质量基准的参考输出，对应 `docs/prism5-validation.md`：同一台参考机（NVIDIA GeForce RTX 4060 Laptop GPU/PCIe/SSE2，OpenGL 3.3.0，驱动 NVIDIA 591.44）、固定 1920×1080、4× MSAA、VSync 关闭、60 帧预热 + 180 帧测量；每个文件只改 `spectralSamples` 一个变量。`schemaVersion` 均为 `1`，字段语义与单位见下一节。

光谱与 CPU 光学：

| 文件 | spectralSamples | Draw Calls | CPU Optics P50 / P95 | CPU Frame P50 / P95 |
| --- | ---: | ---: | ---: | ---: |
| `prism5_samples_7.json` | 7 | 14 | 0.001000 / 0.001000 ms | 2.596000 / 3.803500 ms |
| `prism5_samples_15.json` | 15 | 14 | 0.001900 / 0.001900 ms | 2.778000 / 4.067900 ms |
| `prism5_samples_21.json` | 21 | 14 | 0.002700 / 0.002800 ms | 2.851800 / 4.071300 ms |
| `prism5_samples_31.json` | 31 | 14 | 0.004000 / 0.004100 ms | 2.896500 / 4.263600 ms |

GPU 与显存：

| 文件 | GPU Frame P50 / P95 | GPU Beam P50 / P95 | 测量数 CPU / GPU / Beam | Render Memory | Texture / Geometry Memory | totalMeasuredMemoryBytes | solveChecksum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `prism5_samples_7.json` | 1.605632 / 2.638848 ms | 0.003072 / 0.020480 ms | 180 / 180 / 160 | 213275132 B（203.396 MiB） | 0 B / 960 B | 213276092 B | 1792 |
| `prism5_samples_15.json` | 1.661952 / 2.826240 ms | 0.004096 / 0.028672 ms | 180 / 180 / 150 | 213276476 B（203.397 MiB） | 0 B / 960 B | 213277436 B | 3840 |
| `prism5_samples_21.json` | 1.634304 / 2.753536 ms | 0.005120 / 0.031744 ms | 180 / 180 / 151 | 213277484 B（203.398 MiB） | 0 B / 960 B | 213278444 B | 5376 |
| `prism5_samples_31.json` | 1.696768 / 2.832384 ms | 0.006144 / 0.032768 ms | 180 / 180 / 156 | 213279164 B（203.400 MiB） | 0 B / 960 B | 213280124 B | 7936 |

这组记录证明什么：

- **光谱采样数只推高 CPU Optics 与 Beam 的成本，不增加 Draw Call。** `spectralSamples` 从 7 增到 31 时 CPU Optics P50 从 `0.001000 ms` 增到 `0.004000 ms`、GPU Beam P50 从 `0.003072 ms` 增到 `0.006144 ms`，而 Draw Calls 始终是 `14`：相邻波长仍在同一批 Incident/Internal/Exit 提交里，只是 ribbon VBO 变大。
- **GPU Frame P50 在四个档位上都稳定在 `1.61～1.70 ms`**（最大 P95 为 `2.832384 ms`），因此 31 采样档没有把整帧成本推到需要重新设计的量级。
- **显存增量只来自每次多出的一点点动态几何。** `renderMemoryBytes` 随采样数单调增长约 4 KB 量级（`213275132 B → 213279164 B`），`textureMemoryBytes` 恒为 `0`，`geometryMemoryBytes` 恒为 `960 B`。
- **`solveChecksum` 随采样数线性增长**（`1792 / 3840 / 5376 / 7936`，即每采样 256），可作为「采集确实换了采样档」的确定性指纹。

## 字段说明

- `gpu`：采集时的 GPU / 驱动 / OpenGL 字符串，四个文件相同。
- `width`、`height`、`msaaSamples`：固定分辨率与 MSAA 档位，四个文件均为 1920 / 1080 / 4。
- `spectralSamples`：本档的光谱采样数，也是四个文件之间唯一的自变量。
- `drawCalls`：整帧提交的 Draw Call 计数；`cpuOpticsP50Ms` / `cpuOpticsP95Ms` 是 256 次重复 `solvePrismDemo` 调用的 CPU 光学求解时间。
- `cpuFrameP50Ms` / `cpuFrameP95Ms`：整帧 CPU 时间；`gpuFrameP50Ms` / `gpuFrameP95Ms`：整帧 GPU 时间（四槽 elapsed-time 查询环）。
- `gpuBeamP50Ms` / `gpuBeamP95Ms`：Beam Pass 的 GPU 时间（Beam Pass 周围的 timestamp 查询）。
- `cpuFrameMeasurements`、`gpuFrameMeasurements`、`gpuBeamMeasurements`：各自的有效测量帧数；Beam 少于 180 是因为它只统计该 pass 真正被执行的帧。
- `renderMemoryBytes`、`textureMemoryBytes`、`geometryMemoryBytes`、`totalMeasuredMemoryBytes`：可解释的显存估算，分别对应 RenderTarget / Bloom ping-pong / MSAA Attachment / Shadow Map / 环境 Cubemap / Beam VBO / 棱镜顶点索引数据与导入纹理，以及纹理与几何的实测字节数。估算不含驱动簿记与分配器填充。
- `solveChecksum`：光学求解结果的校验和。

## 复现命令

```powershell
cmake --build build-release --target prism5-benchmark
```

该目标按 `spectralSamples` = `7`、`15`、`21`、`31` 依次运行渲染器，写入 `build-release/prism5-benchmarks/prism5_samples_<n>.json`，因此它不会覆盖本目录里的版本化参考文件。任一档失败都会让该构建目标失败。
