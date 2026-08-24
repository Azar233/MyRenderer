# Prism Spectrum 技术说明

本文记录 MyRenderer 棱镜分光 Demo 的光学模型、工程边界和可重复运行方式。当前 Prism-5 已完成：连续光谱求解、柔边 HDR 光束、实时参数控制、光学 Preset、光路调试、视觉回归、1080p Benchmark 与 Demo Reel 均已形成可重复证据。

## 1. 数据流

```text
glTF KHR_materials_ior / dispersion / volume
                    ↓
MaterialData → GpuMaterial → Glass Shader
                    ↓
PrismDemo 参数/Preset → PrismOptics CPU 光谱采样 → SpectralBeamData
                                              ├→ SpectralBeamMesh → SpectralBeamRenderer
                                              └→ OpticalPathDebugRenderer
```

`DebugGrid` 只继续负责地面网格与坐标轴。

Assimp 6.0.5 尚未公开 `KHR_materials_dispersion` 的材质键，因此 `GltfMaterialExtensions` 只解析 glTF/GLB JSON 中这一项扩展；其余资产内容仍由 Assimp 负责。该窄适配层避免让渲染模块依赖 glTF 的 JSON 结构。

## 2. 波长相关折射率

glTF 保存的 `dispersion` 与 Abbe Number（阿贝数）关系为：

```text
Vd = 20 / dispersion
```

中心折射率 `nd` 来自 `KHR_materials_ior`。对波长 `lambda`（单位 nm），项目使用 Khronos 给出的两项 Cauchy 形式：

```text
n(lambda) = max(
    nd + (nd - 1) / Vd * (523655 / lambda² - 1.5168),
    1
)
```

参考：[Khronos KHR_materials_dispersion](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_dispersion)。`dispersion=0` 是特殊值，表示所有波长都使用中心 IOR。

## 3. 光谱采样与显示颜色

Continuous 模式在 380～700 nm 均匀取样，提供 7 / 15 / 21 / 31 四档；默认 21。Seven-band 模式固定使用 650 / 610 / 580 / 540 / 500 / 460 / 420 nm，便于得到分界清楚、适合美术控制的七色结果。

每个波长都独立经过两次 Ray/Prism 求交、Snell 折射、TIR 判断与 Schlick Fresnel。波长到颜色的转换使用 Wyman、Sloan、Shirley 对 CIE 1931 2° XYZ Color Matching Functions 的解析高斯近似，再用标准 XYZ→线性 sRGB 矩阵转换；负的显示设备通道被裁剪为 0，但不会对每个波长单独做 Gamma 编码或亮度归一化。参考：[JCGT 2013 analytic CIE fits](https://jcgt.org/published/0002/02/01/)。

## 4. 能量

单个样本的透射权重为：

```text
sampleTransmittance = entryFresnel
                    * exitFresnel
                    * beerLambert(distanceInGlass, wavelength)
```

所有有效样本随后统一归一化，使权重总和为 1。这样从 7 个样本切换到 31 个样本不会仅因样本数量增加而让总光束变亮。Prism-3 的 Ribbon Beam Pass 直接读取同一份 `SpectralBeamData`。

## 5. Ribbon Mesh 与 Pass 顺序

`SpectralBeamMesh` 在 CPU 上把光路转换为三角形：入射束是一条白色 Ribbon；棱镜内部束在两个界面收窄并保持在轮廓中；连续模式把相邻波长连接成无缝光谱扇面，七色模式则保留独立条带。Ribbon 的横向方向由光束方向和相机视线叉乘得到，因此转动相机时仍以稳定宽度面向观察者。

```text
Shadow Map
→ Opaque HDR Scene（保留 Depth，不立即 Resolve）
→ Spectral Beam HDR（Depth Test + Additive Blend）
→ Resolve Opaque HDR + 生成 Mip
→ Forward Transparent / Refractive（采样含光束的 Opaque HDR）
→ Optical Path Debug Overlay（可选）
→ Bloom + Tone Mapping
```

Beam Pass 不采样当前颜色附件，只向它加光，因此没有 Framebuffer Feedback。随后才 Resolve 成独立纹理供 Glass Pass 读取。Incident / Internal / Exit 三个批次分别放入 OpenGL `KHR_debug` Debug Group，便于 RenderDoc 或驱动调试器定位。

## 6. Prism-4 参数、Preset 与调试

`PrismDemoParameters` 是控制层的唯一光学参数源。修改 Beam Direction、Central IOR、Dispersion、Spectral Samples、Spectrum Mode 或 White Point 后，CPU 会立即重新调用 `tracePrismSpectrum`；Renderer 只消费求解结果，不在绘制阶段重复做求交。Central IOR 和 Dispersion 也会覆盖测试棱镜的 Glass Shader 参数，使玻璃外观和光线路径保持一致。

四个内置 Preset 共用同一套 Shader 与 Pass，只保存参数：

| Preset | 用途 |
| --- | --- |
| Crown Glass | 低到中等色散的默认物理基线 |
| Water-like | 较低 IOR、轻微冷色吸收的对照材料 |
| Diamond-like | 高 IOR 与 TIR 压力场景 |
| Exaggerated Cover | 七色模式和增强色散的美术化封面构图 |

White Point 使用 Kelvin 色温近似转换到线性 sRGB，并调制入射、内部和出射光束。Bloom Contribution 只控制 Beam HDR 增益，独立于全局 Bloom Threshold / Intensity，方便美术调整发光感而不改变光路。

`Optical path debug` 在 Glass Pass 之后叠加世界空间线：白色是中心入射线，光谱色线显示各波长的内部与出射路径，黄色/洋红短线分别表示入射/出射界面法线，橙色表示 TIR。交点以 Point 标记，Inspector 的折叠表格逐波长显示 IOR、Entry T、Exit T 和 Total Transmittance。

## 7. 可重复运行

```powershell
$env:MYRENDERER_PRISM_DEMO = "1"
$env:MYRENDERER_PRISM_SAMPLES = "21"
.\build-mingw\MyRenderer.exe
```

切换七色模式：

```powershell
$env:MYRENDERER_PRISM_SPECTRUM_MODE = "seven"
```

光束外观覆盖：

```powershell
$env:MYRENDERER_PRISM_BEAM_WIDTH = "0.055"
$env:MYRENDERER_PRISM_BEAM_INTENSITY = "5.0"
$env:MYRENDERER_PRISM_BEAM_SOFTNESS = "0.72"
$env:MYRENDERER_PRISM_BLOOM_CONTRIBUTION = "0.35"
```

Prism-4 还支持 `MYRENDERER_PRISM_PRESET=0|1|2|3`、`MYRENDERER_PRISM_BEAM_ANGLE`、`MYRENDERER_PRISM_IOR`、`MYRENDERER_PRISM_DISPERSION`、`MYRENDERER_PRISM_WHITE_POINT` 与 `MYRENDERER_PRISM_DEBUG=1`。Preset 编号依次对应 Crown / Water / Diamond / Exaggerated，后续单项环境变量会覆盖 Preset 中的对应参数。

固定结果见：

- `images/prism2_continuous_spectrum.png`
- `images/prism2_seven_band.png`
- `images/prism3_continuous_ribbon.png`
- `images/prism3_seven_band_ribbon.png`
- `images/prism4_exaggerated_cover.png`
- `images/prism4_optical_debug.png`

## 8. 当前边界

- 当前可见 Ribbon 是光路可视化，不是真实空气体积散射；干净空气中的光束从侧面通常不可见。
- Ribbon 已解决 OpenGL Line 的宽度和柔边限制，但属于透明发光几何；Prism-5 已覆盖固定角度与 MSAA 回归，接近沿光束方向观察的退化视角仍属于已知边界。
- White Point 当前使用 Kelvin 到线性 sRGB 的显示近似，不是黑体辐射谱的逐波长积分；作品集说明中需保持这一工程边界。
- Optical Path Debug 是可读性优先的 Overlay，关闭后不进入最终作品集画面；Prism-5 已用独立 TIR 基线保留该调试证据。

## 9. Prism-5 验收入口

- 完整视觉矩阵、性能表、显存统计、失败边界与复现命令：`docs/prism5-validation.md`。
- 版本化基线：`docs/images/prism5_*.png`。
- 原始性能 JSON：`docs/performance/prism5_samples_*.json`。
- 15 秒作品集片段：`docs/media/prism5_demo_reel.mp4`。
- 自动目标：`prism5-visual-regression`、`prism5-benchmark`、`prism5-reel-frames`。
- CIE 解析函数是标准曲线的高质量近似，不是逐纳米查表；文档与代码均明确保留这一近似边界。
