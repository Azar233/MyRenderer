# MyRenderer 路线图

> 重排日期：2026-09-16
>
> 项目定位：**C++ Module-driven Scene Rendering & Simulation Lab**——同一套 Scene / Asset / Material / Camera / Light 数据，支持实时 Raster PBR、实时 Stylized/NPR、CPU/GPU Path Tracing，以及由 Timeline、Render Job 和 C++ Scene/Simulation Module 驱动的专项场景模拟。

本文档是当前唯一活跃路线图。旧版按开发日期不断追加的清单已经压缩为“已完成里程碑”；只有“执行队列”中的未勾选项才代表当前承诺。

状态与优先级：

- `[x]`：已实现且有测试、固定图、文档或性能证据。
- `[ ]`：尚未完成。
- **P0**：当前连续执行，不再插入新的大型方向。
- **P1**：P0 结束后的下一条主线。
- **P2**：有价值但不阻塞主线的增强或研究验证。
- **舍弃**：不再保留为项目待办；只有出现新的明确需求才重新立项。

## 1. 当前结论

### 1.1 已具备的能力

| 能力 | 当前状态 | 结论 |
| --- | --- | --- |
| OpenGL Raster PBR | 实时 GUI 可用 | 已是稳定对照后端，继续维护，不再大规模扩张 OpenGL 架构 |
| Stylized / Toon / Outline | 实时 GUI 可用 | Toon 与屏幕空间描边已完成，尚缺组合效果、Preset、调试视图和性能分档 |
| CPU Reference Path Tracer | 静态/自动验收可用 | 材质、灯光、HDRI、体积、MIS、AOV、SAH、BLAS/TLAS、Adaptive Sampling 已闭环；尚未接入 GUI 渐进预览 |
| Raster / Path Traced / Difference | 三个固定 `.myscene` 可自动导出 | 作为跨算法诊断，不要求差异为零；旧固定图回归合同保持不变 |
| Editor Workspace | Docking、Hierarchy、Viewport、Inspector、Content Browser 已有基础 | 后续做成大视口为中心的渲染/模拟工作台，不扩展成游戏编辑器 |
| C++ Module / Batch Runtime | 未开始 | 先做静态 C++ Module API、显式参数注册、版本化 Render Job、Headless Batch 与固定时间步；DLL 动态加载后移 |
| GPU Path Tracing | 未开始 | 当前 OpenGL 3.3 不承载 Compute/硬件 RT；在独立 Vulkan 后端实现 |
| Denoising | 未开始 | 先做 AOV 引导的空间降噪，再做时序降噪；它降低方差，不替代正确采样 |
| ReSTIR | 未开始 | 只在基础 GPU PT 稳定后验证 ReSTIR DI；不提前承诺 ReSTIR GI/PT |
| MMIS | 不进入主线 | 当前标准 MIS 已有效降噪；“MMIS”需先绑定具体论文/算法和目标问题，不能视为通用去噪器 |

### 1.2 接下来只保留五条主线

1. 收口 Stylized/NPR，完成一个可发布的短周期视觉成果。
2. 把 CPU Path Tracer 接入 GUI 渐进预览，并建立低 SPP 降噪/采样实验。
3. 建立 C++ Module 驱动的 Editor Workspace、Headless Render Job、Timeline 与专项 Simulation Runtime。
4. 制作物理天空 + Gerstner 海面自然 Hero Scene，并通过 C++ 模块和 Render Job 批量生成昼夜与海况序列。
5. 建立 Vulkan Raster 基线，再进入 GPU Ray Query / Path Tracing / ReSTIR DI。

体积云、极光、FFT Ocean、浅水与完整天气不与上述主线并行开发。

### 1.3 UI 与工作流目标

界面采用“大视口 + Scene Explorer + Inspector + Content Browser”的渲染工作台布局；当前 ImGui Docking 外壳继续演进，不替换 UI 框架。推荐默认工作区：

| 区域 | 职责 |
| --- | --- |
| 顶部菜单/工具栏 | Scene、Render、Simulation、Window；Raster/CPU PT/GPU PT；Preview/Pause/Step/Reset/Bake/Render |
| 中央 Viewport | 实时或渐进渲染、Transform Gizmo、Beauty/AOV/Debug View、SPP 与任务进度 |
| Scene Explorer | Camera、Mesh、Light、Atmosphere、Fog、Ocean、Volume、Simulation Module |
| Inspector | Transform、Material、Lighting、Volume、Simulation 与 C++ 模块公开参数；不显示 Gameplay 组件 |
| 底部工作区 | Assets、Timeline、Modules、Render Queue、Log/Profile，多标签共享空间 |

Content Browser 只保留与渲染和模拟有关的分类：Scenes、Models、Materials、Textures、HDRI、Modules、Simulations、Caches、RenderJobs、Presets。C++ 源码使用 Visual Studio 等外部 IDE；MyRenderer 负责模块发现、参数显示、构建日志和运行控制，不自建代码编辑器。

运行状态只有 `Edit / Preview / Bake / Render`：Preview 使用可丢弃的运行态副本，Bake 写确定性缓存，Render 从固定场景或缓存输出；顶部三角按钮不定义为 Play Game。

## 2. 已完成里程碑

### 2.1 渲染器与资产基础（2026-08-05）

- [x] C++17 / CMake / OpenGL 3.3 Core 工程、GLFW/GLAD/GLM、RAII GPU 资源和 `KHR_debug`。
- [x] OBJ、DAE、glTF/GLB 静态资产导入；统一 `ModelData`、Mesh、Submesh、Material、Node 数据。
- [x] UV、切线、基础色/法线纹理、纹理缓存、内嵌/外部纹理和缺失纹理回退。
- [x] 线性工作流、sRGB 输入输出、1x/4x MSAA、截图导出和真实 OpenGL smoke test。
- [x] 后台资产导入、文件选择/拖放、结构化诊断、CPU/GPU 时间与资源统计。
- [x] Metallic-Roughness PBR、IBL、方向光阴影、HDR、Bloom、ACES Tone Mapping 和轻量多 Pass 编排。

### 2.2 玻璃、棱镜与焦散旗舰（2026-08-08 ～ 2026-08-25）

- [x] Forward Refractive Pass、透明排序、可采样 Opaque Color/Depth、粗糙背景折射和环境回退。
- [x] Transmission / IOR / Fresnel / TIR、双界面折射、几何厚度、Beer-Lambert、RGB 色散与对象配对。
- [x] 标准 Split-Sum IBL、Volume Glass Preset、固定机位调试视图与 1x/4x MSAA 验收。
- [x] Prism Spectrum：双界面 Snell 光路、连续波长采样、七色美术模式、HDR 光束、自动回归和 Demo Reel。
- [x] Projector 与 Light-space Caustics、彩色透射阴影、空间过滤、性能报告和 GPU Capture。

证据：[`docs/glass2c-volume.md`](docs/glass2c-volume.md)、[`docs/glass3-caustics.md`](docs/glass3-caustics.md)、[`docs/glass4-validation.md`](docs/glass4-validation.md)、[`docs/prism5-validation.md`](docs/prism5-validation.md)。

### 2.3 现代实时渲染基线 GP-P1（2026-08-25 ～ 2026-09-01）

- [x] Hybrid Deferred、G-Buffer 与逐附件调试，保留 Forward 对照。
- [x] Point/Spot 多光源压力场景；64 灯下 Deferred GPU P50 相对 Forward 约 `1.73×`。
- [x] Instancing、CPU Frustum Culling、三档 LOD；2,500 实例场景 Draw Call `2501 → 19`。
- [x] TAA、动态 Motion Vector、History Reprojection、Neighborhood Clamp、SSAO 与调试视图。
- [x] glTF Skin/Animation Sampling/GPU Skinning 最小闭环；Bind Pose、动画和权重调试可用。

证据：[`docs/deferred-shading.md`](docs/deferred-shading.md)、[`docs/local-light-stress.md`](docs/local-light-stress.md)、[`docs/instance-culling-lod.md`](docs/instance-culling-lod.md)、[`docs/taa-ssao.md`](docs/taa-ssao.md)、[`docs/gpu-skinning.md`](docs/gpu-skinning.md)。

### 2.4 Scene Rendering 共用基础 SR-P0（2026-09-02）

- [x] 轻量 Scene / Entity / Transform / Parent-Child、多对象选择、复制、删除、显隐和共享 Mesh 实例。
- [x] ImGui Docking 编辑器外壳、Hierarchy、Viewport、Object/Renderer Inspector 与基础 Content Browser 已具备，可在现有结构上扩展渲染工作区。
- [x] glTF Node 与 Mesh Geometry 解耦；SceneSnapshot 能复用真实场景、相机、材质和灯光语义。
- [x] 显式 Pass Context、OpenGL State Cache、GPU Debug Label 与失败安全 Shader Hot Reload。
- [x] 相机、对象和骨骼上一帧数据、统一 History Reset、Jitter 与动态 Motion Vector。
- [x] `renderer-regression-suite`、`renderer-benchmark-suite`、Windows CI、许可证清单与 CPack Release ZIP。
- [x] 旧 `cube.obj` 许可证待办已随项目/依赖/资产许可证清单收口。

证据：[`docs/scene-rendering-foundation.md`](docs/scene-rendering-foundation.md)。

### 2.5 CPU Reference Path Tracer SR-P1A～M（2026-09-03 ～ 2026-09-15）

| 阶段 | 已完成结果 |
| --- | --- |
| A | 只读 SceneSnapshot、Ray/AABB/Triangle、Surface Interaction、确定性 BVH |
| B–C | Progressive Accumulation、可取消后台任务、确定性 RNG、Lambert/GGX PBR、Russian Roulette |
| D–F | Emissive/Directional/Point/Spot NEE、Power-Heuristic MIS、HDRI 重要性采样、CPU 纹理/法线采样 |
| G–H | Dielectric Fresnel/TIR、IOR、Beer-Lambert Volume、Beauty + 7 组确定性 AOV |
| I–J | Tile 线程池、统计/Profile、16-bin SAH；测试场景 Triangle Test 降低约 `62.5%` |
| K–L | 共享 BLAS/TLAS、400 实例压力场景、95% 置信区间 Adaptive Sampling |
| M | 三个 `.myscene` 的同机位 Raster / Path Traced / Difference / Triptych / JSON / AOV 自动导出 |

标准 MIS 已有明确收益：8 SPP 面光场景 MSE `0.210596 → 0.135364`；16 SPP 高对比 HDRI 场景 MSE `1.65474 → 0.175639`。因此下一步应优化采样分布与降噪，不应把 MMIS 当成“消除噪点”的替代方案。

固定场景：

- `assets/scenes/10_reference_pathtracer_pbr_hdri.myscene`
- `assets/scenes/11_reference_pathtracer_lights.myscene`
- `assets/scenes/12_reference_pathtracer_volume.myscene`

证据：[`docs/reference-path-tracer.md`](docs/reference-path-tracer.md)。

### 2.6 Stylized / NPR SR-P2A～B（2026-09-16）

- [x] PBR / Stylized 实时切换、2～8 档 Toon Ramp、分层高光、Rim Light 与 Shadow Tint。
- [x] Forward / Deferred 共用风格参数，透明/玻璃继续使用物理路径。
- [x] View-space Relative Depth + G-Buffer Normal 的屏幕空间描边；Forward 使用 Depth-only 回退。
- [x] 描边宽度按像素定义，在 TAA 后合成；分辨率、TAA、透明边界与 On/Off 自动验收已覆盖。
- [x] `.myscene` 往返与 `stylized-acceptance` 已接入，输出只写构建目录，不改写既有固定图。

证据：[`docs/stylized-rendering.md`](docs/stylized-rendering.md)。

## 3. 活跃执行队列

### P0-A：先锁定回归基线（预计 2～4 天）

目标：在继续加效果前，确认当前工作树的所有“不能破坏”合同。

- [ ] 跑通 CTest、`path-tracing-regression`、`path-tracing-raster-comparison`、`stylized-acceptance` 与 `renderer-regression-suite`，保存一份当前结果矩阵。
- [ ] 审计现有 Raster 固定图中的 Glass-2C 基线漂移：先区分环境/场景选择/渲染状态问题与真实算法变化，不直接重拍覆盖。
- [ ] 若必须更新任何固定图，单独提交并记录原因、参数、硬件、旧/新差异；不得由后续功能目标隐式更新。
- [ ] 核对 README、参考路径追踪文档和 CMake 目标名称，确保所有验收命令可复制执行。

完成门槛：除已明确登记的 Glass-2C 问题外，现有固定图和逐位 Path Tracer 回归无新增漂移。

### P0-B：收口 Stylized/NPR SR-P2C（预计 1～2 周）

保留能形成完整视觉交付的组合功能，不继续扩展角色专用材质系统。

- [ ] 增加时序稳定的 Dither；提供开关、强度与调试视图。
- [ ] 增加 Height Fog，并明确与深度、透明物和天空的合成顺序。
- [ ] 增加 Color Grading LUT；Bloom 复用已有实现，只做风格化 Preset 集成。
- [ ] 完成 Clean Toon、Painterly、Night Aurora 三组 Preset，并写入 `.myscene`。
- [ ] 用材质展台、室内建筑/陈列场景、室外自然代理场景验证三类构图；不再强制新增角色资产。
- [ ] 增加 Lighting Bands、Rim、Outline、Dither、Fog、LUT 调试视图和 Low/High 两档 GPU 数据。
- [ ] 扩展 `stylized-acceptance`，继续只写构建目录，不触碰旧固定图。

完成门槛：同一机位一键切换 PBR 与三种 Preset；Forward/Deferred、两种分辨率、TAA、透明物边界均有自动验收和限制说明。

### P0-C：GUI CPU Progressive Path Tracing（预计 1～2 周）

这是“交互式参考预览”，目标是持续更新且不阻塞 GUI，不承诺 CPU 实时帧率。

- [ ] 新增 Raster / CPU Path Traced 视图切换；复用当前 SceneSnapshot 和 Camera，不建立第二套场景加载器。
- [ ] 复用现有 `RenderTask`、Tile 线程池和取消机制；后台只写 CPU staging image，主线程负责 OpenGL Texture 上传。
- [ ] 相机、Scene、Transform、Material、Light、分辨率或积分器设置变化时可靠取消并重启；旧任务不得覆盖新结果。
- [ ] 提供 1/2、1/4 与全分辨率预览，交互期间低分辨率，静止后逐步升档。
- [ ] UI 显示 SPP、进度、耗时、Ray/BVH 统计、Seed、Max Depth、AOV、Pause/Resume/Restart 和导出。
- [ ] 在固定 Seed/分辨率/SPP 下，GUI 最终结果与 CLI/自动验收输出逐位一致或 RMSE 为 0。
- [ ] 快速拖动相机和连续加载场景时 GUI 保持响应，无 use-after-free、陈旧纹理或退出卡死。

完成门槛：GUI 可连续看到噪声随 SPP 降低，取消/重启稳定，且 `path-tracing-regression` 的旧输出合同完全不变。

### P0-D：采样与降噪实验（预计 2～3 周）

先建立可量化基线，再决定算法复杂度。

- [ ] 为 1/2/4/8/16 SPP 保存 Raw Beauty、Albedo、Normal、Depth、Direct、Indirect、Variance；以 2048 或 4096 SPP 作为参考。
- [ ] 第一版实现 AOV 引导的 Spatial A-Trous / Cross-Bilateral Denoiser；边缘停止权重使用 Albedo、Normal 与 Depth。
- [ ] GUI Progressive 稳定后再实现 Temporal Accumulation + SVGF 风格方差估计、History Reprojection 与 Disocclusion Rejection。
- [ ] 分开评估 Direct 与 Indirect；报告 Raw/Denoised 的 RMSE、PSNR、SSIM、耗时、过度模糊、拖影和 Firefly。
- [ ] 将光源均匀离散选择升级为 Power-weighted Distribution / Alias Table；增加 GGX VNDF 采样并与现有采样做同预算对照。
- [ ] Firefly Clamp 只作为可选有偏模式，不用于“让指标好看”的正式无偏参考图。
- [ ] 只有在低 SPP 结果和 AOV 合同稳定后，才冻结 GPU Denoiser 所需的数据接口。

完成门槛：至少三个固定场景证明低 SPP 方差显著下降，同时报告细节损失与时序失败案例；不能只展示一张平滑后的静帧。

## 4. 下一阶段主线

### P1-0：C++ 模块驱动的渲染与模拟工作台（预计 4～7 周）

这个阶段先建立可自动运行的“场景时间与任务语义”，再让自然场景和 GPU 后端接入；核心扩展使用 C++ Scene/Simulation Module，不以制作蓝图、脚本节点、通用反射系统或游戏运行时为目标。

#### P1-0A：Editor Workspace 收口（预计 1～2 周）

- [ ] 固化可保存/恢复的默认 Dock Layout：中央 Viewport、Scene Explorer、Inspector、底部多标签工作区；允许用户拖动布局，不把左右位置写死进功能逻辑。
- [ ] 顶部工具栏增加 Render Mode 与 `Edit / Preview / Bake / Render` 状态，提供 Pause、Single Step、Reset、Render Frame、Render Sequence；不得复用含糊的 Play Game 语义。
- [ ] Viewport Overlay 统一显示 Backend、Render Mode、分辨率比例、SPP/Frame、Denoiser、任务进度和取消状态；Raster/CPU PT/GPU PT 共用同一位置。
- [ ] Scene Explorer 只展示渲染/模拟对象；Inspector 按 Transform、Material、Lighting、Atmosphere/Volume、Simulation、Module Parameters 分组。
- [ ] Content Browser 增加 Scenes、Models、Materials、Textures、HDRI、Modules、Simulations、Caches、RenderJobs、Presets 分类，以及搜索、筛选、刷新和缩略图缓存。
- [ ] 底部增加 Timeline、Modules、Render Queue、Log/Profile 标签；第一版 Modules 提供打开 Visual Studio、构建指定 CMake Target、启动/停止模块实例和编译错误定位。
- [ ] UI 只通过 EditorSession/Command 修改 Scene 与任务，不直接持有渲染线程或 GPU 资源生命周期。

#### P1-0B：Headless Batch 与 Render Job（预计 1～2 周）

- [ ] 把场景加载、Snapshot 捕获、Render Settings、帧推进与导出提取为无 ImGui 依赖的运行层；GUI 与 Batch 调用同一实现。
- [ ] 定义带 `schemaVersion` 的 Render Job：Scene、Renderer、Camera、Resolution、Frame Range/FPS、SPP/Depth/Seed、AOV、Output、Simulation Cache 和失败策略。
- [ ] 增加 `MyRendererBatch` 或等价 CLI，支持 `validate`、`render-frame`、`render-sequence`、`simulate/bake`；参数错误、模块错误、缺资源或缺产物必须返回非零。
- [ ] 输出路径支持 Frame Token 与原子写入；先复用 PNG/RGBE HDR，再增加线性 OpenEXR/AOV 序列，颜色空间和 Tone Mapping 必须写入元数据。
- [ ] Render Queue 显示 Pending/Running/Cancelled/Failed/Complete，支持安全取消和从已完成帧恢复；不得用 GUI 帧循环隐式决定任务进度。
- [ ] 同一 Job 从 GUI 和 CLI 执行必须使用相同相机、时间、Seed 与设置，并生成相同最终图和报告。

#### P1-0C：Timeline、C++ Module 与 Simulation Runtime（预计 2～3 周）

- [ ] 定义确定性 Timeline：Frame、Time、FPS、Start/End、固定 `deltaTime`、Loop 与 Scrub；渲染序列不依赖实时 GUI 帧率。
- [ ] 区分编辑态 Scene 与可丢弃 Runtime Scene；Preview/模块异常/Reset 不应污染未保存的编辑态，只有显式 Apply/Bake 才写回资产或缓存。
- [ ] 定义 `ISceneModule` / `ISimulationModule` 最小生命周期：`registerParameters/initialize/reset/fixedUpdate/bake/serialize`，以及 Seed、Fixed Step、输入依赖、输出 Cache 和版本号。
- [ ] 第一版把模块静态编译进独立 `MyRendererModules` 或等价 CMake Target，通过显式 Registry 按稳定字符串 ID 创建实例；不先处理 DLL ABI、卸载和二进制热补丁。
- [ ] 定义轻量 Module Manifest：稳定 ID、Display Name、CMake Target、Source Root、Module API Version 与 Build ID；Content Browser/Modules 面板只读取 Manifest，不扫描或解析 C++ 源码。
- [ ] 定义轻量 `ParameterRegistry`，支持 Bool/Int/Float/Color/Enum/Asset、默认值、范围和 Tooltip；Inspector 自动生成控件，`.myscene` 保存参数覆盖。
- [ ] 模块不得直接拥有 Editor Widget、OpenGL/Vulkan Context 或后台线程；通过受限 `SceneContext`、Job/Cancellation Token 与 Renderer API 协作。
- [ ] Simulation Cache 记录场景内容哈希、模块版本、Seed、时间步和帧范围；输入变化时拒绝静默复用陈旧缓存。
- [ ] Render Job 记录模块 ID、参数、Module API Version 与 Build ID；GUI Preview 和 Batch 必须加载相同模块配置。
- [ ] 支持模块 Initialize/Run/Stop/Reset、超时/取消、结构化日志和源码/编译错误定位；失败不能导致当前 GUI 场景或渲染上下文失效。
- [ ] 第一版采用“重新编译并重启应用/Batch 后恢复 Scene”的可靠迭代方式；只有该成本成为真实瓶颈时，才进入 P2-D DLL Plugin Reload。

P1-0 验收：一个固定 C++ Module 驱动场景与 24 帧参数动画，GUI Preview、CLI Batch 和重复运行共享同一 Timeline/Seed/Build ID；输出帧/AOV 与 Job 报告可复现，取消、模块异常和缓存失效均有自动测试。该阶段新增独立 `module-rendering-acceptance`，不改写既有固定图。

### P1-A：自然 Hero Scene——天空、室外阴影与 Gerstner 海面（预计 4～6 周）

- [ ] 实现 Rayleigh/Mie Atmosphere LUT 或等价可验证方案，统一太阳方向、天空、方向光、曝光和 Aerial Perspective。
- [ ] 完成稳定 3～4 级 CSM、Texel Snapping、Bounds 拟合、Bias 与 Cascade 调试；PCSS 仅作为后续质量档。
- [ ] 用 Projected Grid、Clipmap 或可解释的相机相关 LOD 承载大范围海面。
- [ ] 实现多组 Gerstner Waves，输出解析位移、法线、切线与速度；明确标注为 Wave Synthesis。
- [ ] 复用 Fresnel、IOR、Transmission、Beer-Lambert 与环境反射，增加水深、Foam、Whitecap 和 Underwater Fog。
- [ ] 水面接入 Shadow、Motion Vector、TAA 与调试视图，制作 Calm / Windy / Storm 三组海况。
- [ ] 将太阳时间、雾、风、波浪和相机轨迹暴露为 C++ Module 参数；通过 Render Job 输出固定昼夜/海况帧序列，而不是只保存手调静帧。

完成门槛：同一海岸场景能从正午平静海面切到日落风浪，天空、太阳、雾、水面与阴影方向一致；同一 C++ Module + Render Job 可重复输出参数动画，并有 Low/High GPU 预算。

### P1-B：Vulkan 与 GPU Path Tracing（预计 8～12+ 周）

独立后端通过 SceneSnapshot 共享数据；不先设计大一统 RHI。

1. [ ] Vulkan Raster Baseline：静态 glTF、Dynamic Rendering、Descriptor、上传、Frame-in-flight、同步验证和 RenderDoc Capture。
2. [ ] 定义 GPU Scene/Material/Texture 数据布局；唯一 Mesh 建 BLAS、Entity 建 TLAS，先支持静态与刚体 Transform Update。
3. [ ] 用 `VK_KHR_ray_query` 完成一种混合效果，优先 Ray-traced Shadow，并与 CSM 保留同机位质量/性能对照。
4. [ ] 用 `VK_KHR_ray_tracing_pipeline` 实现基础 GPU Path Tracer，对齐 SR-P1 的 Camera、PBR、Light、HDRI 与固定场景语义。
5. [ ] 加入每帧 1 SPP、Temporal Accumulation、AOV 与 SVGF；以 720p、Depth 3～4、约 30 FPS 作为首轮测量目标，不作为未测先承诺的硬指标。
6. [ ] 对 `10/11/12_reference_pathtracer_*.myscene` 输出 CPU/GPU 同机位图和误差报告；区分浮点/算法差异与实现错误。
7. [ ] Vulkan 作为现有 Workspace/Render Job 的新 Backend 接入；同一 Job 能在 GUI 或 Batch 运行，不维护独立的 Vulkan Demo 场景格式。

完成门槛：OpenGL、CPU PT 与 Vulkan GPU PT 能加载同一场景；GPU 输出随样本收敛并与 CPU Reference 趋势一致；GUI 中可交互预览且有明确硬件、画质和性能边界。

### P1-C：ReSTIR DI 可行性验证（GPU PT 基线之后，预计 2～4 周）

- [ ] 建立 8/64/256/1024 灯固定压力场景，先记录传统 NEE/MIS 的时间、方差与可见性成本。
- [ ] 实现 Candidate Generation、Weighted Reservoir Update、Temporal Reuse、Spatial Reuse 与 Visibility Test。
- [ ] 明确目标分布、权重与 Bias Correction；通过禁用 Temporal/Spatial 的消融定位收益来源。
- [ ] 比较相同 Ray Budget 下的 NEE/MIS 与 ReSTIR DI：RMSE/SSIM、稳定性、Ghosting、Disocclusion、GPU 时间和显存。
- [ ] 只有 DI 在多光源动态场景中证明稳定收益，才评估 ReSTIR GI/PT；否则停在可复现实验结论。

判断标准：ReSTIR 不是“所有场景都比传统光追更好”。它最可能在低 SPP、多光源、动态场景提高直接光采样效率；少量灯光或离线高 SPP 场景可能得不偿失。

## 5. P2 可选分支

### P2-A：体积云与极光

- [ ] 统一 Half/Quarter Resolution Ray March、Blue-noise Jitter、Depth-aware Upsample、Temporal Reprojection 与 History Rejection。
- [ ] 先以 Local/Height Fog 验证密度、吸收、单次散射和深度合成，再进入体积云。
- [ ] 体积云实现 Shape/Detail Noise、Weather Map、Sun March、近似多重散射、云影和 Low/High 档。
- [ ] Aurora 作为同一体积框架的发光帘幕案例；只做可导演视觉模型，不宣称磁层物理模拟。

进入条件：P1-A Hero Scene 已稳定，且该阶段不会阻塞 Vulkan/GPU PT。

### P2-B：FFT Ocean、浅水与 GPU Weather

- [ ] Gerstner 海面达到画质/性能基线后，再用 Vulkan Compute 实现 Tessendorf FFT Ocean，并做同场景对照。
- [ ] Shallow Water 作为独立局部 Height-field Solver；第一版不与远海 FFT 双向耦合。
- [ ] Rain/Snow、3D Noise 和体积预计算在 Compute 后端实现；OpenGL 只保留低档或离线回退。

进入条件：Vulkan Compute/同步/资源生命周期已由 P1-B 证明稳定。

### P2-C：CPU Reference 增量维护

以下项目只在 GPU 对齐、真实资产或实验明确需要时实现，不单独抢占主线：

- [ ] 粗糙 Dielectric Transmission、透明阴影与嵌套介质。
- [ ] 完整 glTF Sampler/Mip/UV Transform 与 Alpha Visibility。
- [ ] TLAS Binned SAH、Transform-only Refit、环境旋转/Portal 与更鲁棒的 Adaptive Sampling。
- [ ] Skinned Mesh 动态 Bounds 和上一帧 Skin 数据只做兼容维护；动态 BLAS Refit/Rebuild 留到 GPU RT 专项测量。

### P2-D：动态 C++ Plugin 与 DCC 协作

- [ ] 在 P1-0 稳定后提供轻量 Blender Helper：导出选中资产为 glTF、生成/更新 `.myscene`、提交 Render Job 并打开结果目录；不在 MyRenderer 内复制建模工具。
- [ ] 当静态模块的重启成本成为明确瓶颈后，将稳定 Module API 编译为独立 DLL；插件入口使用版本化接口，Core 继续拥有内存、线程和 GPU 资源。
- [ ] 安全 Reload 流程必须先暂停 Timeline/Job、销毁实例、卸载旧 DLL、加载带 Build ID 的新 DLL、恢复参数，再重启 Preview；不实现 UE 式二进制 Live Coding/Object Reinstancing。
- [ ] DLL 边界避免传递 STL 容器所有权和 OpenGL/Vulkan 对象；为 API Version、编译器/配置不匹配、加载失败和旧缓存建立明确诊断。
- [ ] Python 只保留为可选的外部批处理、实验汇总或 Blender Helper 实现语言，不成为 MyRenderer Runtime、Scene Module 或可复现 Render Job 的依赖。
- [ ] 若需要更复杂后期，只增加可测试的固定 Compositor Pass/Module 参数，不实现通用合成节点图。

进入条件：静态 C++ Module + Batch 已稳定；只有重复编译/重启显著阻碍模块迭代时才承担 DLL ABI 与安全卸载成本。

## 6. 明确后移或舍弃

| 项目 | 决策 | 原因 / 重新进入条件 |
| --- | --- | --- |
| Face Map | 舍弃 | 角色专用，当前没有角色 Hero Scene；需要角色项目时再立项 |
| Direction Map | 后移 | Toon Ramp 已能完成当前 Preset；只有构图控制明显不足时再加 |
| Inverted Hull Outline | 舍弃 | 屏幕空间描边已覆盖当前需求，避免维护第二条主路径 |
| 蓝图 / Visual Scripting / Behavior Tree | 舍弃 | 场景逻辑由 C++ Module、Timeline 与确定性 Simulation Runtime 驱动 |
| 通用节点材质/合成编辑器 | 舍弃 | 工作量大且不服务当前五条主线；使用固定 Shader/Pass 与代码扩展 |
| 完整 ECS / 通用 Render Graph / 大一统 RHI | 舍弃 | 维持轻量 Scene、Pass Context 与独立 Vulkan 后端 |
| Animation Blending / State Machine / IK / Root Motion / Morph / FBX Animation | 后移且默认不做 | 保留 glTF Skin 最小兼容；自然场景不需要角色系统 |
| 完整 TA Asset Audit 工具 | 后移 | 只有求职主方向切换为 TA Pipeline 时恢复；轻量 Blender Export/Launch Helper 保留在 P2-D |
| Python 作为 Runtime/Scene Module 主接口 | 舍弃 | 核心扩展统一使用 C++；Python 只允许作为可选外部工具，不进入正式渲染复现合同 |
| 内置代码 IDE/调试器 | 舍弃 | 使用 Visual Studio 等外部 IDE；MyRenderer 只负责构建、运行、日志和参数面板 |
| UE 级反射/UHT、Object Reinstancing 与二进制 Live Coding | 舍弃 | 第一版使用显式 Registry/Parameter Metadata；动态 DLL Reload 只有在 P2-D 有真实需求时实现 |
| Blender 式建模、雕刻、UV、复杂绑定工具 | 舍弃 | 继续使用 Blender 等 DCC 制作资产，通过 glTF 与 Helper 协作 |
| PCSS | 可选 | 稳定 CSM 完成后再评估画质/成本，不阻塞室外场景 |
| 完整 Weather Simulation、SPH、3D Navier-Stokes、磁流体极光 | 舍弃 | 只做视觉可信、可解释、可测的实时近似 |
| FFT Ocean 与 Shallow Water 同时开发 | 舍弃 | 先 Gerstner，再分别验证频谱海面与局部动力学 |
| 首版 GPU RT 支持 Skinned BLAS | 后移 | 首版只做静态与刚体；动态几何作为独立成本实验 |
| ReSTIR GI/PT | 后移 | ReSTIR DI 先证明价值 |
| MMIS 主线实现 | 舍弃 | 名称/目标未绑定具体方法；标准 MIS + 更好采样 + Denoiser 优先 |
| 通用游戏物理、音频、网络、AI 和 Gameplay | 舍弃 | 项目是渲染/模拟实验室；只实现 Hero Scene 明确需要的专项 Solver |

## 7. 持续交付与作品集任务

这些任务不再作为单独的大阶段，而是每个 P0/P1 里程碑的完成条件。

- [ ] 每个旗舰阶段至少提供 Hero Shot、同机位 On/Off、Debug View、性能表、失败案例和复现命令。
- [ ] GPU 阶段保存带 Pass/Resource Label 的 RenderDoc 或 Nsight Capture；性能报告同时写 CPU、GPU、显存与画质代价。
- [ ] UI/C++ Module 阶段提供默认 Workspace 截图、完整批处理示例、Module API/Parameter Schema、Render Job Schema、错误示例和 24 帧最小可复现序列。
- [ ] README 首屏只保留定位、最强功能、Hero 图和快速运行；长篇算法与基准放入 `docs/`。
- [ ] 每个可发布里程碑生成无需源码目录和联网的 Windows Release ZIP，并记录硬件要求与已知限制。
- [ ] Demo Reel 在有两个以上完整新阶段后统一更新，避免每个小功能重复剪辑。

## 8. 不可破坏的验收合同

### 8.1 固定图与输出策略

- `path-tracing-regression`：程序化固定场景、原 Reinhard 输出和既有参考路径保持不变；用于逐位/数值正确性。
- `path-tracing-raster-comparison`：加载三个正常 `.myscene`，固定 `256×256 / 512 SPP / Depth 8 / Seed 20260915 / ACES+sRGB / 5×5 Median 展示差异`；用于产物完整性和跨算法趋势，不以零差异判定通过。
- `stylized-acceptance`：固定机位验证 PBR/Toon、Forward/Deferred、分辨率、TAA 与 Outline；输出只写构建目录。
- `renderer-regression-suite`：继续覆盖既有 Glass、Prism、Deferred、Lighting、Culling/LOD、TAA/SSAO、Skinning 与 Scene 基线。
- 新功能默认新增测试目标和输出目录，不复用旧目标去重拍历史基线。

### 8.2 阶段完成定义

每个阶段必须同时满足：

1. 同一 Scene、Camera、分辨率和参数可重复运行。
2. 自动入口在失败、缺产物或元数据不一致时返回非零。
3. 正确性图、Debug/AOV、性能数据和已知限制齐全。
4. 新功能默认关闭或有兼容默认值，旧 `.myscene` 行为不变。
5. 全量回归无未解释漂移；固定图变化必须独立审查。
6. Timeline、C++ Module 或 Simulation 功能必须记录固定 FPS/Time Step/Seed/API Version/Build ID 与输入哈希；GUI 和 Batch 不允许各自解释任务语义。

常用验收入口：

```powershell
ctest --test-dir build-ci-msvc -C Release --output-on-failure
cmake --build build-ci-msvc --config Release --target path-tracing-regression
cmake --build build-ci-msvc --config Release --target path-tracing-raster-comparison
cmake --build build-ci-msvc --config Release --target stylized-acceptance
cmake --build build-ci-msvc --config Release --target renderer-regression-suite
cmake --build build-ci-msvc --config Release --target renderer-benchmark-suite
```

## 9. 实际执行顺序

1. **P0-A 回归基线审计**。
2. **P0-B Stylized/NPR 收口**。
3. **P0-C GUI CPU Progressive Preview**。
4. **P0-D AOV Denoising + Sampling 改进**。
5. **P1-0 C++ 模块渲染工作台**：Workspace → Headless Render Job → Timeline → 静态 Scene/Simulation Module → Parameter Registry。
6. **P1-A 物理天空 + CSM + Gerstner Water Hero Scene**，同时交付由 C++ Module 驱动的可重复昼夜/海况序列。
7. **P1-B Vulkan Raster → Ray Query → GPU Path Tracing + SVGF**，接入同一 Workspace/Render Job。
8. **P1-C ReSTIR DI 对照实验**。
9. 根据作品集缺口在 **P2 体积**、**P2 水体/天气 Compute** 与 **P2-D 动态 C++ Plugin/DCC 协作**中只选一个继续。

同一时间最多进行一个底层系统任务和一个小型视觉打磨任务。任何阶段没有自动回归、性能预算和限制说明时，不启动下一个大型效果。
