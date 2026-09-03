# SR-P0 Scene Rendering Foundation

SR-P0 把原先偏单模型 Viewer 的结构收口为可供实时光栅、风格化渲染和
Reference Path Tracer 共用的场景基础。实现刻意保持轻量：它不是完整 ECS，
也不是通用 Render Graph。

## 场景与资产语义

`Scene` 持有稳定的 `SceneEntityId` 列表。每个 Entity 包含名称、父节点、
局部 TRS、资产归一化矩阵、可见性、阴影标记、材质 Tint 实例，以及当前/
上一帧世界矩阵。父子关系拒绝循环；删除父对象时子对象回到根层级。

场景每帧先用 `beginFrame()` 固化上一帧世界矩阵，再计算层级世界矩阵并生成
`RenderItem`。同一个 `GpuModel` 可以被多个 Entity 引用，10 物体固定场景因此
只上传一份 Cube 几何。Scene 面板支持选择、显隐、父级切换、复制和删除；复制
后的 Transform 与 Tint 独立。

Assimp 导入的静态 glTF 不再把 Node Transform 烘焙进 Vertex。`ModelNodeData`
保存层级，`GpuModel::DrawCommand` 保存节点变换，多个 Node 指向同一 Mesh 时复用
同一个 VAO/VBO/EBO。骨骼资产暂时保留兼容旧 Palette 的绑定变换策略，避免在
SR-P0 同时改写蒙皮空间约定。

## Pass Context 与状态隔离

`RenderPassContext` 为每个顶层 Pass 记录名称、输入、输出、Viewport、Clear Mask
与 `RenderState`。`OpenGlStateCache` 集中缓存 Depth、Blend、Cull、Front Face 和
Polygon Mode。每个 Pass 进入前应用明确基线状态，结束后恢复默认状态，从而避免
透明、焦散或 Debug Pass 把隐含 OpenGL 状态泄漏给后续阶段。

这仍是顺序执行器：资源生命周期和依赖不由系统自动推导。等 Path Tracer、NPR、
水体和体积效果出现真实跨队列/跨分辨率需求后，再根据证据决定是否升级 Render
Graph。

## Shader 热重载

Renderer 每 15 帧检查一次 Vertex/Fragment Shader 的最后修改时间。发生变化时
先构建候选 Program；只有所有 Stage 编译且 Link 成功，才与正在使用的 Program
交换。失败时旧 Program 继续渲染，Inspector 显示完整日志；文件再次变化后才重试。
成功重载会重置 Temporal History，避免新旧 Shader 输出在 TAA 中混合。

## Temporal 与动态几何

G-Buffer 的第四个 `RGBA16F` Attachment 保存对象运动向量和有效位。Vertex Shader
同时接收当前/上一帧 View-Projection、Model、Node 与 Joint Palette：

- 静态对象继续使用深度重建的相机 Motion Vector；
- 刚体对象使用当前/上一帧 Model Clip Position；
- 蒙皮对象额外使用上一帧 Joint Palette；
- 首帧、模型切换、尺寸/路径变化或 Shader 重载会使 History 失效。

TAA 优先读取有效的对象 Motion Vector，否则回退到原有相机重建路径。蒙皮对象的
包围球在绑定姿势半径基础上加入旋转弧与最大 Joint 位移的保守余量；它牺牲一些
剔除精度，换取动画不被错误剔除。

## 回归与复现

SR-P0 新增三张 1920×1080 固定基线：

- `docs/images/sr_p0_scene_entities.png`：至少 10 个 Entity 共享一份 Cube Mesh；
- `docs/images/sr_p0_object_motion.png`：刚体运动向量调试图；
- `docs/images/sr_p0_skin_motion.png`：骨骼运动向量调试图。

常用命令：

```powershell
cmake -S . -B build-release -DBUILD_TESTING=ON
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
cmake --build build-release --target foundation-visual-regression
cmake --build build-release --target renderer-regression-suite
cmake --build build-release --target renderer-benchmark-suite
cmake --build build-release --target package
```

完整回归套件会串起现有 Prism、Glass、Deferred、多灯、Instancing、TAA/SSAO、
Skinning 和 SR-P0 固定画面。Benchmark 套件保留各专项 JSON 中的硬件、分辨率、
质量档位、CPU/GPU P50/P95、Draw Call 与显存估算。

## 当前边界与下一阶段

- Scene 只保存运行时内存状态，尚无场景文件序列化、Undo/Redo 或 Prefab。
- Pass Context 已显式化状态和资源说明，但不负责资源别名、调度或屏障。
- Hot Reload 针对现有 Vertex/Fragment Program；尚无 Include 依赖追踪。
- 骨骼动态 Bounds 是保守球体，不是每帧精确 Skinned AABB。
- OpenGL 3.3 无 Compute/SSBO；GPU Driven、海洋 FFT 与体积效果需要后续 API 决策。

SR-P1A 已在此基础上加入只读 `SceneSnapshot`、Ray/AABB、Ray/Triangle、
`SurfaceInteraction` 与 Median-Split BVH，让 Rasterizer 与 CPU Reference Path Tracer
复用同一实例、材质、纹理、相机和灯光/环境数据。设计、测试和后续采样阶段见
[`reference-path-tracer.md`](reference-path-tracer.md)。
