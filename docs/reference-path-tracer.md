# SR-P1 Reference Path Tracer

SR-P1 先实现 CPU 参考路径追踪器，用可重复的几何、采样和材质结果为实时光栅、
后续 Vulkan 光追与自然场景提供 Ground Truth。第一批交付 SR-P1A 只建立共享场景快照
与加速结构，不提前混入随机采样、BSDF 或线程调度。

## SR-P1A：共享只读场景快照

实时资产仍只经过现有 `ModelImporter`。`GpuModel` 在上传 OpenGL 资源的同时接管并保留
一份 `shared_ptr<const ModelData>`；光栅路径使用 GPU Mesh，参考路径追踪路径使用同一份
CPU Mesh、材质和纹理数据，不会重新读取 OBJ、DAE、glTF 或 GLB。

`captureSceneSnapshot()` 从某一帧的 `Scene` 与 `Camera` 生成值语义快照：

- 相机位置、View/Projection、垂直 FOV 与宽高比；
- 方向光、局部点光/聚光和环境参数；
- 可见 Entity 的世界变换、Tint、投影阴影标记与稳定 Entity ID；
- 去重后的只读 `ModelData` 资产；
- glTF Node → Mesh 引用展开后的 Mesh Instance。

同一 `ModelData` 被多个 Entity 或 Node 引用时，快照只持有一个资产共享引用；实例各自
保存 Object-to-World 与 Normal-to-World。`buildWorldTriangles()` 仅在构建当前 CPU BLAS
基线时展开世界空间三角形，同时保留 Asset / Instance / Mesh / Material / Primitive ID，
以便后续 Surface Interaction 查找材质和纹理。

## Ray、Surface Interaction 与 BVH

几何层不依赖 OpenGL：

- `Ray` 显式保存 `[tMin, tMax]`，用于自相交偏移和有限距离阴影查询；
- `Bounds3` 使用 slab test，并正确处理平行方向、无效 Bounds 和裁剪区间；
- Ray/Triangle 使用双面 Möller–Trumbore 求交，输出重心坐标、UV、几何/着色法线、
  Front Face 与稳定场景标识；
- 退化三角形、非有限顶点和不可逆实例变换不会进入有效求交结果；
- BVH 首版按最大质心轴做确定性的 Median Split，叶节点默认最多 4 个三角形；
- 遍历优先访问近节点，并用当前最近距离裁剪后续 AABB 与 Triangle 查询。

首版选择 Median Split 是为了先锁定正确性、确定性和单元测试接口。等 Cornell-style、
PBR 与 Volume Glass 场景产生可重复的 Build/Traversal Profile 后，再决定是否增加 SAH，
避免在没有数据时增加构建复杂度。

## 验证

纯 CPU 测试不创建 OpenGL 上下文：

```powershell
cmake --build build --config Release --target MyRendererPathTracingTests
ctest --test-dir build -C Release -R path-tracing-foundation --output-on-failure
```

测试覆盖：

- AABB 正向命中、平行 slab 未命中和 `tMax` 裁剪；
- Triangle 正反面命中、重心/UV 插值、法线朝向和退化面拒绝；
- Median BVH 节点统计、Primitive 重排后身份保持、最近命中和有限距离遮挡；
- 同一 Mesh 的多 Node / 多 Entity 实例、资产去重、材质编号与世界变换展开；
- SceneSnapshot → World Triangle → BVH → Surface Interaction 完整 CPU 数据链。

## 当前边界与下一步

- 当前是静态快照；Skinned Mesh 会标记为 `skinned`，但本阶段仅使用源/Bind Pose 顶点，
  不复制运行时关节姿势。
- Snapshot 已承载材质与纹理来源，但尚未采样基础色、法线或 metallic-roughness 纹理。
- 当前 BVH 是展开后的单层世界空间结构；等正确性稳定后再拆分共享 Mesh BLAS 与实例 TLAS。
- 尚无像素输出、渐进积累、随机采样、BSDF、NEE/MIS 或多线程。

下一批 SR-P1B 将实现确定性 RNG、Camera Ray、Progressive Accumulation、SPP/Max Depth、
可取消后台任务和最小 Diffuse/Emissive 路径，从“可求交”推进到“可输出第一张参考图”。
