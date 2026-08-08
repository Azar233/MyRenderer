# MyRenderer 技术术语字典

本文档记录 MyRenderer 已使用、正在实现和路线图中即将使用的图形学与工程术语。解释优先服务于理解本项目，不追求替代完整教材。

最后更新：2026-08-08

## 阅读与维护约定

状态含义：

- `已实现`：代码中已有可运行路径并完成过构建或运行验证。
- `基础已就绪`：底层资源或 Pass 边界已经存在，但最终视觉功能尚未完成。
- `部分实现`：已有简化版本，仍缺标准完整性或质量改进。
- `计划`：已进入 `todolist.md`，尚未实现。
- `概念`：用于帮助理解设计，不代表项目一定会实现。

后续维护规则：每次引入新的渲染技术、工具或架构概念时，同步增加或更新对应词条；只有通过相应验证后才把状态改为“已实现”。

## 1. 渲染器与程序结构

| 术语 | 通俗解释 | 在 MyRenderer 中的作用 | 状态 |
| --- | --- | --- | --- |
| Renderer（渲染器） | 接收场景、相机和材质数据，并组织 GPU 把它们画成图像的系统。 | `src/render/Renderer.*` 负责状态设置、Pass 顺序、Shader 参数和离屏绘制。 | 已实现 |
| Rendering Pipeline（渲染管线） | 数据从模型进入 GPU，经过顶点处理、光栅化、像素着色，最终变成屏幕图像的完整流程。 | 当前主流程为 Shadow、Opaque HDR、Forward Transparent/Refractive、Bloom/Tone Map。 | 已实现 |
| Render Pass（渲染阶段） | 为完成一种明确任务而进行的一组绘制，例如阴影、主场景或后处理。 | 通过 `RenderPassSequence` 顺序执行各阶段。 | 已实现 |
| Multi-pass Rendering（多 Pass 渲染） | 同一帧分多次渲染，每次产生下一阶段需要的结果。 | 阴影贴图、HDR 主场景、Bloom 和最终合成属于不同 Pass。 | 已实现 |
| Render Graph（渲染图） | 用“资源依赖关系”自动决定 Pass 顺序、资源生命周期和同步的系统。 | 当前只有轻量顺序编排，不是通用 Render Graph。 | 计划 |
| Forward Rendering（前向渲染） | 绘制物体时直接计算光照并输出最终颜色。 | 当前不透明 PBR 路径使用前向渲染；未来透明玻璃也会使用 Forward Refractive Pass。 | 已实现 |
| Deferred Rendering（延迟渲染） | 先把几何信息写入 G-Buffer，再统一计算光照，适合大量动态灯光。 | 图形程序路线中的候选功能；透明玻璃通常仍需单独走 Forward Pass。 | 计划 |
| Application Layer（应用层） | 管理窗口、输入、UI、主循环等外围工作，不应承载所有底层渲染细节。 | `src/app/Application.*` 目前仍较大，后续会继续拆分。 | 部分实现 |
| Asset Pipeline（资产管线） | 模型和纹理从文件进入 CPU 数据、校验、上传 GPU 并用于渲染的全过程。 | OBJ/DAE/glTF/GLB 最终转换成统一 `ModelData`，再创建 `GpuModel`。 | 已实现 |
| Format-independent Data（格式无关数据） | 渲染层只认识统一内部结构，不关心原文件是哪种格式。 | `ModelData` 隔离 tinyobjloader、Assimp 与渲染代码。 | 已实现 |
| RAII | 用 C++ 对象生命周期自动管理 GPU/系统资源，构造时获取，析构时释放。 | Shader、Texture、Mesh、Framebuffer 等资源通过类封装释放。 | 已实现 |
| CPU / GPU Boundary（CPU/GPU 边界） | 区分 CPU 负责的导入、组织工作与 GPU 负责的并行绘制工作。 | 模型后台导入在 CPU 完成，OpenGL 资源上传在拥有上下文的主线程完成。 | 已实现 |
| DCC（Digital Content Creation） | Blender、Maya、3ds Max、Substance 等美术制作软件的统称。 | TA 路线计划增加 Blender 导出与校验工具。 | 计划 |
| Technical Artist（TA，技术美术） | 连接美术与程序，负责材质、工具、资产规范、性能预算和生产流程。 | 本项目可通过资产审阅、材质调试和玻璃效果工具体现 TA 能力。 | 概念 |

## 2. GPU 光栅化基础

| 术语 | 通俗解释 | 在 MyRenderer 中的作用 | 状态 |
| --- | --- | --- | --- |
| Rasterization（光栅化） | 把三角形覆盖的区域转换成一个个待着色的像素/片元。 | OpenGL GPU 主渲染方式；项目不是 CPU 软光栅器或路径追踪器。 | 已实现 |
| Vertex Shader（顶点着色器） | GPU 对每个顶点执行的程序，主要完成坐标变换并输出插值数据。 | `basic.vert` 计算世界位置、法线、切线、MVP 和阴影坐标。 | 已实现 |
| Fragment Shader（片元着色器） | GPU 对每个可见片元执行的程序，计算材质、光照和最终颜色。 | `basic.frag` 实现 Blinn-Phong/PBR、IBL、法线贴图和阴影。 | 已实现 |
| GLSL | OpenGL 使用的 Shader 编程语言。 | `shaders/` 中的 `.vert`、`.frag` 文件使用 GLSL 330 Core。 | 已实现 |
| Uniform | CPU 为一次或一批 Draw Call 设置、在 Shader 中只读的参数。 | 用于传递矩阵、灯光、相机、材质开关与强度。 | 已实现 |
| Attribute（顶点属性） | 每个顶点携带的数据，例如位置、法线、UV、切线。 | 当前顶点布局包含 Position、Normal、UV0、Tangent。 | 已实现 |
| Interpolation（插值） | GPU 在三角形内部自动根据三个顶点平滑计算片元数据。 | 世界位置、法线、UV 等从 Vertex Shader 插值到 Fragment Shader。 | 已实现 |
| VAO | 记录顶点属性如何从 Buffer 中读取的 OpenGL 对象。 | `Mesh` 用 VAO 保存顶点布局和相关 Buffer 绑定。 | 已实现 |
| VBO | 存放顶点数据的 GPU Buffer。 | 保存 Position、Normal、UV、Tangent 等顶点数据。 | 已实现 |
| EBO / Index Buffer | 存放顶点索引，用复用顶点的方式组成三角形。 | `Mesh` 通过 `glDrawElements` 使用索引绘制。 | 已实现 |
| Draw Call | CPU 向 GPU 提交一次绘制命令。数量太多会增加 CPU/驱动开销。 | 当前按 Mesh 的 Submesh/Material 范围提交并统计。 | 已实现 |
| MVP（Model/View/Projection） | Model 放置物体，View 表示相机，Projection 把 3D 投影到屏幕。 | `basic.vert` 用三矩阵计算最终裁剪空间位置。 | 已实现 |
| Coordinate Space（坐标空间） | 同一点可以用模型、世界、观察、裁剪、屏幕等不同坐标描述。 | 光照在世界空间计算；阴影还会转换到 Light Space。 | 已实现 |
| Viewport（视口） | 最终渲染结果在目标纹理或窗口中的像素区域。 | 随 ImGui 渲染面板大小调整离屏 RenderTarget。 | 已实现 |
| Depth Test（深度测试） | 比较片元与已保存深度，只保留更靠近相机的表面。 | 解决模型前后遮挡，也会供未来折射深度追踪使用。 | 已实现 |
| Depth Write（深度写入） | 决定通过测试的片元是否更新深度。透明物体通常测试深度但不写入。 | 不透明/MASK 写深度；BLEND 保持深度测试但关闭深度写入。 | 已实现 |
| Stencil Buffer（模板缓冲） | 每个像素保存一个小整数，可用作局部遮罩。 | 当前与 Depth 一起使用 `DEPTH24_STENCIL8`，尚未用于特效遮罩。 | 基础已就绪 |
| Back-face Culling（背面剔除） | 不绘制朝向背面的三角形，以减少无用工作。 | Inspector 中可开关；阴影 Pass 会临时剔除正面以减轻阴影痤疮。 | 已实现 |
| Wireframe（线框模式） | 只显示三角形边线，用于查看拓扑与细分密度。 | Inspector 可切换。 | 已实现 |

## 3. Framebuffer、RenderTarget 与颜色缓冲

| 术语 | 通俗解释 | 在 MyRenderer 中的作用 | 状态 |
| --- | --- | --- | --- |
| Framebuffer / FBO | GPU 渲染的目标集合，可以包含颜色、深度、模板等附件。 | 主场景、阴影、Bloom、最终 LDR 都使用离屏 FBO。 | 已实现 |
| RenderTarget（渲染目标） | 对 Framebuffer 和附件纹理的工程封装。 | `RenderTarget` 管理 Opaque HDR、最终 HDR、Depth/Stencil 和最终 LDR。 | 已实现 |
| Attachment（附件） | 挂在 FBO 上、用于接收颜色或深度结果的纹理/Renderbuffer。 | 当前包含颜色纹理、Depth/Stencil 纹理和 MSAA Renderbuffer。 | 已实现 |
| Off-screen Rendering（离屏渲染） | 不直接画到窗口，而是先画到纹理，便于 UI 展示与后处理。 | ImGui Viewport 显示最终 LDR Texture。 | 已实现 |
| HDR（High Dynamic Range） | 使用浮点颜色保存大于 1 的高亮，不会过早把亮度截断。 | 主场景使用 `RGBA16F`，为 Bloom、曝光和高亮焦散保留能量。 | 已实现 |
| LDR（Low Dynamic Range） | 常规 0～1 范围的显示颜色。 | Tone Mapping 后写入 `RGBA8` 最终纹理。 | 已实现 |
| MSAA | 在同一像素内采多个几何覆盖样本，主要改善三角形边缘锯齿。 | 支持 1x/4x；不等同于 TAA，也不能单独解决 Shader 闪烁。 | 已实现 |
| Resolve | 把多采样颜色/深度合并成普通单采样纹理，供后续 Pass 读取。 | 4x 模式现已 Resolve Color、Depth 和 Stencil。 | 已实现 |
| Sampleable Depth（可采样深度） | 深度不只用于遮挡，还能像纹理一样被后续 Shader 读取。 | Glass-0 已建立 `GL_DEPTH24_STENCIL8` 深度纹理。 | 已实现 |
| Opaque Color Texture | 只包含不透明场景的 HDR 颜色，是玻璃折射看到的背景。 | Glass-0 已与最终 HDR 输出分离。 | 已实现 |
| Scene Color Texture | 包含不透明场景以及后续玻璃等效果的最终 HDR 场景颜色。 | 后处理从它提取 Bloom 并执行 Tone Mapping。 | 已实现 |
| Framebuffer Feedback | 一个 Pass 同时读取和写入同一纹理，会产生未定义或不可预测结果。 | Glass-0 使用独立 Opaque 输入与 HDR Scene 输出避免该问题。 | 已解决 |
| Renderbuffer | 只能作为渲染附件、通常不能直接在 Shader 里采样的存储对象。 | MSAA Color 和 MSAA Depth/Stencil 当前使用 Renderbuffer。 | 已实现 |

## 4. 模型、网格与资产导入

| 术语 | 通俗解释 | 在 MyRenderer 中的作用 | 状态 |
| --- | --- | --- | --- |
| Mesh（网格） | 由顶点和三角形组成的几何对象。 | `MeshData` 保存 CPU 数据，`Mesh` 保存 VAO/VBO/EBO。 | 已实现 |
| Submesh（子网格） | 同一 Mesh 中使用不同材质或索引区间的一部分。 | 每个 Submesh 对应材质编号和一个 Draw Call。 | 已实现 |
| Vertex（顶点） | 构成三角形的点及其附加属性。 | 当前包含位置、法线、UV0 和带手性的切线。 | 已实现 |
| Index（索引） | 指向顶点数组的编号，用于复用顶点组成三角形。 | 由 OBJ/Assimp 导入并上传 EBO。 | 已实现 |
| Triangulation（三角化） | 把四边形或多边形拆成 GPU 易处理的三角形。 | tinyobjloader 与 Assimp 导入时执行。 | 已实现 |
| AABB | 与坐标轴平行的包围盒，用最小/最大 XYZ 包住模型。 | 用于模型归一化、自动取景、统计和未来裁剪。 | 已实现 |
| Normal（法线） | 表示表面朝向的向量，决定光照强弱与反射方向。 | 缺失法线时导入器会生成平滑法线。 | 已实现 |
| UV / Texture Coordinate | 把模型表面位置映射到二维纹理坐标。 | 当前支持 UV0。 | 已实现 |
| Tangent Space（切线空间） | 以表面切线、双切线、法线构成的局部坐标系。 | 用于把法线贴图中的方向转换到世界空间。 | 已实现 |
| Tangent Handedness（切线手性） | 用 `+1/-1` 表示双切线方向，处理镜像 UV。 | 存储在 `Vertex.tangent.w`。 | 已实现 |
| Degenerate UV（退化 UV） | 三角形的 UV 面积接近零，无法建立可靠切线空间。 | 导入时记录诊断并回退到几何法线。 | 已实现 |
| OBJ | 传统文本模型格式，适合简单几何与回归测试。 | 使用 tinyobjloader 独立导入。 | 已实现 |
| DAE / COLLADA | XML 场景交换格式，可包含节点、Mesh、材质。 | 通过 Assimp 导入，主要兼容旧 Dandelion 资产。 | 已实现 |
| glTF 2.0 | 面向实时运行时传输的标准 3D 格式。 | 当前主要材质与 PBR 交换格式。 | 部分实现 |
| GLB | 把 glTF JSON、Buffer 和图像打包在一个二进制文件中。 | 通过 Assimp 支持内嵌纹理。 | 已实现 |
| Assimp | 统一读取多种模型格式的第三方资产导入库。 | 只开启 COLLADA 与 glTF Importer，第三方类型不进入渲染层。 | 已实现 |
| Data URI | 把二进制图片用编码文本直接嵌入 glTF JSON。 | `textured_triangle.gltf` 用作固定回归资产。 | 已实现 |
| Embedded Texture（内嵌纹理） | 图像数据存放在模型文件内部，而不是外部图片路径。 | 支持 glTF Data URI 与 GLB/Assimp 内嵌图像。 | 已实现 |
| Texture Cache（纹理缓存） | 相同纹理只解码和上传一次，多个材质共享 GPU 对象。 | `TextureCache` 按 Cache Key 复用纹理。 | 已实现 |
| Fallback Texture（回退纹理） | 纹理缺失或损坏时显示的替代纹理，避免崩溃或黑屏。 | 无纹理使用白色；加载失败使用洋红棋盘。 | 已实现 |

## 5. 纹理与颜色空间

| 术语 | 通俗解释 | 在 MyRenderer 中的作用 | 状态 |
| --- | --- | --- | --- |
| Texture Sampling（纹理采样） | Shader 根据 UV 从纹理读取颜色或数据。 | 基础色、法线、金属度/粗糙度、阴影等都通过采样获得。 | 已实现 |
| Sampler | 定义纹理过滤与超出 UV 范围时如何重复/截断的规则。 | 当前 OpenGL Texture 有固定参数；完整 glTF Sampler 语义待补。 | 部分实现 |
| Mip / Mipmap | 同一纹理的多级缩小版本，远处或模糊采样时减少闪烁并提高缓存效率。 | 环境 Cubemap 使用 Mip 表示不同粗糙度；普通材质 Mip 管线仍可加强。 | 部分实现 |
| sRGB | 面向显示和图片存储的非线性颜色编码。 | 基础色纹理以 sRGB 格式上传并由 GPU 解码。 | 已实现 |
| Linear Color Space（线性颜色空间） | 数值与真实光能近似成正比，适合光照和混合计算。 | 材质与灯光在线性空间计算。 | 已实现 |
| Gamma Correction（Gamma 校正） | 在线性计算与显示编码之间进行转换。 | 最终输出只执行一次 Linear-to-sRGB，避免重复 Gamma。 | 已实现 |
| Data Texture（数据纹理） | 存储法线、粗糙度等数值，而不是可直接观看的颜色。 | 法线与 Metallic-Roughness 纹理保持线性采样。 | 已实现 |
| Normal Map（法线贴图） | 用纹理改变表面微小朝向，不增加真实几何。 | 使用 TBN 切线空间矩阵影响光照。 | 已实现 |
| Base Color / Albedo | 材质在不包含光照时的基础颜色。 | 来自 Factor × Texture × Inspector Tint。 | 已实现 |

## 6. 光照、BRDF 与 PBR

| 术语 | 通俗解释 | 在 MyRenderer 中的作用 | 状态 |
| --- | --- | --- | --- |
| Blinn-Phong | 经典经验光照模型，用漫反射和高光近似表面明暗。 | 可作为非 PBR 对照路径。 | 已实现 |
| PBR（Physically Based Rendering） | 使用接近物理规律、参数可跨环境复用的材质与光照方法。 | 当前采用 glTF Metallic-Roughness 工作流。 | 已实现 |
| BRDF | 描述光从某方向射入后，会以多少能量反射到观察方向的函数。 | Cook-Torrance 用多个项组合出 PBR 高光与漫反射。 | 已实现 |
| Cook-Torrance | 常用微表面 PBR BRDF 框架。 | `basic.frag` 组合 GGX 分布、几何遮蔽和 Fresnel。 | 已实现 |
| GGX | 描述微表面法线分布的模型，能产生较自然的长尾高光。 | 当前用于 PBR Specular Distribution。 | 已实现 |
| Metallic（金属度） | 0 通常表示绝缘体，1 表示金属；决定高光颜色和漫反射比例。 | glTF 打包纹理 B 通道与 Factor 相乘。 | 已实现 |
| Roughness（粗糙度） | 表面越粗糙，高光越宽越模糊。 | glTF 打包纹理 G 通道与 Factor 相乘。 | 已实现 |
| Fresnel Effect（菲涅耳效应） | 观察角度越贴近表面，反射通常越强。 | PBR 当前使用 Schlick 近似；玻璃反射/透射也依赖它。 | 已实现 |
| Energy Conservation（能量守恒） | 反射、透射、吸收的光能总和不应凭空超过入射能量。 | 当前不透明 PBR近似遵循；Glass 材质需继续保证。 | 部分实现 |
| IBL（Image-based Lighting） | 用环境图像提供来自四面八方的光照和反射。 | 当前使用程序化 Cubemap 与近似预滤波。 | 部分实现 |
| Cubemap | 由六个方向组成的立方体纹理，适合表示远处环境。 | 用于天空盒与 PBR 环境采样。 | 已实现 |
| Skybox（天空盒） | 把环境 Cubemap 作为无限远背景显示。 | 在主场景模型之前绘制。 | 已实现 |
| Split-sum IBL | 把环境镜面反射积分拆成预过滤 Cubemap 与 BRDF LUT，运行时快速组合。 | P0 计划从当前近似 IBL 升级。 | 计划 |
| Irradiance Map | 对环境光做低频卷积，表示漫反射接收到的平均光照。 | 标准 IBL 路线的一部分。 | 计划 |
| Prefiltered Environment Map | 按不同粗糙度预先模糊环境反射。 | 当前有近似 Mip；标准生成流程待实现。 | 部分实现 |
| BRDF LUT | 二维查找表，预存环境镜面 BRDF 中与视角和粗糙度有关的积分结果。 | 标准 Split-sum IBL 路线需要。 | 计划 |

## 7. 阴影与光空间

| 术语 | 通俗解释 | 在 MyRenderer 中的作用 | 状态 |
| --- | --- | --- | --- |
| Shadow Map（阴影贴图） | 从光源视角记录最近深度，再判断相机看到的点是否被遮挡。 | 当前为方向光生成 2048² 深度纹理。 | 已实现 |
| Light Space（光源空间） | 以光源作为“相机”的坐标空间。 | 顶点转换到 Light View-Projection 后查询 Shadow Map。 | 已实现 |
| PCF | 对阴影贴图周围多个深度样本求平均，使边缘不那么硬。 | 当前使用 3×3 PCF。 | 已实现 |
| Shadow Acne（阴影痤疮） | 精度误差造成表面出现条纹或自阴影噪点。 | 当前通过 Bias 和阴影 Pass 剔除策略减轻。 | 部分实现 |
| Peter Panning | 阴影 Bias 过大导致阴影看起来与物体分离。 | 后续阴影调试视图需要同时平衡它与 Acne。 | 计划 |
| CSM（Cascaded Shadow Maps） | 按相机距离分多级阴影范围，让近处保持更高精度。 | P0 阴影质量路线。 | 计划 |
| Transmissive Shadow（透射阴影） | 光穿过透明物体后形成带颜色和衰减的阴影。 | Glass-3 计划，普通 Shadow Map 当前只处理不透明遮挡。 | 计划 |

## 8. 后处理与显示

| 术语 | 通俗解释 | 在 MyRenderer 中的作用 | 状态 |
| --- | --- | --- | --- |
| Post-processing（后处理） | 场景渲染完成后，对整张图像执行的效果。 | Bloom、Tone Mapping 和 sRGB 输出由 `PostProcessor` 完成。 | 已实现 |
| Tone Mapping（色调映射） | 把 HDR 亮度压缩到普通显示器可显示范围。 | 支持 ACES 近似和关闭对照。 | 已实现 |
| ACES | 常用电影风格色调映射曲线，能较自然地压缩高亮。 | `postprocess.frag` 中实现近似公式。 | 已实现 |
| Exposure（曝光） | 在 Tone Mapping 前整体放大或缩小 HDR 亮度。 | Inspector 可调。 | 已实现 |
| Bloom | 提取高亮并模糊叠加，让强光产生泛光。 | 当前阈值提取后进行多次水平/垂直高斯模糊。 | 已实现 |
| Threshold（阈值） | 只有亮度超过指定值的像素才进入 Bloom。 | `bloomThreshold` 控制提取范围。 | 已实现 |
| Gaussian Blur（高斯模糊） | 按距离加权平均邻近像素，产生平滑模糊。 | Bloom 使用分离式水平/垂直模糊。 | 已实现 |
| Chromatic Aberration（镜头色差） | 镜头造成画面边缘 RGB 通道错位的全屏效果。 | 尚未实现；不能代替玻璃材质内部的光谱色散。 | 概念 |

## 9. 玻璃、折射与焦散

| 术语 | 通俗解释 | 在 MyRenderer 中的作用 | 状态 |
| --- | --- | --- | --- |
| Opaque（不透明） | 光不能穿过，通常输出颜色并写入深度。 | 当前所有正式材质都按不透明处理。 | 已实现 |
| Alpha Blending（透明混合） | 根据 Alpha 把前景颜色与已画背景做比例混合。 | 已支持 glTF `BLEND` 的标准 Over 合成；它本身不会产生折射。 | 已实现 |
| Transparent Sorting（透明排序） | 通常按从远到近绘制透明表面，减少混合顺序错误。 | 当前对单个模型内的 BLEND 子网格按中心距离排序；多对象全局排序将在场景层补齐。 | 部分实现 |
| Transmission（透射） | 光穿过材质继续传播，是玻璃区别于普通半透明贴纸的核心。 | Glass-1 计划加入 Dielectric Transmission。 | 计划 |
| Refraction（折射） | 光进入不同介质时改变传播方向，使玻璃后的背景发生扭曲。 | 已具备独立 Opaque Color/Depth 输入和 Refractive Pass 边界，Shader 尚未实现。 | 基础已就绪 |
| Refractive Pass（折射 Pass） | 在不透明场景完成后，专门绘制透明、玻璃、水晶等材质的阶段。 | 当前已绘制 Alpha Blend 子网格并建立独立 HDR 输出；真实折射 Shader 尚未实现。 | 基础已就绪 |
| Screen-space Refraction（屏幕空间折射） | 利用当前画面的颜色与深度近似追踪折射，只能看到屏幕中已经存在的内容。 | Glass-1 计划；离屏或被遮挡信息需要环境图回退。 | 计划 |
| IOR（折射率） | 表示光在介质中传播速度差异的参数，决定折射弯曲和基础反射比例。 | Glass-1 将作为水晶材质核心参数。 | 计划 |
| Snell's Law（斯涅尔定律） | 根据两种介质折射率和入射角计算折射方向。 | Glass-1 的折射方向计算基础。 | 计划 |
| Total Internal Reflection（全反射） | 从高折射率介质向外传播且角度过斜时，光不再透出而完全反射。 | 对水晶内部边缘高亮很重要。 | 计划 |
| Thickness（厚度） | 光在物体内部实际走过的距离，影响位移和吸收强度。 | Glass-2 计划支持参数、贴图和前后深度估算。 | 计划 |
| Beer-Lambert Law | 光在介质中传播越远，被吸收越多；不同颜色可以有不同吸收。 | 用于让玻璃呈现真实体积颜色。 | 计划 |
| Attenuation（衰减/吸收） | 光穿过介质后亮度和颜色逐渐减少。 | Glass Material 将提供 Color 和 Distance 参数。 | 计划 |
| Spectral Dispersion（光谱色散） | 不同波长的折射率不同，白光经过水晶后分离成彩虹。 | Glass-2 计划先用 R/G/B 三通道近似。 | 计划 |
| Abbe Number（阿贝数） | 描述材料色散强弱的参数；数值越低通常色散越明显。 | 可作为比简单“色散强度”更接近物理的材质控制。 | 计划 |
| Thin-film Iridescence（薄膜虹彩） | 薄层内部多次反射产生干涉，随角度呈现彩色表面。 | 可作为参考效果的可选表面增强，不等同于体积色散。 | 计划 |
| Caustics（焦散） | 光经过反射或折射后聚集，在接收表面形成明亮花纹。 | 参考图地面的彩虹亮斑；Glass-3 目标。 | 计划 |
| Caustics Projector / Decal | 把预制或程序化焦散图案投射到地面，以较低成本控制视觉效果。 | Glass-3 的第一版 TA 友好方案。 | 计划 |
| Light-space Caustics | 从光源方向计算折射光落点并累积能量的实时焦散近似。 | Glass-3 图形程序进阶方案。 | 计划 |
| Additive Blending（加法混合） | 把新颜色直接加到已有颜色上，适合表示光和能量叠加。 | 计划用于 HDR 焦散结果，使高亮能进入 Bloom。 | 计划 |

## 10. 调试、性能与测试

| 术语 | 通俗解释 | 在 MyRenderer 中的作用 | 状态 |
| --- | --- | --- | --- |
| KHR_debug | OpenGL 驱动提供的调试消息与对象标记扩展。 | Debug 构建可接收 API 错误和性能提示。 | 已实现 |
| GPU Timer Query | 让 GPU 测量一段命令实际执行时间，避免只看 CPU 提交时间。 | 使用四槽 `GL_TIME_ELAPSED` Query 环降低阻塞。 | 已实现 |
| CPU-bound | 帧率主要受 CPU、驱动提交或逻辑限制，GPU 还有空闲。 | Benchmark 与 Nsight 分析时需要先判断。 | 概念 |
| GPU-bound | 帧率主要受 Shader、像素、带宽或 GPU 工作量限制。 | 高分辨率、玻璃色散和焦散可能增加 GPU 压力。 | 概念 |
| GPU Capture | 把一帧的 Draw Call、资源和管线状态保存下来逐步检查。 | 计划使用 RenderDoc/Nsight 留下可复现证据。 | 计划 |
| RenderDoc | 跨 API 帧调试工具，可查看事件、纹理、Buffer、Shader 输入输出。 | 作品集性能/正确性报告计划使用。 | 计划 |
| Nsight Graphics | NVIDIA 的 GPU 调试、Trace 和 Shader Profiling 工具。 | 计划用于定位 GPU 瓶颈并记录优化前后数据。 | 计划 |
| Benchmark（基准测试） | 在固定场景、参数和硬件下重复测量，便于比较改动。 | 计划加入预热、固定帧数、P50/P95 输出。 | 计划 |
| P50 / P95 | 50%/95% 样本不超过的耗时；P95 更能反映偶发卡顿。 | 计划用于 CPU/GPU 帧时间报告。 | 计划 |
| Smoke Test（冒烟测试） | 用最短流程确认程序能启动、加载和渲染，不保证所有细节正确。 | `gpu-smoke` 使用真实隐藏 OpenGL 窗口渲染 5 帧。 | 已实现 |
| Regression Test（回归测试） | 验证新改动没有破坏此前正常功能。 | 资产导入 CTest 已实现；视觉回归仍在计划中。 | 部分实现 |
| Visual Regression（视觉回归） | 固定场景输出图片并与基准图比较，发现渲染结果变化。 | 计划使用误差阈值和差异热图。 | 计划 |
| SSIM | 比逐像素相等更关注结构相似度的图像比较指标。 | 计划用于容忍不同 GPU 的微小浮点差异。 | 计划 |
| Debug View（调试视图） | 把法线、深度、粗糙度等中间数据直接显示出来。 | 当前有网格/轴线；完整材质与 Glass 调试视图待实现。 | 部分实现 |
| Overdraw | 同一像素在一帧内被重复绘制多次，透明与粒子常导致高 Overdraw。 | TA/性能调试视图计划覆盖。 | 计划 |
| GPU Memory / VRAM | GPU 用于纹理、Buffer 和 RenderTarget 的显存。 | 当前统计纹理估算值；完整 RenderTarget/Buffer 预算待补。 | 部分实现 |

## 11. 路线图中的现代渲染术语

| 术语 | 通俗解释 | 在 MyRenderer 中的作用 | 状态 |
| --- | --- | --- | --- |
| G-Buffer | Deferred Rendering 用的一组纹理，存储 Albedo、Normal、Material、Depth 等。 | GP-P1 计划。 | 计划 |
| SSAO | 根据屏幕深度和法线近似物体缝隙中的环境遮蔽。 | GP-P1 候选效果。 | 计划 |
| SSR | 在屏幕深度/颜色中追踪反射，只能反射屏幕已有信息。 | GP-P1 候选效果。 | 计划 |
| TAA | 融合当前帧与历史帧降低锯齿和闪烁，需要运动信息与稳定策略。 | 对焦散稳定也有价值。 | 计划 |
| Motion Vector（运动向量） | 记录像素从上一帧到当前帧移动了多少。 | TAA History Reprojection 的基础。 | 计划 |
| Temporal Reprojection（时序重投影） | 根据运动向量把上一帧结果映射到当前帧。 | TAA 与焦散时序稳定会使用。 | 计划 |
| Jitter | 每帧轻微移动投影采样位置，用多帧积累获得更密集采样。 | TAA 计划使用 Halton 序列。 | 计划 |
| Frustum Culling（视锥裁剪） | CPU/GPU 不提交相机视野外的物体。 | GP-P1 大场景优化路线。 | 计划 |
| LOD | 根据距离或屏幕尺寸选择不同精度模型。 | GP-P1 与 TA 资产预算路线。 | 计划 |
| Instancing（实例化） | 一次 Draw Call 绘制同一 Mesh 的多个不同 Transform 实例。 | GP-P1 压力场景计划。 | 计划 |
| GPU-driven Rendering | 由 GPU 完成可见性、批次和间接绘制命令生成，减少 CPU 提交。 | GP-P2 备选旗舰。 | 计划 |
| Compute Shader | 不直接负责画三角形、用于通用 GPU 并行计算的 Shader。 | OpenGL 3.3 不支持；GPU 粒子/Driven 路线需升级 API。 | 计划 |
| SSBO | Shader 可读写的大容量结构化 Buffer。 | OpenGL 4.3+ GPU-driven/Compute 路线需要。 | 计划 |
| Indirect Draw | Draw Call 的参数存放在 GPU Buffer 中，可由 GPU 生成。 | GPU-driven 路线需要 Multi-Draw Indirect。 | 计划 |
| Vulkan | 显式管理资源、同步和命令提交的现代低层图形 API。 | GP-P2 推荐旗舰后端。 | 计划 |
| Dynamic Rendering | Vulkan 中不预先创建固定 RenderPass/Framebuffer 组合，运行时声明附件。 | 计划作为 Vulkan 后端基线。 | 计划 |
| Timeline Semaphore | 用递增数值表达 Vulkan GPU 工作依赖的同步对象。 | 计划用于 Vulkan Frame-in-flight 与资源上传同步。 | 计划 |

## 12. 容易混淆的概念

### Alpha Blending 与 Transmission

- Alpha Blending：把已经算好的前景颜色与背景颜色按比例混合，像把两张图片叠在一起。
- Transmission：光真正穿过材质，可能发生折射、吸收和内部反射。
- 结论：实现 glTF `alphaMode=BLEND` 并不等于实现玻璃。

### Chromatic Aberration 与 Spectral Dispersion

- Chromatic Aberration：镜头或全屏后处理让 RGB 通道在画面边缘错位。
- Spectral Dispersion：水晶内部不同波长按照不同 IOR 折射。
- 结论：参考图中的水晶彩边应由材质色散产生，而不是只做全屏 RGB 偏移。

### Bloom 与 Caustics

- Bloom：把已经存在的高亮区域扩散成光晕。
- Caustics：反射/折射光线聚集后，真的在地面形成高能量图案。
- 结论：Bloom 可以让焦散更耀眼，但不能凭空生成焦散形状。

### MSAA 与 TAA

- MSAA：同一帧对几何边缘做多重覆盖采样。
- TAA：融合多帧信息，能处理更多 Shader 锯齿和闪烁，但会面临拖影。
- 结论：当前 4x MSAA 能改善边缘，未来移动焦散仍可能需要 TAA 或专用时序稳定。

### HDR Texture 与 HDR Display

- HDR Texture：渲染过程中使用浮点纹理保存高亮能量。
- HDR Display：显示器和操作系统以 HDR 标准真正输出更大亮度范围。
- 结论：本项目目前实现的是 HDR 渲染中间结果，最终仍 Tone Map 到普通 LDR/sRGB 输出。

### Material 与 Shader

- Shader：定义“怎样计算”的 GPU 程序。
- Material：提供“用哪些参数和纹理计算”的数据实例。
- 结论：多个玻璃 Preset 可以共享一个 Glass Shader，但使用不同 IOR、Thickness、Dispersion 等材质参数。

## 13. 主要代码位置速查

| 想查什么 | 主要文件 |
| --- | --- |
| 帧流程与 Pass 顺序 | `src/render/Renderer.cpp`、`src/render/RenderPassSequence.h` |
| HDR/MSAA/Depth/Framebuffer | `src/render/RenderTarget.*` |
| Bloom、Tone Mapping、最终输出 | `src/render/PostProcessor.*`、`shaders/postprocess.frag` |
| PBR、IBL、法线贴图、阴影采样 | `shaders/basic.frag` |
| 顶点变换与切线空间输入 | `shaders/basic.vert` |
| 环境 Cubemap 与天空盒 | `src/render/EnvironmentMap.*`、`shaders/skybox.frag` |
| Shadow Map | `src/render/ShadowMap.*`、`shaders/shadow_depth.*` |
| GPU Mesh 与材质绑定 | `src/render/Mesh.*`、`src/render/GpuModel.*` |
| 纹理解码、缓存和回退 | `src/render/Texture2D.*` |
| 格式无关资产结构 | `src/asset/ModelData.h` |
| OBJ 导入 | `src/io/ObjLoader.*` |
| DAE/glTF/GLB 导入 | `src/io/AssimpImporter.*` |
| UI、加载与主循环 | `src/app/Application.*` |
| CPU 资产回归测试 | `tests/AssetImportTests.cpp` |
| 后续阶段与验收标准 | `todolist.md` |
