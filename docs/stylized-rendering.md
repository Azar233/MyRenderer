# SR-P2 Stylized / NPR Rendering

SR-P2A/B 在现有 Scene、材质、灯光、阴影、IBL、Forward 与 Hybrid Deferred
管线上增加一条可切换的 Stylized / Toon 着色路径。它不是另一套资产系统：同一份
`.myscene`、相机、Transform、glTF 材质和灯光可在 PBR 与 Stylized 之间直接切换。

## SR-P2A 范围

Inspector 的 `PBR & environment > Shading mode` 提供 `Physically based` 和
`Stylized / toon`。Stylized 模式当前包含：

- `Lighting bands` 与 `Band softness`：把主光和局部光的 `N·L` 量化为 2～8 档，
  可用窄 `smoothstep` 控制硬边或柔和过渡。
- `Specular size` 与 `Specular softness`：用 `N·H` 阈值生成独立的分层高光。
- `Rim width`、`Rim softness`、`Rim intensity` 和 `Rim color`：根据 `1-N·V`
  生成可导演的边缘光，并让背光侧更明显。
- `Shadow tint`：方向光阴影从固定黑色改为可调色调；PCF/彩色透射阴影的可见度
  仍参与混合。

环境 Diffuse Irradiance、局部 Point/Spot Light、Caustics 和已有后处理继续复用。
Forward 的透明/透射材质保留现有物理玻璃路径，避免 Toon 分支破坏双界面折射、
Beer-Lambert 与色散。Hybrid Deferred 的不透明物体使用同一套量化、高光、边缘光
和阴影参数；透明物体仍按既有 Forward Refractive Pass 绘制。

所有参数都保存在 `.myscene` 的 renderer 设置中。旧场景没有这些字段时使用默认
PBR 模式，因此现有固定图和场景文件的行为不变。

## SR-P2B 屏幕空间描边

`Screen-space outline` 在最终合成阶段读取当前帧 Depth；Hybrid Deferred 还会读取
G-Buffer Encoded Normal，以捕获同一物体内部的硬折角。Forward 没有常驻 Normal
Buffer，因此使用低成本 Depth-only 回退，不额外执行几何 Prepass。

- `Outline width` 以像素为单位，采样步长由实际输出尺寸计算，调整分辨率时不会按
  UV 比例无意变粗或变细。
- `Outline depth threshold` 使用重建后的 View-space 相对深度差，避免直接比较非线性
  Depth 带来的远近不一致。
- `Outline normal threshold` 控制 Deferred 法线折角灵敏度。
- 轮廓只向已有不透明几何内部扩张，不在天空一侧产生外部 Halo。
- 描边在 TAA、Bloom、曝光、Tone Mapping 和 sRGB 之后合成，因此不会进入 TAA
  History 产生拖影，颜色也不随曝光漂移。

BLEND 和玻璃材质沿用不写 Depth 的既有语义，因而不会生成实体外轮廓；它们后方
不透明物体的 Depth 轮廓仍然可见。这是当前明确的透明边界规则，不把玻璃错误地
当作不透明剪影。未来如需玻璃专用轮廓，应使用独立材质 Mask，而不是改变深度写入。

## 自动验收

以下目标通过正常场景加载器打开
`assets/scenes/14_polyhaven_material_gallery.myscene` 和固定玻璃场景，输出：

- `sr_p2a_pbr_forward.png`
- 640×360 Toon Forward / Deferred 及 Outline On/Off
- 640×360 TAA On 下的 Outline On/Off
- 960×540 Outline On/Off
- `sr_p2b_transmission_boundary.png`

```powershell
cmake --build build-ci-msvc --config Release --target stylized-acceptance
```

输出目录是 `build-ci-msvc/stylized-acceptance/`。验收要求 PBR/Toon、640×360
Outline On/Off、TAA Outline On/Off 和 960×540 Outline On/Off 均不能逐像素相同；
Toon Forward / Deferred 还必须满足显示空间 `MAE <= 0.02` 且变化像素比例
`<= 15%`。自动化会隐藏编辑器 Selection Outline，避免把橙色选择框误当成 NPR
证据。该比较不会创建或改写现有固定图 baseline。

自动化也可单独覆盖场景中保存的模式：`MYRENDERER_STYLIZED=0|1`、
`MYRENDERER_STYLIZED_BANDS=2..8`、`MYRENDERER_STYLIZED_OUTLINE=0|1`、
`MYRENDERER_STYLIZED_OUTLINE_WIDTH=0.5..6.0` 和 `MYRENDERER_RENDER_PATH=0|1`。
这些覆盖在场景加载后应用，仅用于可重复捕获。

## 当前边界与下一步

SR-P2A/B 已完成表面光照和稳定的屏幕空间描边。Face/Direction Map、Dither、
Height Fog、Color Grading LUT、三组 Preset、调试视图和 Low/High 性能档仍属于后续
SR-P2 增量。当前 Toon 参数是场景级设置，不是每材质节点图；
这与本阶段“不扩张为通用节点材质编辑器”的范围一致。
