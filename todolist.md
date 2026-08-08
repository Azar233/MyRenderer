# MyRenderer：OpenGL GPU 光栅化渲染器开发大纲

## 1. MVP 目标

使用 C++ 与 CMake 从零搭建一个独立、精简的 OpenGL 渲染器。首个可交付版本需要：

- 在 Windows 上通过 CMake 完成配置、编译和运行。
- 创建 OpenGL 3.3 Core Profile 窗口与渲染循环。
- 从命令行参数或默认资源路径加载一个 `.obj` 模型。
- 将顶点和索引上传到 GPU，并通过 `glDrawElements` 绘制模型。
- 使用顶点着色器完成 Model/View/Projection 变换。
- 使用片元着色器完成基础光照，能够辨认模型的立体结构。
- 启用深度测试，模型表面遮挡关系正确。
- 窗口缩放后画面比例正确，按 `Esc` 可退出。

### MVP 完成标准（Definition of Done）

- [x] 在全新 `build` 目录中，CMake 配置和编译无错误（已用 MinGW Debug 验证）。
- [x] 启动程序后能看到模型，而不是黑屏、纯色屏或只有一个测试三角形。
- [x] 正确解析 OBJ 的位置、法线和面索引；无顶点法线时可生成可用法线。
- [x] 非三角形面会在加载阶段被三角化。
- [x] 模型自动居中并缩放到相机视野内，避免因 OBJ 尺寸差异而不可见。
- [x] 深度遮挡正确，调整窗口大小后模型不拉伸。
- [x] Debug 构建下没有 OpenGL 初始化、Shader 编译/链接或 OBJ 加载错误。
- [x] 至少用两个 OBJ 验证：已实际运行 cube、bunny、sphere 三个模型。

## 2. MVP 技术选型

| 模块 | 选型 | 用途 |
| --- | --- | --- |
| 语言 | C++17 | 保持实现简单并获得稳定的标准库支持 |
| 构建 | CMake 3.20+ | 管理目标、资源和第三方依赖 |
| 图形 API | OpenGL 3.3 Core Profile | 使用 GPU 固定功能光栅化阶段与可编程 Shader |
| 窗口/上下文 | GLFW | 创建窗口、OpenGL 上下文并处理基础输入 |
| OpenGL 函数加载 | GLAD | 加载 OpenGL 函数指针 |
| 数学库 | GLM | 向量、矩阵及 Model/View/Projection 变换 |
| OBJ 加载 | tinyobjloader | 仅处理 MVP 所需的 OBJ，避免引入完整 Assimp |
| 依赖接入 | CMake `FetchContent` | 首次配置时自动获取并参与构建 |

> 依赖版本应在 `CMakeLists.txt` 中固定 tag/commit，避免后续构建结果漂移。若开发环境不能联网，再切换为仓库内 `third_party/` 子模块，不同时维护两套依赖方案。

## 3. 与旧实验框架的关系

旧路径 `E:/dandelion-main/src/render` 中值得保留的是渲染概念，而不是原有 CPU 实现：

- 保留：MVP 矩阵、顶点/法线数据、三角形索引、基础 Blinn-Phong/Lambert 光照思想。
- 替换：CPU `VertexProcessor` → GLSL 顶点着色器。
- 替换：CPU `Rasterizer`、重心坐标插值和 CPU 深度缓冲 → OpenGL GPU 光栅化、插值和深度测试。
- 替换：CPU `FragmentProcessor` → GLSL 片元着色器。
- 精简：旧 `RenderEngine` 的多渲染器选择 → 单一实时 OpenGL `Renderer`。
- 不迁移：Whitted 光线追踪、线程队列、自旋锁、CPU FrameBuffer、BVH、物理模拟、半边结构和 ImGui 编辑器。
- 参考但不直接复制：旧项目的 VAO/VBO/EBO 和 Shader 封装；新项目只实现 MVP 所需的 RAII 封装。

## 4. 建议目录结构

```text
MyRenderer/
├─ CMakeLists.txt
├─ README.md
├─ todolist.md
├─ assets/
│  └─ models/
│     └─ cube.obj
├─ shaders/
│  ├─ basic.vert
│  └─ basic.frag
└─ src/
   ├─ main.cpp
   ├─ app/
   │  ├─ Application.h
   │  └─ Application.cpp
   ├─ io/
   │  ├─ ObjLoader.h
   │  └─ ObjLoader.cpp
   └─ render/
      ├─ Camera.h
      ├─ Camera.cpp
      ├─ Mesh.h
      ├─ Mesh.cpp
      ├─ Renderer.h
      ├─ Renderer.cpp
      ├─ Shader.h
      └─ Shader.cpp
```

MVP 数据流：

```text
.obj 文件
  -> ObjLoader（CPU：解析、三角化、补法线、计算 AABB）
  -> MeshData（positions + normals + indices）
  -> Mesh（上传 VAO/VBO/EBO）
  -> Renderer（设置状态、uniform、Draw Call）
  -> Vertex Shader -> GPU Rasterization -> Fragment Shader
  -> GLFW 窗口
```

## 5. 实施 Todo List

### 阶段 A：工程骨架与可构建性

- [x] 创建根 `CMakeLists.txt`，设置项目名、C++17 和 Debug/Release 配置。
- [x] 用 `FetchContent` 接入 GLFW、GLAD、GLM、tinyobjloader，并锁定版本。
- [x] 创建可执行目标 `MyRenderer`，集中声明源文件和链接依赖。
- [x] 设置编译警告；MSVC 使用 `/W4`，其他编译器使用 `-Wall -Wextra -Wpedantic`。
- [x] 将 `shaders/` 和 `assets/` 在构建后复制到可执行文件附近，并定义稳定的开发资源根目录。
- [x] 添加最小 `main.cpp`，验证程序可配置、可编译、可启动。

验收：

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

### 阶段 B：窗口与 OpenGL 上下文

- [x] 初始化 GLFW，显式请求 OpenGL 3.3 Core Profile。
- [x] 创建窗口，并在失败时输出明确错误后退出。
- [x] 使用 GLAD 加载 OpenGL 函数并打印 GPU、驱动和 OpenGL 版本。
- [x] 实现主循环：处理事件、清屏、交换缓冲。
- [x] 使用实时 framebuffer/视口尺寸同步 `glViewport`。
- [x] 实现 `Esc` 退出和 GLFW 资源释放。
- [x] Debug 构建注册 OpenGL debug callback（通过 `KHR_debug`，平台支持时启用）。

验收：出现可缩放的稳定窗口，背景色正确且控制台无 OpenGL 错误。

### 阶段 C：GPU 管线冒烟测试

- [x] 创建最小顶点/片元 Shader 文件。
- [x] 实现 `Shader` RAII 类：读取文件、编译、链接、错误日志、`use()`、uniform 设置。
- [x] 使用正式 OBJ Mesh 路径完成 GPU 管线冒烟测试，不再引入临时硬编码三角形。
- [x] 验证 Shader 文件路径在从源码目录和构建目录运行时均可解析。
- [x] 正式代码中不存在临时三角形路径。

验收：窗口中显示一个由 GPU 管线绘制的三角形；故意制造 Shader 语法错误时能看到可定位的日志。

### 阶段 D：OBJ 加载与 CPU 侧网格数据

- [x] 定义 `Vertex`：至少包含 `glm::vec3 position` 与 `glm::vec3 normal`。
- [x] 定义 `MeshData`：`std::vector<Vertex>`、`std::vector<uint32_t> indices` 和 AABB。
- [x] 用 tinyobjloader 加载 OBJ，并开启三角化。
- [x] 正确展开 OBJ 独立的位置/法线索引，建立 OpenGL 可使用的统一顶点索引。
- [x] 对缺失法线的模型按三角形累计并归一化，生成平滑顶点法线；退化三角形会安全跳过并报告。
- [x] 支持一个 OBJ 内的多个 shape，MVP 中合并成一个 `MeshData`。
- [x] 检查空模型、越界索引、文件不存在和解析失败，并输出文件路径与原因。
- [x] 根据 AABB 计算中心和尺寸，得到自动居中/缩放的模型矩阵。
- [ ] 添加一个小型、许可证明确的 `assets/models/cube.obj` 作为固定测试资源。

验收：控制台输出顶点数、三角形数和 AABB；无论 OBJ 是否自带法线，都能得到有效的 GPU 输入数据。

### 阶段 E：Mesh GPU 资源管理

- [x] 实现 `Mesh` RAII 类，拥有 VAO、VBO、EBO 并禁止复制。
- [x] 将交错布局的 `Vertex` 数据一次性上传到 VBO，将索引上传到 EBO。
- [x] 配置属性位置：`location 0 = position`、`location 1 = normal`。
- [x] 实现 `Mesh::draw()`，内部绑定 VAO 并调用 `glDrawElements(GL_TRIANGLES, ...)`。
- [x] 保证 OpenGL 资源在上下文销毁前释放，避免析构顺序错误。

验收：OBJ 数据替代硬编码三角形后，Draw Call 成功且没有 `GL_INVALID_*` 错误。

### 阶段 F：相机、变换与基础光照

- [x] 实现轨道 `Camera`，提供位置、观察目标、FOV、near/far 和 View 矩阵。
- [x] 根据 framebuffer 宽高实时计算 Projection 矩阵，防止窗口缩放后拉伸。
- [x] 顶点 Shader 接收 `uModel`、`uView`、`uProjection`，输出世界空间位置和正确变换后的法线。
- [x] 法线使用 normal matrix（`transpose(inverse(mat3(model)))`）变换。
- [x] 片元 Shader 实现环境光、方向光 Lambert 和 Blinn-Phong 高光。
- [x] 设置清屏色、`GL_DEPTH_TEST` 和深度缓冲清理。
- [x] 已提供可切换的背面剔除；默认关闭以兼容绕序不一致的 OBJ。
- [x] 使用 AABB 自动取景，让不同尺寸的模型初次加载即可见。

验收：模型具有稳定的明暗层次，旋转模型矩阵后明暗和遮挡变化符合预期。

### 阶段 G：应用整合与 MVP 验证

- [x] `Application` 负责窗口生命周期、资源加载和逐帧循环。
- [x] 程序支持 `MyRenderer.exe [model.obj]`；未传参时加载默认 `cube.obj`。
- [x] 启动日志输出实际模型路径、GPU/OpenGL 信息、网格统计和错误原因。
- [x] 为模型增加自动缓慢旋转，并提供轨道相机观察深度与光照。
- [x] 分别验证 MinGW Debug 与 Release 构建，并运行真实 OpenGL 冒烟测试。
- [x] 在 NVIDIA RTX 4060 Laptop GPU / OpenGL 3.3 环境完成运行验证。
- [x] 编写 `README.md`：环境要求、构建命令、运行方式、按键和已知限制。
- [x] 已完成 cube 与 bunny 的 GUI 截图验收，确认菜单、面板、取景、硬/平滑法线和模型切换。

最终运行示例：

```powershell
.\build\Debug\MyRenderer.exe .\assets\models\cube.obj
```

## 6. Post-MVP 路线清单

状态约定：`[x]` 已完成，`[~]` 进行中，`[ ]` 待开始。每个阶段完成后在阶段下方追加完成日期、验证命令和结果摘要，不以“代码已写完”代替验收。

### 阶段 1：MVP 基线收口

- [x] 让路线清单与现有 Orbit Camera、ImGui、离屏 Framebuffer、背面剔除等实现保持一致。
- [x] 明确格式策略：OBJ 保持兼容；DAE 与 glTF 通过统一资产导入层接入，不继续扩展 OBJ 专用架构。
- [x] Debug 构建请求 Debug Context，并在驱动支持时注册 `KHR_debug` 回调。
- [x] MinGW Debug 构建无错误。
- [x] 使用真实 OpenGL 上下文完成 OBJ 五帧冒烟测试。

> 完成注释（2026-08-05）：阶段 1 已完成。`cmake --build build-mingw --parallel` 构建通过；隐藏窗口加载 `sphere.obj` 渲染 5 帧并以退出码 0 结束；NVIDIA 驱动报告 `KHR_debug` 已启用。回调捕获到一条 Shader 状态重编译性能提示（NVIDIA 消息 131218），不是渲染错误，留到阶段 2 调整状态设置后复测。

### 阶段 2：渲染边界与统一资产数据

- [x] 从 `Application` 提取 `Renderer`，让应用层只负责窗口、循环和 UI 编排。
- [x] 定义格式无关的 `ModelData`、`MeshData`、`SubmeshData`、`MaterialData` 和节点变换。
- [x] 定义统一 `ModelImporter` 接口；现有 `ObjLoader` 作为第一个实现接入。
- [x] 为位置、法线、UV0、切线、索引范围和材质编号确定稳定的数据约定。
- [x] 保证 cube、bunny、sphere 的模型统计与真实 OpenGL 渲染路径无回归。

验收：替换内部数据结构后，现有 OBJ 仍可加载；渲染代码不包含 `.obj`、`.dae`、`.gltf` 等格式判断。

> 完成注释（2026-08-05）：阶段 2 已完成。新增统一 `ModelData`/`ModelImporter`、`GpuModel` 与 `Renderer`；OBJ 已能生成 UV0、材质元数据和按 shape/材质范围划分的子网格。MinGW Debug 构建通过，cube（12 面）、bunny（5002 面）、sphere（320 面）均使用真实 OpenGL 上下文渲染 5 帧并以退出码 0 结束。`cow.dae` 会由导入器注册表明确报告暂不支持，未发生崩溃。NVIDIA 131218 性能提示在首次 Draw Call 仍会出现，确认属于驱动按状态编译 Shader 变体，不影响阶段验收。

### 阶段 3：DAE 与 glTF 2.0 静态模型导入

- [x] 在统一导入接口下接入多格式模型库，不让第三方类型泄漏到渲染层。
- [x] 支持 DAE 的多 Mesh、节点变换、法线、UV0 和材质关联。
- [x] 支持 glTF/GLB 的静态 Mesh、节点变换、UV0、材质和纹理引用。
- [x] Scene 面板展示并允许加载 `.obj`、`.dae`、`.gltf`、`.glb`。
- [x] 保留原 `ObjLoader` 回归路径；Assimp 构建只启用 COLLADA 与 glTF 导入器。

验收：`bunny_hole.dae`、`cow.dae` 至少各完成一次可视化验收；`dragon2.dae` 完成加载压力测试；增加一个带 UV 的 glTF/GLB 固定测试资产。

> 完成注释（2026-08-05）：阶段 3 已完成。接入官方 Assimp 6.0.5 的 import-only 构建，新增 `AssimpImporter`，节点全局变换会静态烘焙到 GPU Mesh，同时保留内部节点结构。`bunny_hole.dae`（2503 顶点/4968 面）与 `cow.dae`（2930 顶点/5856 面）完成真实窗口可视化验收；`dragon2.dae`（1082810 顶点/360944 面）在约 2.01 秒内完成加载、五帧渲染并正常退出；新增带 UV0、材质与节点变换的 `textured_triangle.gltf`，导入和渲染通过。OBJ 三模型回归仍由独立 `ObjLoader` 保证。

### 阶段 4：UV、纹理与多材质渲染

- [x] 实现 `Texture2D` RAII、纹理解码、缓存和缺失纹理回退。
- [x] 按子网格和材质范围提交 Draw Call。
- [x] 支持 DAE 外部纹理与 glTF 外部、Data URI、GLB 内嵌纹理。
- [x] Shader 支持基础色纹理，并兼容只有常量颜色的旧模型。
- [x] Inspector 展示当前模型的 Mesh、材质与纹理统计。
- [x] 为带 UV 的 Spot 奶牛固定资产绑定 `spot_texture.png`，复现眼睛、口鼻、耳朵和身体斑纹。

验收：同一模型至少两个材质可正确显示；UV 朝向、纹理路径、缺失纹理和无纹理回退均有固定测试资产。

> 完成注释（2026-08-05）：阶段 4 已完成。新增格式无关 `TextureData`、`Texture2D`/`TextureCache`、按子网格材质绑定和基础色贴图 Shader；Assimp 的压缩/原始内嵌纹理路径统一覆盖 glTF Data URI 与 GLB，外部路径覆盖 DAE/glTF，OBJ 继续解析 MTL 纹理。新增 `material_regression.obj`（三材质、外部纹理、常量色、故意缺图）、`textured_quad.dae`（外部纹理）并为 `textured_triangle.gltf` 增加 Data URI 图像；`spot_triangulated_good.obj` 通过新增 MTL 正式绑定 `textures/spot_texture.png`。MinGW Debug 与 Release 构建通过；Debug 下 cube、bunny、sphere、bunny_hole、cow、dragon2、纹理 glTF、纹理 DAE、多材质 OBJ 和 Spot 纹理奶牛均用真实 OpenGL 上下文渲染五帧并以退出码 0 结束。缺失纹理会记录原因并使用洋红棋盘，不破坏当前场景。

### 阶段 5：颜色空间与渲染质量

- [x] 建立线性空间计算和 sRGB 输入/输出约定，移除含义不清的重复 Gamma 处理。
- [x] 为离屏 RenderTarget 增加可配置 MSAA 与 Resolve。
- [x] 增加法线贴图所需的切线空间，并处理缺失/退化 UV。
- [x] 增加截图导出，作为视觉回归验证基础。

验收：纯色、基础色纹理和法线贴图样例颜色正确；1x/4x MSAA 可切换且窗口缩放无错误。

> 完成注释（2026-08-05）：阶段 5 已完成。基础色纹理使用 `GL_SRGB8_ALPHA8` 自动解码，法线贴图保持线性采样，材质 Tint 先转换到线性空间，Blinn-Phong 光照后仅在最终输出执行一次 sRGB 编码；离屏 RenderTarget 支持 1x/4x MSAA、颜色 Resolve 和解析后 PNG 导出。OBJ 导入器会生成带手性的切线，Shader 通过 TBN 使用法线贴图；缺失或退化 UV 会记录诊断并安全回退到几何法线。新增 `normal_test.ppm` 与 `degenerate_uv.obj` 固定回归资产；MinGW Debug/Release 构建均通过，并通过真实 OpenGL 上下文验证基础色/法线贴图、缺图回退、退化 UV、1x/4x MSAA 与截图导出。

### 阶段 6：加载体验、诊断与性能

- [x] 增加文件选择器和拖放加载。
- [x] 将导入错误按文件、节点、Mesh、材质和纹理分层显示。
- [x] 增加 CPU/GPU 帧时间、Draw Call、三角形和纹理内存统计。
- [x] 为 CPU 资产导入增加自动化测试，为 GPU 路径保留真实上下文冒烟测试。

验收：错误资产不会破坏当前场景；大 DAE 加载期间有明确状态；性能数据可在 UI 中查看。

> 完成注释（2026-08-05）：阶段 6 已完成。Windows 原生文件选择器与 GLFW 文件拖放均接入统一加载入口；CPU 导入通过 `std::async` 在后台执行，Scene 面板显示文件大小、已用时间和活动进度，只有 CPU 验证与主线程 GPU 上传全部成功后才事务式替换当前场景。导入诊断按 File、Node、Mesh、Material、Texture 五级结构化显示；Renderer 使用四槽 OpenGL `GL_TIME_ELAPSED` 查询环避免同步阻塞，并在 UI 中显示 CPU/GPU 帧时间、Draw Call、三角形、纹理内存与最近加载耗时。新增 `MyRendererAssetTests` CTest 和 `gpu-smoke` 目标；MinGW Debug/Release 构建及 CPU 测试均通过，真实 OpenGL 下材质渲染、大 DAE 后台加载和错误资产场景保留测试通过。

> 交互增强（2026-08-05）：新增可独立开关的 XZ 地面网格、带箭头的世界 XYZ 轴线，以及视口左下角随轨道相机旋转的 XYZ 方向指示器；X/Y/Z 固定使用红/绿/蓝颜色，并把 Debug Line Draw Call 纳入运行统计。

> 对象变换增强（2026-08-05）：Object 面板新增世界坐标 Position XYZ；模型矩阵统一为 `Translate × Rotate × Scale × Normalize`，导入模型的 AABB 中心先归一到局部原点，默认 Position 固定为 `(0, 0, 0)`。世界网格和 XYZ 轴恢复到 `Y=0` 的真实原点，Frame model 会对准当前 Position，Reset transform 同时恢复原点并重置相机。

### 阶段 7：PBR 与多 Pass

- [x] 实现 glTF 金属度/粗糙度 PBR 材质。
- [x] 增加环境贴图、IBL、阴影映射和天空盒。
- [x] 在出现第二个真实渲染 Pass 后再引入轻量 Pass 编排。
- [x] 增加 Tone Mapping、Bloom 等可切换后处理。

验收：使用标准 PBR 测试模型完成材质、IBL、阴影和后处理的对照截图。

> 完成记录（2026-08-05）：glTF 2.0 metallic-roughness 因子与 G/B 打包纹理已接入 Cook-Torrance GGX；新增程序化 HDR Cubemap、近似 IBL、天空盒、2048² 方向光 PCF 阴影；渲染流程按 `Shadow map → HDR scene → Bloom + tone map` 编排，支持 ACES、曝光和 Bloom 开关。新增 `pbr_material_test.gltf` 回归资产、CPU 导入断言与 `stage7_pbr_full.png` / `stage7_pbr_baseline.png` 对照截图。

## 7. 当前格式决策

- OBJ：继续支持，适合几何调试和最小回归资产；不再作为材质与场景能力的主设计目标。
- DAE：为了兼容仓库现有 Dandelion 资产，在阶段 3 纳入支持范围。
- glTF 2.0/GLB：作为后续纹理、材质和 PBR 的主要交换格式。
- FBX：暂不列入近期验收；只有出现明确资产需求时再开启。

## 8. 面向图形程序 / Technical Artist 求职的后续路线图（2026-08-08）

### 8.1 当前项目判断

结论：当前项目可以继续作为作品集主项目的 base，建议把它定位成“实时渲染器 + 资产审阅工具”，而不是扩张成完整游戏引擎。现有实现已经能证明 C++、OpenGL、GPU 光栅化、资产导入、PBR、多 Pass、调试 UI 和基础性能统计能力；下一阶段最需要补的是标准完整性、可复现的性能证据、面向美术的工作流和作品集呈现。

| 维度 | 当前状态 | 作品集判断 |
| --- | --- | --- |
| 渲染基础 | OpenGL 3.3、PBR、阴影、IBL、Bloom、Tone Mapping、MSAA | 已超过入门 Demo，可作为后续功能的可靠基线 |
| 资产管线 | OBJ / DAE / glTF / GLB、后台 CPU 导入、纹理缓存、诊断 | 架构方向正确，但 glTF 核心材质语义尚不完整 |
| 工程质量 | CMake、RAII、CPU 测试、GPU smoke、KHR_debug、GPU Timer | 有工程意识，但缺 CI、视觉回归、基准场景和 GPU Capture 证据 |
| 编辑器 / 工具 | Docking UI、Inspector、文件选择、拖放、截图、调试网格 | 能用，但还不是面向美术生产的资产审阅与调试工具 |
| 图形程序匹配度 | C++ / GLSL / 渲染管线基础较完整 | 需要补现代 GPU 技术、系统化 profiling 和高负载优化案例 |
| TA 匹配度 | 材质显示、导入诊断和实时参数已有基础 | 需要补资产校验、材质调试、热重载、批处理和 DCC / 商用引擎工作流 |
| 对外展示 | README 以功能文字为主，仓库内有截图但首页没有视觉入口 | 当前最大短板之一；招聘方无法快速看到效果、架构和性能结论 |

路线原则：先完成所有人共用的 P0，再在“图形程序”与“TA”中选一个主方向。主方向做 1 个有深度、可量化的旗舰案例，副方向只补 1 个能证明协作能力的工具，不同时铺开所有高级效果。

### 8.2 P0：把现有 base 收口为作品集级基线（最高优先级，预计 2～4 周）

#### 架构与可维护性

- [ ] 拆分 `Application.cpp`：至少分离窗口/生命周期、Scene 面板、Inspector、Viewport、导入任务和作品集 Demo 控制；UI 层不直接管理 GPU 资源生命周期。
- [ ] 将目前只保存名称与 lambda 的 `RenderPassSequence` 升级为显式 Pass 上下文：声明输入、输出、视口、清理方式和 GPU Debug Label；此时先不做通用 Render Graph。
- [ ] 为 OpenGL 状态增加集中管理或状态缓存，明确 Depth、Blend、Cull、Polygon Mode 的进入/退出状态；连续切换阴影、线框、天空盒和透明材质后不得出现状态泄漏。
- [ ] 增加 Shader 热重载：监视 GLSL 文件时间戳，编译失败时保留上一份可用 Program，并在 UI 显示文件、行号和编译错误。
- [ ] 将“单个当前模型”扩展为最小 Scene / Entity 列表，支持多个对象、独立 Transform、选择、删除、复制、显隐和层级节点；不要为此引入完整 ECS。

#### 渲染正确性与标准兼容

- [ ] 补全 glTF 2.0 核心材质：Occlusion、Emissive、`alphaMode`（OPAQUE / MASK / BLEND）、`alphaCutoff`、`doubleSided`、Sampler 的 wrap/filter；透明物体先做稳定的后向前排序。
- [ ] 不再把节点全局变换永久烘焙进顶点；保留 glTF Scene / Node 层级和实例关系，同一 Mesh 被多个 Node 引用时只上传一份 GPU 几何。
- [ ] 将当前“程序化 Cubemap + 近似 IBL”升级为标准 Split-Sum IBL：HDR equirectangular 导入、Diffuse Irradiance、Prefiltered Specular Cubemap、BRDF LUT；提供近似版与标准版对照截图。
- [ ] 改进方向光阴影：根据相机/场景 Bounds 拟合 Light Frustum，加入可调 Bias、Peter-panning / Acne 调试视图；随后再实现 3～4 级 CSM，不先堆更软的滤波。
- [ ] 增加渲染调试视图：Albedo、World Normal、Roughness、Metallic、AO、Emissive、Depth、Shadow Cascade、Overdraw；每个视图在 Inspector 中可直接切换。

#### 测试、性能与交付

- [ ] 建立视觉[回归测试](https://vibe-hub.org/regression-test)：固定资产、相机、分辨率和 Renderer Settings，输出 PNG，并以像素误差 / SSIM 阈值和差异热图判定；允许显卡差异的小容差，不做逐字节比较。
- [ ] 增加 `renderer-benchmark` 场景和命令行模式：固定分辨率、关闭 VSync、预热后采样至少 300 帧，导出 CPU frame、GPU frame、Draw Call、Triangles、纹理显存和 P50 / P95。
- [ ] 用 RenderDoc 与 Nsight Graphics 各保存一份可复现 Capture / 报告；先判断 CPU-bound 或 GPU-bound，再记录一个真实瓶颈的假设、修改、前后数据和结论。
- [ ] 在关键 Pass 增加 `KHR_debug` 分组和对象 Label，让 GPU Capture 中直接显示 Shadow、Scene、Bloom、Tone Map 及纹理/FBO 名称。
- [ ] 增加 Windows CI：Debug/Release 配置、编译、`ctest`、格式/静态检查；真实 GPU smoke 保留为本机或自托管任务，不在无 GPU Runner 上伪造通过。
- [ ] 清点第三方库与测试资产许可证，补 `LICENSE`、`THIRD_PARTY_NOTICES.md` 和每个外部资产的来源；解决 `cube.obj` 当前“文件存在但许可证未明确”的旧待办。
- [ ] 打包可直接运行的 Windows Release ZIP，首次启动不依赖源码目录或联网；缺少资源时给出可定位错误。

P0 验收：干净机器解压即可运行；标准 glTF 材质测试场景显示正确；Shader 改坏后界面继续显示上一帧正确材质并报告错误；CI 通过；固定视觉回归通过；README 能链接到一份包含硬件、分辨率和优化前后数据的性能报告。

### 8.3 跨方向旗舰 Demo：Spectral Glass & Real-time Caustics

这个阶段放在 P0 的必要渲染基础之后、图形程序/TA 分线之前。它不要求先完成 Deferred、骨骼动画或 Vulkan；不透明物体以后可以进入 Deferred，水晶仍通过 Forward Refractive Pass 绘制。目标参考管线：

```text
Shadow / Depth
→ Opaque HDR Scene
→ Resolve Opaque Color + Sampleable Depth
→ Forward Refractive Glass
→ Additive Caustics
→ Bloom
→ Tone Mapping
```

#### Glass-0：折射管线基础

- [x] 将 HDR Opaque Scene Color 和 Depth 改为可采样纹理；4x MSAA 模式同时 Resolve Color、Depth 和 Stencil。
- [x] 将现有 HDR Scene 拆成 Opaque Pass 与 Forward Refractive Pass，后者位于 Bloom / Tone Mapping 之前。
- [x] 使用独立 Opaque Color 输入与 HDR Scene 输出，避免折射 Pass 同时采样和写入同一纹理形成 Framebuffer Feedback。
- [x] 增加透明/折射渲染队列、后向前排序，以及独立的 Depth Test、Depth Write、Blend 状态。
- [ ] 场景支持同时放置水晶主体、接收焦散的地面和辅助展示物体。

> Glass-0 进度（2026-08-08）：`RenderTarget` 已拆分 Opaque HDR Color、最终 HDR Scene Color 和可采样 `GL_DEPTH24_STENCIL8`；1x 直接写入纹理附件，4x MSAA 同时 Resolve Color/Depth/Stencil。`Renderer` 当前按 `Opaque HDR scene → Forward transparent/refractive scene → Bloom/Tone map` 执行；已接入 glTF OPAQUE/MASK/BLEND、Alpha Cutoff、双面法线、透明子网格后向前排序、Depth Test 开启/Depth Write 关闭和标准 Over 混合。新增 `alpha_material_test.gltf` 覆盖两个重叠 BLEND 子网格；MinGW 构建、CTest、1x/4x 真实 OpenGL smoke 与截图验证通过。下一项为多对象场景与真实接收地面。

#### Glass-1：玻璃反射与折射

- [ ] 增加 Dielectric Transmission 材质路径，支持 IOR、Transmission、Roughness 和 Fresnel。
- [ ] 使用 Snell 定律计算折射方向并处理 Total Internal Reflection。
- [ ] 实现屏幕空间折射：根据法线、IOR 和厚度采样 Opaque Scene Color / Depth；屏幕外或追踪失败时回退到 Prefiltered Environment Cubemap。
- [ ] 为粗糙玻璃生成 Opaque Scene Color Mip，按 Roughness 采样模糊背景或预过滤环境。
- [ ] 增加 Reflection、Refraction、IOR 与 Refracted UV 调试视图。

#### Glass-2：厚度、体积吸收与光谱色散

- [ ] 支持均匀 Thickness 与 Thickness Texture，并通过前/后表面深度 Pass 估算闭合模型厚度。
- [ ] 使用 Beer-Lambert Law 实现 Attenuation Color / Distance，避免透明物体呈现为无体积的彩色塑料。
- [ ] 分别计算 R/G/B 折射率形成波长相关的色散，并提供 Dispersion / Abbe Number 控制。
- [ ] 区分材质光谱色散与全屏 Chromatic Aberration；后者只能作为镜头效果，不能代替玻璃折射。
- [ ] 可选增加 Thin-film Iridescence，并提供 Thickness、Transmittance、RGB Refraction Offset 调试视图。

#### Glass-3：彩色焦散与透射阴影

- [ ] 第一版实现可控 Caustics Projector / Decal，以 HDR 浮点纹理和 Additive Blend 快速复现地面彩虹。
- [ ] 增加彩色透射阴影，避免高透射玻璃继续投射纯黑阴影。
- [ ] 图形程序进阶版实现 Light-space Caustics：折射入射光并把 RGB 能量累积到接收表面。
- [ ] 为焦散增加强度、尺度、方向、锐度和动画控制，并测试漏光、离屏失败、低采样密度与闪烁。
- [ ] 增加时序稳定或空间过滤，并记录画质、GPU 时间和显存代价。

#### Glass-4：展示、测试与验收

- [ ] 制作专用水晶模型、白色地面、黑色背景和高对比 HDRI 展示场景。
- [ ] 提供 Glass、Dispersion、Caustics 独立开关和相同机位的 On / Off 对照截图。
- [ ] 建立固定相机视觉回归，覆盖 IOR、Thickness、Dispersion、Caustics 和 1x/4x MSAA。
- [ ] 输出 1080p 下各 Pass 的 GPU 时间、Draw Call 和显存占用，并保存带 Pass 标记的 RenderDoc / Nsight Capture。
- [ ] 为 TA 展示准备至少 3 个可复用玻璃 Preset；为图形程序展示准备算法、失败案例和优化前后报告。

Glass 阶段验收：Glass-2 完成后水晶主体应具有稳定的反射、体积、内部折射和 RGB 色散；Glass-3 完成后地面出现与光源/物体关系一致的彩色焦散，才视为达到参考图的完整效果。

### 8.4 图形程序主线（Graphics Programmer Track）

#### GP-P1：可观测的现代实时渲染能力（优先做）

- [ ] 增加 G-Buffer / Deferred Shading 路径，并保留当前 Forward 路径作对照；至少包含 Albedo、Encoded Normal、Metallic/Roughness、Depth，支持逐附件调试。
- [ ] 建立多光源压力场景：点光 / 聚光、至少三档灯光数量；记录 Forward 与 Deferred 在同一机器同一画面下的 GPU 时间、带宽和 Draw Call 差异。
- [ ] 实现实例化、CPU Frustum Culling 和 LOD 选择；用同一 Mesh 的大规模实例场景证明提交与几何优化，不以空场 FPS 作为结果。
- [ ] 从 SSAO、TAA、SSR 中选择两项实现，其中优先 TAA：需要 Motion Vector、Halton Jitter、History Reprojection、Neighborhood Clamp、静止/运动 Ghosting 对照。
- [ ] 增加骨骼动画最小闭环：glTF Skin、Joint/Weight、Animation Sampling、GPU Skinning；提供 bind pose、动画和骨骼/权重调试视图。
- [ ] 给每项优化建立 Before / After Capture；报告必须同时写画质代价、CPU/GPU/显存变化，不能只写“FPS 提升”。

#### GP-P2：旗舰方向三选一（只选一个做深）

- [ ] **推荐：Vulkan 后端。** 复用现有格式无关资产层与场景层，使用 Vulkan 1.4、Dynamic Rendering、Timeline Semaphore、Vulkan-Hpp RAII；先完成同一 glTF 场景与 OpenGL 的画面一致性，再做资源上传、Frame-in-flight、同步验证和 RenderDoc Capture。不要一开始设计“大一统 RHI”。
- [ ] **备选：GPU-Driven Rendering。** 将 OpenGL 基线提升到支持 Compute / SSBO / Multi-Draw Indirect 的版本，实现 GPU Frustum/Occlusion Culling、Indirect Command 生成和实例批次；用 1k / 10k / 100k 实例曲线展示扩展性。
- [ ] **备选：高级阴影与全局光照。** 完成稳定 CSM、PCSS 或 EVSM，再实现 Probe / DDGI 风格的动态间接光近似；以稳定性、漏光、时间抖动和性能作为主要评估，不只展示静帧。

GP-P2 验收：有一篇独立技术说明，包含问题定义、算法/资源生命周期图、关键 Shader 或同步设计、GPU Capture、失败尝试、硬件环境和可复现实验数据；面试时能在 10 分钟内讲清楚为什么这样设计。

### 8.5 TA 主线（Technical Artist Track）

#### TA-P1：把 Viewer 变成资产审阅与材质调试工具（优先做）

- [ ] 增加 Asset Audit 面板，对 Mesh、材质、纹理和节点做[数据校验](https://vibe-hub.org/data-validation)：三角形/顶点数、退化三角形、缺失/越界 UV、无效切线、负缩放、材质槽、透明模式、纹理尺寸/格式/Mip/估算显存、缺失引用和命名规则。
- [ ] 为校验规则增加 Warning / Error 阈值配置，并支持一键导出 JSON / CSV / Markdown 报告；同一资产重复导入得到稳定结果。
- [ ] 增加 Material Inspector：逐通道预览、贴图替换、数值 Override、UV Tiling/Offset、法线强度、Alpha Cutoff、双面开关；保存为非破坏性的 Material Instance 配置。
- [ ] 增加“问题定位”操作：点击诊断即可选中 Node / Mesh / Material，并在视口高亮对应对象、线框、UV Seam 或错误顶点。
- [ ] 增加批处理模式：扫描文件夹、并行导入、汇总失败与预算超限资产；后台任务可取消，界面保持响应。
- [ ] 增加 Blender 小工具或脚本：导出选中物体为 glTF、调用 MyRenderer 校验、回传报告路径；这比继续支持更多文件格式更能体现 TA Pipeline 能力。

#### TA-P2：视觉旗舰方向二选一（只选一个做成完整案例）

- [ ] **推荐：Stylized Material / NPR 套件。** Toon Ramp、可控 Rim Light、描边（Inverted Hull 或 Screen-space）、分层高光、Face/Direction Map、雾与 Color Grading；提供美术参数预设、不同角色/场景适配、画质与性能档位。
- [ ] **备选：GPU VFX 套件。** Compute 粒子、Emitter、Curve/Gradient、Flipbook、Soft Particle、Depth Collision、Ribbon 或 Trail；提供 Overdraw、粒子数、CPU/GPU 时间、LOD、Pooling 和预算可视化。

#### TA-P3：生产工作流证据

- [ ] 为旗舰效果制作 3 个可复用 Preset，而不是只适配一个模型；参数命名、范围和 Tooltip 让陌生美术可以独立使用。
- [ ] 制作“错误资产 → 自动报告 → 定位 → 修复 → 重新导入”的 30～60 秒无旁白流程视频。
- [ ] 另做一个小型 Unreal Engine 5 或 Blender 对照案例，复现同一材质/效果并说明参数映射；独立渲染器证明底层理解，商用引擎案例证明可进入生产协作。
- [ ] 为资产预算写明确平台档位，例如 PC Low / High 的三角形、纹理尺寸、显存和 Shader Complexity 阈值，并在面板中实时显示是否超标。

TA 路线验收：一个未参与开发的使用者能在文档指导下导入资产、读懂问题、调整材质、保存预设并导出报告；作品集同时展示最终画面、工具交互和性能预算，不只放 Shader 静帧。

### 8.6 作品集包装与求职材料（两条路线都要做）

- [ ] 重写 README 首页：首屏放 Hero 图 / GIF、一句话定位、3～5 个最强能力、快速运行；把长篇构建细节下移。
- [ ] 提供中英双语说明，至少保证英文版包含 Overview、Features、Architecture、Controls、Build、Benchmarks、Known Limitations 和 Roadmap。
- [ ] 增加渲染帧流程图、资产数据流图和核心类关系图；图必须与当前代码一致，不画未来架构。
- [ ] 为 PBR / IBL、阴影、后处理、调试视图和旗舰功能制作相同机位的 On / Off 对照；现有 `stage7_pbr_full.png` 只能作为开发记录，需要重新制作有构图、布光和材质层次的展示场景。
- [ ] 录制 60～90 秒 Demo Reel：5 秒内看到最终效果，中段展示调试视图 / 工具，结尾展示性能数据与项目链接；避免长时间拖 UI 参数。
- [ ] 增加 `docs/architecture.md`、`docs/rendering.md`、`docs/benchmarks.md` 和 `docs/asset-pipeline.md`，每篇只解释真实实现与取舍。
- [ ] 建立 Release 页面：可执行包、Demo 视频、硬件要求、已知限制、测试状态；为招聘方提供无需编译的体验入口。
- [ ] 每个旗舰功能都准备一段面试讲稿：需求、约束、方案、失败点、调试手段、性能数据、下一步；不只背算法定义。

### 8.7 推荐执行顺序（默认选择）

1. P0 架构拆分、Shader 热重载、glTF 材质完整性、标准 IBL。
2. P0 视觉回归、Benchmark、GPU Debug Label、RenderDoc / Nsight 前后对比。
3. 若以水晶参考图为近期目标：完成 Glass-0～Glass-3，再根据主投方向强化算法性能报告或材质工具体验。
4. 若主投图形程序且不以水晶 Demo 为旗舰：先做 Deferred + 多光源 + Culling/LOD，再选择 Vulkan 作为旗舰。
5. 若主投 TA 且不以水晶 Demo 为旗舰：先做 Asset Audit + Material Inspector + Blender 脚本，再选择 NPR 套件作为旗舰。
6. 最后集中完成英文 README、展示场景、Demo Reel、技术文章和 Release 包。

暂不优先：继续增加 FBX 等文件格式、完整 ECS、物理/音频/网络、从零制作通用节点材质编辑器、在 Vulkan 基线未稳定前做实时光追。它们工作量大，但对当前作品集主叙事的增益低。

### 8.8 调研依据（访问于 2026-08-08）

- Ubisoft 的 [3D Graphics Engineer / Rendering Programmer](https://www.ubisoft.com/en-us/company/careers/search/744000113760857-3d-graphics-engineer-rendering-programmer-graphics-programmer) 明确强调运行时渲染管线、可维护 C++、渲染工具和性能瓶颈分析。
- Ubisoft 的 [Senior Render Programmer](https://www.ubisoft.com/en-us/company/careers/search/744000129436368-senior-render-programmer-tom-clancy-s-the-division-2-) 强调 C++ / Shader、图形 API、3D 数学、跨硬件、显存/性能意识和 GPU 调试。
- Ubisoft 的 [Junior Technical Artist](https://www.ubisoft.com/en-us/company/careers/search/744000125831490-junior-technical-artist-rainbow-six-siege) 把资产技术校验、预算、内容管线、工具、模板和文档列为核心职责。
- PlayStation 的 [Graphics Engineer](https://careers.playstation.com/senior-game-engineer-graphics/job/6003126004) 同时要求渲染系统、性能优化，以及面向设计师/美术的组件和工具，说明两条路线的共同交集是“可用工具 + 可测性能”。
- Khronos [glTF 2.0 Specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html) 是材质、纹理、Sampler、Skin、Animation 与 Scene 语义的验收基准。
- Khronos 的 [Vulkan Tutorial](https://docs.vulkan.org/tutorial/latest/00_Introduction.html) 当前以 Vulkan 1.4、Dynamic Rendering、Timeline Semaphore、Slang 和现代 C++ 为教学基线，可作为 GP-P2 的版本选择依据。
- NVIDIA [Nsight Graphics Features](https://developer.nvidia.com/nsight-graphics-features) 覆盖 Frame Capture、GPU Trace、Shader Profiling 和 GPU Pipeline State 检查，适合作为性能案例的证据工具。

> 岗位页面会随招聘状态变化；本节提取的是长期能力信号，不把某一条在招职位当作唯一目标岗位。
