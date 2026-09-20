# docs/captures：图形捕获（graphics capture）清单

本目录存放**版本化的工具捕获文件**：这里的每个文件都是在固定分辨率与固定采样档位下用外部图形工具录下的原始捕获，用来支撑阶段文档里的逐 Pass GPU 时间与 Pass 顺序结论。本页是清单类文档而不是阶段文档，因此按 [`../README.md`](../README.md) 第 4.1 节**不配图**：证据形式就是下面的命令、Pass 名与时间数字，它们本身就是记录，不需要也不可能用一张图代替。

## 本目录的规则

- **这里是工具捕获，不是像素基线。** 这些文件不会被 visual-regression 目标比对；逐像素基线在 [`../images/`](../images/) 与 [`../reference-images/`](../reference-images/)，性能参考值在 [`../performance/`](../performance/)。
- **捕获文件依赖外部工具才能打开。** 本目录的 `.nsys-rep` 需要对应版本的 Nsight Systems；仓库只保证它是当时那次采集的原始产物，不保证能被其它版本或其它机器重现出同样的数字。
- **重跑不会覆盖这里的文件。** 捕获目标把结果写进构建目录（`glass4-nsight-capture` 写 `build-release/glass4-captures/`），因此新机器的捕获不会覆盖版本化文件。
- **删掉就无法重建。** 捕获的环境（分辨率、MSAA、驱动版本、工具版本）必须与本清单一起读，单看文件本身不足以复核结论。
- 新增捕获时，本清单要给出「文件名 + 采集配置 + 它证明哪一条结论 + 重拍命令」。

## 条目清单

### `glass4_caustics.nsys-rep`

- 采集内容：Glass-4 Light-space RGB 焦散的 Nsight Systems 捕获，用来核对渲染器的顶层 Pass 分组与逐 Pass GPU 时间。
- 采集条件：1920×1080、4× MSAA，`MYRENDERER_GLASS3_DEMO=1`，并在同一组 `cmake -E env` 里设置 `MYRENDERER_SMOKE_TEST=1`、`MYRENDERER_CAUSTICS=1`、`MYRENDERER_CAUSTICS_MODE=1`；在 NVIDIA GeForce RTX 4060 Laptop GPU 上录制，工具版本 Nsight Systems 2024.5.1。仓库里的 `find_program` 也正是把搜索路径指向 `Nsight Systems 2024.5.1`。
- 纳入版本：`763bd49`（2026-08-25，Glass-4 作品集验收）。
- 它证明什么：渲染器为**每个顶层 Pass** 都压了一个 `KHR_debug` Debug Group（组名就是 Pass 名），因此外部工具里能直接按名读 Pass 边界，而不是靠时间戳猜。导出后的 GPU range 汇总里有六个具名 Pass，捕获到的中位 GPU 时间约为：`Opaque HDR` 0.905 ms、`Forward transparent/refractive` 0.927 ms、`Bloom/tone map` 1.015 ms、`Light-space RGB caustics` 0.131 ms、`Colored transmission shadow` 0.054 ms、`Shadow map` 0.012 ms。

这个捕获是**只有六帧**的短采集，包含初始化效应，因此上面六个数字只用于确认 Pass 划分与量级，不能当作稳定性能值。稳定性能请用 `glass4-benchmark`：它按 30 帧预热 + 90 帧测量输出逐 Pass 的 P50/P95（`glass4-validation.md` 记录了「30 warm-up frames and 90 measured frames」这一口径）；逐场景性能表与失败边界见 [`../glass4-validation.md`](../glass4-validation.md)。该文档同时确认这些 Pass 名与捕获内容一致。

怎么读它：用 Nsight Systems 打开文件，看 OpenGL 的 GPU workload 行，逐行核对上面六个 Pass 名。

```powershell
nsys stats --report opengl_khr_gpu_range_sum,opengl_khr_range_sum glass4_caustics.nsys-rep
```

怎么重拍它（目标把新捕获写进 `build-release/glass4-captures/glass4_caustics.nsys-rep`，因此不会覆盖本目录的版本化文件）：

```powershell
cmake --build build-release --target glass4-nsight-capture
```

> **待复核**：本清单记录的 `Opaque HDR`（以及 `Forward transparent/refractive`、`Bloom/tone map`）与当前 `src/render/Renderer.cpp` 里的 Pass 名对不上——同一位置现在叫 `Forward opaque HDR scene` / `G-buffer geometry` / `Deferred lighting`，透明与后处理两处则是 `Forward transparent / refractive scene` 与 `Bloom + tone map`。捕获录制于 `763bd49`，那时用的可能就是旧名；本次按原文保留捕获记录里的名字，未擅自改写，也未逐版本核实当时的 `sequence.add` 字符串。可确认的是：当前 `build-release/glass4-benchmarks/glass4_caustics_on.json` 的 `gpuPasses` 键已经用新名（`Forward opaque HDR scene` 等），因此新旧名之间至少发生过一次重命名。另：「六帧」这一采集长度只有本清单一处记录，仓库里没有对应的 nsys 报告文件可以复核。

## 相关入口

- 逐 Pass GPU 时间与公开基准：`glass4-benchmark`，参数与解读见 [`../glass4-validation.md`](../glass4-validation.md)。
- 逐 Pass 时间在 GUI 里的显示：Renderer 面板按 Pass 名列出最近一次平滑时间；同一条数据也写进 benchmark JSON 的 `gpuPasses`。
- 捕获目标的完整环境变量集写在 `CMakeLists.txt` 的 `glass4-nsight-capture` 定义里；`find_program` 找不到 `nsys` 时该目标不会被创建。
