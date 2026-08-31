# MyRenderer

一个独立的 C++17 / OpenGL 3.3 GPU 光栅化渲染器。项目从 Dandelion 图形学实验框架中保留了模型、相机、材质与实时预览的设计思路，但不包含 CPU 软光栅化、光线追踪、物理模拟和半边网格模块。

当前版本可以加载 OBJ、DAE、glTF 2.0 和 GLB 静态模型，将网格与纹理上传到 GPU，按子网格材质范围通过 `glDrawElements` 完成多材质渲染，并在 Dear ImGui 界面中实时调整模型与渲染参数。

Post-MVP 阶段已将文件导入、CPU 模型数据、GPU 模型和渲染执行拆分；OBJ 使用独立轻量导入器，DAE 与 glTF/GLB 使用统一 Assimp 适配器，渲染层不依赖具体文件格式。

项目中出现的图形学与工程概念统一记录在 [`dictionary.md`](dictionary.md)，包含通俗解释、项目用途和当前实现状态。

## 已实现

- OpenGL 3.3 Core Profile 与 GLFW 窗口。
- OBJ 三角化、多 shape 合并、缺失法线生成和 AABB 自动取景。
- 通过导入专用 Assimp 6.0.5 支持 DAE、glTF 2.0/GLB 的多 Mesh、节点变换、UV0、切线和材质关联。
- 基础色纹理、材质常量色、外部纹理、Data URI 与 GLB/Assimp 内嵌纹理来源。
- `Texture2D` OpenGL RAII、跨材质纹理缓存、白色无纹理回退与洋红缺失纹理回退。
- 基础色贴图按 sRGB 解码，材质/灯光在线性空间计算，并在最终输出时编码为 sRGB。
- 切线空间法线贴图、退化 UV 安全回退与可关闭的法线贴图着色。
- 按子网格材质范围绑定材质并提交 Draw Call，同一模型可显示多个材质。
- GPU 顶点/索引缓冲、深度测试、背面剔除和线框模式。
- 可实时切换的 Forward / Hybrid Deferred 不透明渲染路径：`GL_RGBA8` Albedo、`GL_RGBA16F` Encoded Normal、`GL_RG8` Metallic/Roughness 与 `GL_DEPTH24_STENCIL8` Depth/Stencil G-Buffer，支持 1×/4× MSAA Resolve、逐附件 Debug View、世界坐标重建与全屏 PBR/IBL Lighting Pass；透明/玻璃继续使用 Forward Refractive Pass。
- Point Light / Spot Light 局部光照与确定性压力场景：8/32/64 三档灯光、100 个独立物体、平滑有限半径逆平方衰减与聚光锥；GUI/Benchmark 对比 Forward/Deferred 的 GPU P50/P95、Draw Call、显存和估算 Attachment 流量。
- GPU Instancing、CPU Frustum Culling 与屏幕尺寸 LOD：2,500 个共享 Sphere Mesh 的固定场景按模型/Tint/LOD 合批，六平面包围球剔除并切换三档聚类索引；1080p/4×MSAA 实测 Draw Call 2,501→19、CPU P50 5.386→2.077 ms、GPU P50 1.465→0.653 ms。
- TAA 与 SSAO 屏幕空间管线：8 样本 Halton Jitter、深度反投影 Motion Vector、History Reprojection、历史深度拒绝、3×3 Neighborhood Clamp，以及 16 样本 SSAO + 5×5 深度感知滤波；提供静止/运动 Ghosting、Motion、History Weight 和 SSAO 调试基线。
- Debug 构建在驱动支持时启用 OpenGL `KHR_debug` 诊断。
- Model/View/Projection 变换与基础 Blinn-Phong 光照。
- 离屏 Framebuffer 渲染视口、可切换 1x/4x MSAA Resolve 与解析后视口 PNG 导出。
- glTF 2.0 metallic-roughness PBR（Cook-Torrance GGX）、Radiance HDR equirectangular 环境、Diffuse Irradiance、GGX Prefiltered Specular Cubemap、BRDF LUT、Split-Sum IBL、天空盒与方向光 PCF 阴影；HDR 资产缺失时回退到程序化 Studio 环境。
- glTF `OPAQUE` / `MASK` / `BLEND`、Alpha Cutoff、双面材质、透明子网格后向前排序，以及独立的透明深度/混合状态。
- glTF `KHR_materials_transmission` / `KHR_materials_ior` / `KHR_materials_volume` / `KHR_materials_dispersion`：IOR 驱动的 Fresnel、Snell 折射、全反射、深度 Ray March、Thickness Texture、对象级前/后表面深度、真实出射法线、双界面折射、Beer-Lambert 体积吸收、环境回退与 13 种 Glass/Light Debug View；材质色散可被 Inspector 全局覆盖。
- Glass-3/4 彩色光传输与作品集验收：RGBA16F 透射阴影、可控 Caustics Projector、Light-space RGB Photon Splat 焦散、两次空间滤波，以及独立 Glass / Dispersion / Caustics 开关、四组玻璃 Preset、14 场景视觉回归、逐 Pass GPU Timestamp 和带标记 Nsight Capture。
- Prism-0～5 光谱 Demo：原创封闭三棱柱、纯黑舞台、固定正面镜头、CPU 双界面 Ray/Prism 求交，以及 380～700 nm 的 7/15/21/31 档波长采样；每个样本使用 Cauchy IOR、CIE 1931 近似线性 RGB、两界面 Fresnel 与 Beer-Lambert 能量。独立 `Spectral beam HDR` Pass 把结果生成相机朝向的柔边 Ribbon Mesh，支持连续光谱和七色美术模式，并提供固定视觉回归、性能报告和 Demo Reel。
- 多对象 `RenderItem` 场景提交、跨对象透明 Draw List 全局排序，以及可调颜色/高度并能接收 PBR 光照与阴影的程序化地面；可开启第二模型实例验证场景级排序。
- `Shadow map → Transmission shadow → Caustics HDR/filter → Forward Opaque 或 G-Buffer + Deferred Lighting → Forward refractive → Bloom/tone map` 多 Pass 管线；Opaque HDR Color、最终 HDR Scene Color 与可采样 Depth 相互独立，可切换 ACES Tone Mapping、曝光和 Bloom。
- 像素风应用图标，覆盖 GLFW 标题栏、任务栏和 Windows 可执行文件资源。
- 顶部菜单、模型列表、Scene 面板、Inspector 面板和运行状态。
- Windows 原生模型文件选择器、窗口拖放加载与后台 CPU 资产导入；失败导入不会替换当前场景。
- 按文件、节点、Mesh、材质和纹理分组的结构化诊断。
- CPU 帧时间、无阻塞 GPU 时间查询、Draw Call、三角形和纹理内存统计。
- 可独立开关的 XZ 地面网格、世界 XYZ 轴线和随相机旋转的视口方向指示器（X 红、Y 绿、Z 蓝）。
- 轨道相机、平移、缩放、自动旋转和材质/灯光控制。

## 构建要求

- CMake 3.20+
- 支持 C++17 的编译器（Visual Studio 2022 或 MinGW-w64）
- 支持 OpenGL 3.3 的显卡与驱动
- Git 与 Python 3（首次配置时用于获取依赖和生成 GLAD）
- 首次配置需要访问 GitHub；依赖版本已在 `CMakeLists.txt` 中锁定

## Visual Studio 2022 构建

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T host=x64
cmake --build build --config Debug --parallel
.\build\Debug\MyRenderer.exe .\assets\models\bunny.obj
```

## MinGW-w64 构建

```powershell
cmake -S . -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-mingw --parallel
.\build-mingw\MyRenderer.exe .\assets\models\bunny.obj
```

不传模型参数时，程序默认加载 `assets/models/cube.obj`。

## GUI 操作

- Scene 面板：查看主体、地面接收器与可选对照实例，切换 `assets/models` 中的 OBJ、DAE、glTF/GLB，使用原生文件选择器，输入其他模型路径，或把模型文件拖入窗口。CPU 导入期间会显示文件大小、耗时和活动进度，当前场景保持可用。
- Inspector / Object：调整世界坐标 Position、旋转、缩放、材质颜色 Tint 和光照系数；Stage 区可控制真实地面、地面颜色/高度与对照实例。这里也会显示 Mesh、子网格/Draw Call、材质、纹理、回退纹理与估算显存统计。模型导入后以 AABB 中心作为局部原点，默认世界 Position 为 `(0, 0, 0)`。
- Inspector / Renderer：切换 Forward / Deferred (hybrid)、G-Buffer Albedo/Encoded Normal/Metallic-Roughness/Depth/SSAO 调试、TAA Final/Motion/History Weight、PBR、IBL、天空盒、阴影、彩色透射阴影、HDR Caustics、Transmission、Geometric Glass Thickness、ACES、Bloom、线框、背面剔除、法线贴图、地面网格、XYZ 轴线和 1x/4x MSAA；调整 SSAO Radius/Bias/Strength、TAA History Weight、焦散、折射、体积、色散、环境、曝光、灯光与 FOV，并查看活动 Pass 和 GPU 时间。
- View / `Prism spectrum preset`：加载 `prism_spectrum.gltf` 并恢复 Prism-0 固定镜头与黑场参数；Renderer 面板可单独开关 `Prism incident beam guide`。成功加载其他模型时会自动退出 Prism 模式，关闭光束/光路 Overlay，并恢复进入 Preset 前的通用渲染与场景显示设置。
- View / `Volume glass preset`：加载平滑闭合球体，自动创建两个独立玻璃实例、原创棋盘格背景和固定正面机位；Renderer 面板可切换真实双界面折射，并使用 Clear / Olive / Amber / Crystal 四组体积玻璃参数。
- View / `Glass caustics preset`：加载透明水晶球、白色接收地面与固定高机位，默认启用 Light-space RGB 焦散、彩色透射阴影和空间滤波；可即时切到 Projector / Decal 做美术对照。
- View / `Local light stress preset`：加载 10×10 立方体固定舞台，并在 Renderer 面板选择 8/32/64 档 Point/Spot 灯光；切换 Forward/Deferred 可查看相同画面下的活动 Pass、Draw Call 与估算 Opaque Attachment 流量。
- 渲染视口：鼠标右键拖动旋转相机，中键拖动平移，滚轮缩放；工具栏或 File 菜单可将当前解析后画面保存为 PNG。
- `Esc`：退出程序。

ImGui 窗口支持拖动与 Docking，布局会保存到运行目录下的 `MyRenderer.ini`。

## 支持范围与格式路线

当前正式支持静态 `.obj`、`.dae`、`.gltf` 和 `.glb`。OBJ 继续使用轻量的 tinyobjloader；DAE 与 glTF/GLB 通过统一 `ModelImporter` 接口接入 Assimp，第三方数据类型不会进入渲染层。

所有格式最终转换为相同的 `ModelData`、子网格、材质和纹理来源数据。渲染层统一解码并缓存外部或内嵌图像，基础色 Shader 将材质因子、可调 Tint 和基础色贴图相乘；没有贴图的材质使用白色纹理，无法解码或缺失的基础色贴图使用洋红棋盘并在状态区报告原因。基础色纹理使用 sRGB 内部格式，法线贴图保持线性数据；光照在线性空间完成，最终颜色仅进行一次 sRGB 编码。

OBJ、DAE 与 glTF/GLB 材质可使用切线空间法线贴图；缺失或退化 UV 会禁用对应顶点的切线扰动并回退到几何法线。glTF PBR 使用标准 metallic-roughness 工作流（粗糙度在 G 通道、金属度在 B 通道），并支持基础 Alpha Mode、双面材质、`KHR_materials_transmission`、`KHR_materials_ior`、`KHR_materials_volume` 和 `KHR_materials_dispersion`。Alpha Blending 只做颜色覆盖率合成；Glass-1～2B 已完成透射、几何厚度、Beer-Lambert 吸收与色散。Glass-2C 按 `RenderItem` 重建 R32F 入口/出口深度、RGBA16F 出射法线和 R32UI 对象 ID，沿内部折射方向追踪真实退出点，再执行玻璃→空气的第二次 Snell 折射；无有效退出点时继续使用稳定的局部平行表面回退。对象级缓存解决独立玻璃实例互相串层，但同一 `RenderItem` 内的嵌套/凹形多壳体仍属于屏幕空间近似。环境光来自 Poly Haven 的 CC0 Radiance HDRI，并预计算标准 Split-Sum IBL。骨骼动画和 FBX 尚未启用。

固定回归资产包括 `material_regression.obj`（基础色/法线贴图、常量材质、缺失纹理）、`degenerate_uv.obj`（退化 UV 法线贴图回退）、`textured_quad.dae`（DAE 外部纹理）、`textured_triangle.gltf`（Data URI 内嵌纹理）、`pbr_material_test.gltf`（五组金属度/粗糙度组合与打包数据纹理）、`alpha_material_test.gltf`（OPAQUE/MASK/BLEND、双面与重叠透明排序）、`glass_material_test.gltf`（闭合光滑/粗糙玻璃与几何厚度）、`volume_texture_test.gltf`（线性 G 通道 Thickness Texture 导入）、`glass_volume_sphere.gltf`（1,986 顶点闭合流形球体）和 `prism_spectrum.gltf`（原创封闭三棱柱与体积玻璃）。默认环境为 Poly Haven 的 2K `Delta 2` CC0 HDRI，来源和许可记录见 `assets/environments/README.md`。

## 自动测试

纯 CPU 资产导入、场景透明排序与棱镜光路测试不创建 OpenGL 上下文，可直接通过 CTest 运行：

```powershell
ctest --test-dir build-mingw --output-on-failure
```

隐藏窗口模式会创建真实 OpenGL 上下文、加载模型并渲染 5 帧后退出：

```powershell
$env:MYRENDERER_SMOKE_TEST = "1"
.\build-mingw\MyRenderer.exe .\assets\models\sphere.obj
Remove-Item Env:MYRENDERER_SMOKE_TEST
```

设置 `MYRENDERER_SCENE_DEMO=1` 会额外创建一个模型实例，用于验证多个对象之间的透明排序；`gpu-smoke` 已对 Alpha 回归场景启用该模式。

自动测试还可通过 `MYRENDERER_MSAA=1|4` 选择采样数，并用 `MYRENDERER_SCREENSHOT=<输出.png>` 在渲染后导出截图。交互模式下截图默认写入运行目录的 `screenshots` 文件夹。

设置 `MYRENDERER_PRISM_DEMO=1` 会默认加载 Prism-0 固定资产和 Hero Shot 参数；可与隐藏窗口截图组合，用于生成同机位 baseline：

```powershell
$env:MYRENDERER_SMOKE_TEST = "1"
$env:MYRENDERER_PRISM_DEMO = "1"
$env:MYRENDERER_SCREENSHOT = ".\prism0_baseline.png"
.\build-mingw\MyRenderer.exe
Remove-Item Env:MYRENDERER_SMOKE_TEST, Env:MYRENDERER_PRISM_DEMO, Env:MYRENDERER_SCREENSHOT
```

Prism-2 可通过 `MYRENDERER_PRISM_SAMPLES=7|15|21|31` 选择光谱采样档位（其他数值吸附到最近档），并以 `MYRENDERER_PRISM_SPECTRUM_MODE=seven` 切换七色美术模式；默认是 21 样本连续光谱。

Prism-3 光束参数可通过 `MYRENDERER_PRISM_BEAM_WIDTH`、`MYRENDERER_PRISM_BEAM_INTENSITY` 与 `MYRENDERER_PRISM_BEAM_SOFTNESS` 覆盖，也可在 Inspector 的 `Spectral beam ribbons` 下实时调整；整体曝光与 Bloom 继续使用通用后处理控件。

Prism-4 在 Inspector 中提供实时 Beam Direction、IOR、Dispersion/Abbe、光谱采样、连续/七色模式、White Point、Bloom Contribution、四个光学 Preset、完整 Optical Path Debug，以及镜头锁定/恢复。自动化可使用 `MYRENDERER_PRISM_PRESET=0|1|2|3`、`MYRENDERER_PRISM_BEAM_ANGLE`、`MYRENDERER_PRISM_IOR`、`MYRENDERER_PRISM_DISPERSION`、`MYRENDERER_PRISM_WHITE_POINT`、`MYRENDERER_PRISM_BLOOM_CONTRIBUTION` 与 `MYRENDERER_PRISM_DEBUG=1`。

Glass-2B 默认启用前/后表面几何厚度，也可用 `MYRENDERER_GEOMETRIC_THICKNESS=0|1` 做回退厚度与几何厚度的同机对照；`MYRENDERER_GLASS_DEBUG=8` 显示 Front/Back Depth 数据有效性与深度跨度。

Glass-2C 默认启用真实出射面追踪；`MYRENDERER_TWO_INTERFACE_REFRACTION=0|1` 提供局部平行近似/双界面同机对照，`MYRENDERER_GLASS_DEBUG=9|10` 分别显示 Exit Surface Normal 与 Object ID。可重复验收命令为：

```powershell
cmake --build build-mingw --target glass2c-visual-regression
cmake --build build-mingw --target glass2c-benchmark
```

视觉目标重拍并比较 7 张 1920×1080 固定镜头图片；Benchmark 覆盖 1x/4x MSAA 与双界面 On/Off 的 CPU/GPU P50/P95、Draw Call 和显存估算。参考结果见 `docs/glass2c-volume.md`。

Glass-3 使用 `MYRENDERER_GLASS3_DEMO=1` 进入焦散固定场景，`MYRENDERER_CAUSTICS=0|1`、`MYRENDERER_CAUSTICS_MODE=0|1` 与 `MYRENDERER_TRANSMISSION_SHADOWS=0|1` 控制独立功能；`MYRENDERER_GLASS_DEBUG=11|12` 显示 Caustics 与 Transmission Shadow。可重复验收命令为：

```powershell
cmake --build build-release --target glass3-visual-regression
cmake --build build-release --target glass3-benchmark
```

视觉目标比较 6 张 1920×1080 固定镜头图片；Benchmark 输出 Off / Projector / Light-space 的独立 Caustics GPU P50/P95、整帧、Draw Call 与显存。参考结果见 `docs/glass3-caustics.md`。

Glass-4 增加 `MYRENDERER_GLASS_PRESET=0|1|2|3`、`MYRENDERER_TRANSMISSION=0|1`、`MYRENDERER_DISPERSION_ENABLED=0|1`、`MYRENDERER_DISPERSION` 与 `MYRENDERER_IOR`，并提供最终作品集验收目标：

```powershell
cmake --build build-release --target glass4-visual-regression
cmake --build build-release --target glass4-benchmark
cmake --build build-release --target glass4-nsight-capture
```

视觉回归覆盖 14 张 1920×1080 固定镜头图片；Benchmark JSON 输出所有活动 Pass 的 GPU P50/P95、整帧、Draw Call 和显存。参考结果与已知边界见 `docs/glass4-validation.md`。

GP-P1A Deferred 基线使用 `MYRENDERER_RENDER_PATH=0|1` 切换 Forward/Deferred，使用 `MYRENDERER_GBUFFER_DEBUG=0..4` 选择 Final/Albedo/Encoded Normal/Metallic-Roughness/Depth。固定画面与性能对照可重复执行：

```powershell
cmake --build build-release --target deferred-visual-regression
cmake --build build-release --target deferred-benchmark
```

视觉回归覆盖 6 张 1920×1080 固定镜头图片；Forward/Deferred 最终图 MAE 为 0.000517。RTX 4060 Laptop 的 4× MSAA 基线上，Forward / Deferred GPU Frame P50 分别为 1.435 / 1.860 ms，RenderTarget 估算分别为 291.2 / 469.1 MiB。当前单光源基线不宣称 Deferred 更快，多光源扩展性将在下一阶段验证。实现、GUI 调试和限制见 `docs/deferred-shading.md`。

GP-P1B 多光源压力场景可重复执行：

```powershell
cmake --build build-release --target local-lights-visual-regression
cmake --build build-release --target local-lights-benchmark
```

Benchmark 覆盖 Forward/Deferred × 8/32/64 灯。RTX 4060 Laptop 的 64 灯 GPU Frame P50 为 Forward 3.773 ms、Deferred 2.183 ms；Deferred 约快 1.73×，同时 RenderTarget 显存由 291.2 MiB 增至 469.1 MiB。画质、带宽估算边界与完整曲线见 `docs/local-light-stress.md`。

GP-P1C 实例提交、CPU 视锥剔除与 LOD 可重复执行：

```powershell
cmake --build build-release --target instance-stress-visual-regression
cmake --build build-release --target instance-stress-benchmark
```

Benchmark 分别记录 2,500 个 Sphere 的逐对象基线、Instancing、Instancing+Culling 和完整 LOD 四个阶段。RTX 4060 Laptop 的完整路径将 Draw Call 从 2,501 降至 19、CPU Frame P50 从 5.386 ms 降至 2.077 ms、GPU Frame P50 从 1.465 ms 降至 0.653 ms；算法、固定截图、三角形代价和边界见 `docs/instance-culling-lod.md`。

GP-P1D TAA 与 SSAO 可重复执行：

```powershell
cmake --build build-release --target screen-space-visual-regression
cmake --build build-release --target screen-space-benchmark
```

视觉矩阵覆盖 Baseline、SSAO Final/Debug、TAA Static/Moving、Motion Vector 与 History Weight。RTX 4060 Laptop、1080p、1×MSAA 下 Baseline / TAA moving / SSAO+TAA GPU P50 为 0.536 / 0.695 / 1.457 ms；算法、显存策略与对象运动边界见 `docs/taa-ssao.md`。

`gpu-smoke` 目标会运行材质场景以及“成功场景后加载错误资产”的恢复测试，确保 GPU 路径使用真实上下文且失败导入保留当前场景：

```powershell
cmake --build build-mingw --target gpu-smoke
```

Prism-5 提供三组可重复的作品集验收目标：

```powershell
cmake --build build-release --target prism5-visual-regression
cmake --build build-release --target prism5-benchmark
cmake --build build-release --target prism5-reel-frames
python tools/encode_prism5_reel.py build-release/prism5-reel-frames docs/media/prism5_demo_reel.mp4 --fps 24
```

视觉目标会重拍并比较 10 张 1920×1080 固定镜头图片；Benchmark 在关闭 VSync 后预热 60 帧、采样 180 帧，分别输出 7/15/21/31 光谱档位的 CPU/GPU P50/P95、Draw Call 与显存估算。参考结果见 `docs/prism5-validation.md`。

## 代码结构

```text
src/app/Application.*    窗口、主循环、后台导入、ImGui 与模块整合
src/app/FileDialog.*     Windows 原生模型文件选择器
src/asset/ModelData.h    格式无关的顶点、材质、子网格、Mesh 与节点数据
src/io/ModelImporter.h   统一模型导入接口与导入结果
src/io/AssimpImporter.*  DAE 与 glTF/GLB 静态模型导入适配
src/io/ObjLoader.*       OBJ 导入器：统一索引、UV、法线与 AABB
src/optics/PrismOptics.* 无 OpenGL 依赖的三棱镜求交、双界面折射、Fresnel 与 TIR
src/optics/PrismDemo.*   Prism 参数、四组光学 Preset、White Point 与实时求解入口
src/render/Camera.*      轨道相机
src/render/DebugGrid.*   世界网格、XYZ 轴线与 Debug Line GPU 绘制
src/render/EnvironmentMap.* HDR equirectangular 导入、Split-Sum IBL 预计算、程序化回退与天空盒
src/render/GBuffer.*     Deferred MRT、1x/4x MSAA Resolve、Attachment 绑定与显存估算
src/render/GpuModel.*    一个模型所拥有的 GPU Mesh 集合与统计
src/render/Mesh.*        VAO/VBO、多档 EBO、实例矩阵 Buffer 与 Instanced Draw
src/render/OpenGlDebug.* Debug 构建的 OpenGL 驱动诊断
src/render/OpticalPathDebugRenderer.* 世界空间光路、交点、法线、TIR 与能量调试层
src/render/PostProcessor.* TAA 历史重投影、HDR Bloom、ACES Tone Mapping 与最终 sRGB 输出
src/render/RenderTarget.*Opaque/HDR/MSAA 场景、可采样深度与最终 LDR 离屏 Framebuffer
src/render/Renderer.*    渲染状态、轻量 Pass 编排、相机参数与离屏绘制
src/render/SceneDrawList.* 透明排序、视锥平面、包围球与屏幕尺寸 LOD 决策
src/render/ShadowMap.*   方向光深度贴图
src/render/SsaoRenderer.* G-Buffer 半球遮蔽与深度感知滤波
src/render/Shader.*      GLSL 编译、链接与 uniform
src/render/Texture2D.*   GPU 纹理 RAII、图像解码、缓存和回退纹理
shaders/                 GPU 顶点和片元 Shader
assets/models/           模型资源
assets/icons/            SVG 源稿、PNG 预览和 Windows ICO
tests/AssetImportTests.cpp 无 OpenGL 上下文的 CPU 导入回归测试
tests/PrismOpticsTests.cpp 无 OpenGL 上下文的棱镜光路与数值稳定性测试
```
