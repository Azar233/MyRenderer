# Glass-3：彩色焦散与透射阴影

Glass-3 在现有方向光、透明材质和 HDR 管线之上增加两条互补路线：`Projector / Decal` 用于快速美术定向，`Light-space RGB` 用于展示从灯光和玻璃几何出发的实时光能近似。两种路线共用 1024×1024 RGBA16F 焦散纹理、方向光 View-Projection、接收材质采样和空间滤波。

## 帧流程

```text
Opaque shadow depth
  -> Colored transmission shadow (RGBA16F, multiplicative blend)
  -> Projector or Light-space RGB caustics (RGBA16F, additive blend)
  -> Horizontal + vertical spatial filter
  -> Opaque HDR scene samples shadow transmittance and caustic radiance
  -> Forward transparent / refractive scene
  -> Bloom + tone map
```

不透明 Shadow Map 只写入不透明子网格。透明玻璃在同一 Light-space 中写入独立的透射纹理；纹理先清为白色，再用 `destination × sourceTransmittance` 累积每个玻璃边界。每个表面使用一半光学厚度，因此闭合体的前后边界相乘后近似完整 Beer-Lambert 衰减。接收 Shader 将 PCF 可见度与 RGB 透射率相乘，不再把高透射玻璃当成纯黑遮挡。

## Projector / Decal

Projector Shader 直接在 Light-space HDR 纹理中生成可旋转、缩放、变锐和动画的 RGB 环带。它不声称求解真实光路，适合 TA 快速构图、概念验证和低成本档位。Intensity、Scale、Direction、Sharpness 与 Animation 都能在 Inspector 中实时调整。

## Light-space RGB

进阶模式从方向光出发，对玻璃入射三角形分别计算 R/G/B 折射率。每个三角形在接收平面的折射落点生成一个小型 Photon Splat，并使用 `GL_ONE + GL_ONE` 累积 HDR 能量；样本越集中，结果越亮。RGB 分别提交，使 Dispersion 能产生可读的通道分离。

当前实现是实时光栅近似：它用入射界面折射方向投影到单一水平接收平面，并未追踪玻璃内部多次反射或任意接收网格。Geometry Shader 的 Splat 避免把变形后的原始三角形直接拉伸到地面，从而消除长尖刺；随后两次可调 Gaussian 空间滤波降低低采样网格边缘的闪烁。静态灯光、物体和参数下输出完全确定，不需要历史缓冲。

## 控制和调试

- `View / Glass caustics preset`：透明水晶球、白色接收地面、黑背景和固定高机位。
- `HDR caustics`：独立启停焦散，不影响玻璃主体折射。
- `Caustics mode`：Projector / Decal 或 Light-space RGB。
- `Strength / Scale / Direction / Sharpness`：控制能量、折射位移、落点偏移和空间滤波宽度。
- `Animate caustics`：只驱动 Projector 的程序化波纹；Light-space 模式保持几何驱动。
- `Colored transmission shadows`：独立启停 RGB 透射阴影。
- Debug 11 / 12：分别显示 Caustics Map 和合成后的 Transmission Shadow Visibility。

自动化入口：`MYRENDERER_GLASS3_DEMO=1`、`MYRENDERER_CAUSTICS=0|1`、`MYRENDERER_CAUSTICS_MODE=0|1`、`MYRENDERER_TRANSMISSION_SHADOWS=0|1`、`MYRENDERER_GLASS_DEBUG=11|12`。

## 视觉回归与性能

`glass3-visual-regression` 固定为 1920×1080，覆盖 Light-space 1x/4x MSAA、Caustics Off、Projector、Caustics Debug 和 Transmission Shadow Debug 六个场景。空间滤波和静态参数保证同机重拍稳定。

RTX 4060 Laptop、OpenGL 3.3、1920×1080、4x MSAA，30 帧预热、90 帧采样：

| 模式 | Caustics GPU P50 / P95 | Draw Call | 整帧 GPU P50 / P95 |
| --- | ---: | ---: | ---: |
| Off | 0 / 0 ms | 16 | 2.423 / 3.585 ms |
| Projector + 2-pass filter | 0.071 / 0.072 ms | 19 | 2.303 / 3.166 ms |
| Light-space RGB + 2-pass filter | 0.091 / 0.091 ms | 21 | 2.273 / 3.199 ms |

三种模式的 RenderTarget 估算均为 305,299,296 bytes，因为资源在启动时预分配以避免切换模式时卡顿。相较 Glass-2C，新增开销主要来自 2048² RGBA16F 透射阴影（32 MiB）和两张 1024² RGBA16F 焦散/滤波纹理（16 MiB）。整帧差异小于正常运行噪声，因此性能判断使用独立 Caustics Timestamp Query。

## 已知边界

- 只支持一个水平接收平面；墙面、曲面或多个高度需要接收者 G-Buffer、分层投影或 Ray Query。
- Light-space 模式使用单次入射折射和 Photon Splat，不是完整双界面路径追踪；强凹形、嵌套介质和全反射不完整。
- 折射落点离开 8×8 Light-space 范围时会被裁掉；Direction/Scale 过大可主动复现离屏失败。
- 低面数玻璃会减少 Splat 密度；空间滤波能降低闪烁，但不能恢复缺失的几何光路。
- Projector 是明确的美术近似，不能作为物理焦散证据；作品集算法对照应使用 Light-space 模式。
