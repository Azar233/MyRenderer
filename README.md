# MyRenderer

一个独立的 C++17 / OpenGL 3.3 GPU 光栅化渲染器。项目从 Dandelion 图形学实验框架中保留了模型、相机、材质与实时预览的设计思路，但不包含 CPU 软光栅化、光线追踪、物理模拟和半边网格模块。

当前版本可以加载 OBJ、DAE、glTF 2.0 和 GLB 静态模型，将网格上传至 VAO/VBO/EBO，通过 GLSL 顶点/片元 Shader 和 `glDrawElements` 完成 GPU 渲染，并在 Dear ImGui 界面中实时调整模型与渲染参数。

Post-MVP 阶段已将文件导入、CPU 模型数据、GPU 模型和渲染执行拆分；OBJ 使用独立轻量导入器，DAE 与 glTF/GLB 使用统一 Assimp 适配器，渲染层不依赖具体文件格式。

## 已实现

- OpenGL 3.3 Core Profile 与 GLFW 窗口。
- OBJ 三角化、多 shape 合并、缺失法线生成和 AABB 自动取景。
- 通过导入专用 Assimp 6.0.5 支持 DAE、glTF 2.0/GLB 的多 Mesh、节点变换、UV0、切线和材质关联。
- GPU 顶点/索引缓冲、深度测试、背面剔除和线框模式。
- Debug 构建在驱动支持时启用 OpenGL `KHR_debug` 诊断。
- Model/View/Projection 变换与基础 Blinn-Phong 光照。
- 离屏 Framebuffer 渲染视口。
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
- Inspector / Object：调整旋转、缩放、材质颜色和光照系数。
- Inspector / Renderer：切换线框、背面剔除、背景色、灯光、FOV 和 VSync。
- 渲染视口：鼠标右键拖动旋转相机，中键拖动平移，滚轮缩放。
- `Esc`：退出程序。

ImGui 窗口支持拖动与 Docking，布局会保存到运行目录下的 `MyRenderer.ini`。

## 支持范围与格式路线

当前正式支持静态 `.obj`、`.dae`、`.gltf` 和 `.glb`。OBJ 继续使用轻量的 tinyobjloader；DAE 与 glTF/GLB 通过统一 `ModelImporter` 接口接入 Assimp，第三方数据类型不会进入渲染层。

所有格式最终转换为相同的 `ModelData`、子网格和材质数据。当前已经导入 UV、切线、材质颜色和纹理引用，但纹理解码、GPU 纹理和按材质着色属于下一阶段，因此现阶段仍以统一基础材质显示模型。

当前暂不支持纹理显示、多材质着色、阴影、PBR、骨骼动画和后处理；FBX 也未启用。

## 自动冒烟测试

隐藏窗口模式会创建真实 OpenGL 上下文、加载模型并渲染 5 帧后退出：

```powershell
$env:MYRENDERER_SMOKE_TEST = "1"
.\build-mingw\MyRenderer.exe .\assets\models\sphere.obj
Remove-Item Env:MYRENDERER_SMOKE_TEST
```
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
shaders/                 GPU 顶点和片元 Shader
assets/models/           模型资源
assets/icons/            SVG 源稿、PNG 预览和 Windows ICO
```
