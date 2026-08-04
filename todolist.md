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
- [ ] Debug 构建注册 OpenGL debug callback（平台支持时启用）。

验收：出现可缩放的稳定窗口，背景色正确且控制台无 OpenGL 错误。

### 阶段 C：GPU 管线冒烟测试

- [x] 创建最小顶点/片元 Shader 文件。
- [x] 实现 `Shader` RAII 类：读取文件、编译、链接、错误日志、`use()`、uniform 设置。
- [ ] 暂时创建一个硬编码三角形，建立 VAO/VBO，并用 `glDrawArrays` 绘制。
- [x] 验证 Shader 文件路径在从源码目录和构建目录运行时均可解析。
- [ ] 冒烟测试通过后删除临时三角形代码，避免它混入正式 Mesh 路径。

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
- [ ] 首次验证时启用背面剔除；若 OBJ 绕序不一致，应允许临时关闭并记录原因。
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

## 6. MVP 暂不实现

- PBR、阴影、纹理、法线贴图、天空盒、后处理和多 Pass 渲染。
- MTL 材质的完整支持；首版使用统一材质颜色。
- 场景图、多个模型实例、动画、骨骼、FBX/glTF 等格式。
- ImGui 编辑器、文件选择器和复杂鼠标相机控制。
- CPU 软光栅化器、光线追踪器、BVH、物理模拟与多线程渲染队列。
- MSAA、离屏 Framebuffer、截图系统和性能分析 UI。

## 7. MVP 之后的优先级

1. Orbit Camera：鼠标旋转、缩放和观察模型。
2. MTL 与漫反射纹理，并处理 UV 和纹理路径。
3. 多 Mesh/多材质 Draw Call。
4. 帧率、渲染模式和光照参数的小型 ImGui 面板。
5. MSAA、Gamma 校正与 sRGB 工作流。
6. glTF 加载、PBR、阴影映射和后处理。

## 8. 建议的首轮开发顺序

严格按以下闭环推进，避免一次性搭完所有抽象后才发现管线不可用：

1. CMake 可构建。
2. GLFW 窗口可启动。
3. GPU 三角形可见。
4. OBJ 能解析并输出统计。
5. OBJ 顶点上传 GPU 并绘制。
6. 加入 MVP、深度测试和基础光照。
7. 自动取景、错误处理、双模型验证和 README。
