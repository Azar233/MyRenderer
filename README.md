# MyRenderer

一个独立的 C++17 / OpenGL 3.3 GPU 光栅化渲染器。项目从 Dandelion 图形学实验框架中保留了模型、相机、材质与实时预览的设计思路，但不包含 CPU 软光栅化、光线追踪、物理模拟和半边网格模块。

当前版本可以加载 OBJ、DAE、glTF 2.0 和 GLB 静态模型，将网格与纹理上传到 GPU，按子网格材质范围通过 `glDrawElements` 完成多材质渲染，并在 Dear ImGui 界面中实时调整模型与渲染参数。

Post-MVP 阶段已将文件导入、CPU 模型数据、GPU 模型和渲染执行拆分；OBJ 使用独立轻量导入器，DAE 与 glTF/GLB 使用统一 Assimp 适配器，渲染层不依赖具体文件格式。

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
- Debug 构建在驱动支持时启用 OpenGL `KHR_debug` 诊断。
- Model/View/Projection 变换与基础 Blinn-Phong 光照。
- 离屏 Framebuffer 渲染视口、可切换 1x/4x MSAA Resolve 与解析后视口 PNG 导出。
- 像素风应用图标，覆盖 GLFW 标题栏、任务栏和 Windows 可执行文件资源。
- 顶部菜单、模型列表、Scene 面板、Inspector 面板和运行状态。
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

- Scene 面板：切换 `assets/models` 中的 OBJ、DAE、glTF/GLB，或输入其他模型路径。
- Inspector / Object：调整旋转、缩放、材质颜色 Tint 和光照系数，并查看 Mesh、子网格/Draw Call、材质、纹理、回退纹理与估算显存统计。
- Inspector / Renderer：切换线框、背面剔除、法线贴图、1x/4x MSAA、背景色、灯光、FOV 和 VSync。
- 渲染视口：鼠标右键拖动旋转相机，中键拖动平移，滚轮缩放；工具栏或 File 菜单可将当前解析后画面保存为 PNG。
- `Esc`：退出程序。

ImGui 窗口支持拖动与 Docking，布局会保存到运行目录下的 `MyRenderer.ini`。

## 支持范围与格式路线

当前正式支持静态 `.obj`、`.dae`、`.gltf` 和 `.glb`。OBJ 继续使用轻量的 tinyobjloader；DAE 与 glTF/GLB 通过统一 `ModelImporter` 接口接入 Assimp，第三方数据类型不会进入渲染层。

所有格式最终转换为相同的 `ModelData`、子网格、材质和纹理来源数据。渲染层统一解码并缓存外部或内嵌图像，基础色 Shader 将材质因子、可调 Tint 和基础色贴图相乘；没有贴图的材质使用白色纹理，无法解码或缺失的基础色贴图使用洋红棋盘并在状态区报告原因。基础色纹理使用 sRGB 内部格式，法线贴图保持线性数据；光照在线性空间完成，最终颜色仅进行一次 sRGB 编码。

OBJ、DAE 与 glTF/GLB 材质可使用切线空间法线贴图；缺失或退化 UV 会禁用对应顶点的切线扰动并回退到几何法线。高度图不会自动转换为法线贴图。当前暂不支持透明混合、阴影、PBR、骨骼动画和后处理；FBX 也未启用。

固定回归资产包括 `material_regression.obj`（基础色/法线贴图、常量材质、缺失纹理）、`degenerate_uv.obj`（退化 UV 法线贴图回退）、`textured_quad.dae`（DAE 外部纹理）和 `textured_triangle.gltf`（Data URI 内嵌纹理）。`uv_quadrants.ppm` 的四象限颜色用于检查 UV 方向，`normal_test.ppm` 用于检查切线空间法线扰动。

## 自动冒烟测试

隐藏窗口模式会创建真实 OpenGL 上下文、加载模型并渲染 5 帧后退出：

```powershell
$env:MYRENDERER_SMOKE_TEST = "1"
.\build-mingw\MyRenderer.exe .\assets\models\sphere.obj
Remove-Item Env:MYRENDERER_SMOKE_TEST
```

自动测试还可通过 `MYRENDERER_MSAA=1|4` 选择采样数，并用 `MYRENDERER_SCREENSHOT=<输出.png>` 在渲染后导出截图。交互模式下截图默认写入运行目录的 `screenshots` 文件夹。

## 代码结构

```text
src/app/Application.*    窗口、主循环、ImGui 与模块整合
src/asset/ModelData.h    格式无关的顶点、材质、子网格、Mesh 与节点数据
src/io/ModelImporter.h   统一模型导入接口与导入结果
src/io/AssimpImporter.*  DAE 与 glTF/GLB 静态模型导入适配
src/io/ObjLoader.*       OBJ 导入器：统一索引、UV、法线与 AABB
src/render/Camera.*      轨道相机
src/render/GpuModel.*    一个模型所拥有的 GPU Mesh 集合与统计
src/render/Mesh.*        VAO/VBO/EBO、顶点布局与子网格 Draw Call
src/render/OpenGlDebug.* Debug 构建的 OpenGL 驱动诊断
src/render/RenderTarget.*离屏 Framebuffer
src/render/Renderer.*    渲染状态、Shader、相机参数与离屏绘制
src/render/Shader.*      GLSL 编译、链接与 uniform
src/render/Texture2D.*   GPU 纹理 RAII、图像解码、缓存和回退纹理
shaders/                 GPU 顶点和片元 Shader
assets/models/           模型资源
assets/icons/            SVG 源稿、PNG 预览和 Windows ICO
```
