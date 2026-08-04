# MyRenderer

一个独立的 C++17 / OpenGL 3.3 GPU 光栅化渲染器。项目从 Dandelion 图形学实验框架中保留了模型、相机、材质与实时预览的设计思路，但不包含 CPU 软光栅化、光线追踪、物理模拟和半边网格模块。

当前 MVP 可以加载 OBJ，将网格上传至 VAO/VBO/EBO，通过 GLSL 顶点/片元 Shader 和 `glDrawElements` 完成 GPU 渲染，并在 Dear ImGui 界面中实时调整模型与渲染参数。

## 已实现

- OpenGL 3.3 Core Profile 与 GLFW 窗口。
- OBJ 三角化、多 shape 合并、缺失法线生成和 AABB 自动取景。
- GPU 顶点/索引缓冲、深度测试、背面剔除和线框模式。
- Model/View/Projection 变换与基础 Blinn-Phong 光照。
- 离屏 Framebuffer 渲染视口。
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

- Scene 面板：切换 `assets/models` 中的 OBJ，或输入其他 OBJ 路径。
- Inspector / Object：调整旋转、缩放、材质颜色和光照系数。
- Inspector / Renderer：切换线框、背面剔除、背景色、灯光、FOV 和 VSync。
- 渲染视口：鼠标右键拖动旋转相机，中键拖动平移，滚轮缩放。
- `Esc`：退出程序。

ImGui 窗口支持拖动与 Docking，布局会保存到运行目录下的 `MyRenderer.ini`。

## 支持范围

MVP 只正式支持 `.obj`。目录中的 `.dae` 会显示为暂不支持；后续可通过 Assimp 扩展为 DAE/FBX/glTF，但不会让 MVP 依赖旧 Dandelion 工程。

当前暂不支持 MTL/纹理、多材质、阴影、PBR、骨骼动画和后处理。

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
src/io/ObjLoader.*       OBJ 解析、统一索引、法线与 AABB
src/render/Camera.*      轨道相机
src/render/Mesh.*        VAO/VBO/EBO RAII 与 Draw Call
src/render/RenderTarget.*离屏 Framebuffer
src/render/Shader.*      GLSL 编译、链接与 uniform
shaders/                 GPU 顶点和片元 Shader
assets/models/           模型资源
```
