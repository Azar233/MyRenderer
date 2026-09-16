# Architecture routing map

Read only the documents relevant to the requested change.

## Assets and scenes

- `src/asset/ModelData.h`: format-neutral CPU geometry, materials, textures, nodes, skins, and animation clips.
- `src/io/ModelImporter.h`, `ObjLoader.*`, `AssimpImporter.*`: import boundary and structured diagnostics.
- `src/scene/Scene.*`: entities, hierarchy, local/world transforms, visibility, shared GPU models, and current/previous transforms.
- `src/scene/SceneDocument.*`: `.myscene` persistence, relative paths, schema defaults, and load/save behavior.
- Read `README.md` sections “独立场景文件” and “支持范围与格式路线” for the current public contract.

## Application and editor

- `src/app/Application.*`: window, main loop, task ownership, UI integration, and subsystem orchestration.
- `src/app/ApplicationScene.cpp`: scene loading, preset setup, and editor scene operations.
- `src/app/EditorUi.h`: required section and property-control patterns.
- `src/app/FileDialog.*`: Windows file dialog boundary.
- Read `README.md` section “Editor UI 设计规范”. Use `docs/editor-scene-update.md` only for editor interaction/history context.

## Real-time renderer

- `src/render/Renderer.*`: pass order, renderer settings, shared lighting/material behavior, history invalidation, and metrics.
- `src/render/RenderPassSequence.h`, `OpenGlStateCache.*`: explicit pass contract and GL state isolation.
- `src/render/GBuffer.*`, `PostProcessor.*`, `SsaoRenderer.*`: deferred attachments, TAA/SSAO, outline, bloom, and final display conversion.
- `src/render/GpuModel.*`, `Mesh.*`, `Texture2D.*`: GPU upload, draw commands, node transforms, and texture color-space handling.
- `shaders/basic.*` and `shaders/deferred_lighting.frag`: Forward/Deferred opaque parity.
- Read `docs/scene-rendering-foundation.md` first for cross-cutting render changes. Then select `docs/deferred-shading.md`, `docs/taa-ssao.md`, `docs/stylized-rendering.md`, `docs/gpu-skinning.md`, or the relevant Glass/Prism document.

## CPU reference path tracer

- `src/pathtracer/SceneSnapshot.*` and `SceneSnapshotCapture.*`: immutable bridge from the live scene.
- `ProgressiveRenderer.*`, `TileScheduler.*`: deterministic accumulation, cancellation, worker lifetime, and progress publication.
- `PbrBsdf.*`, `MaterialBsdf.*`, `TextureSampling.*`, `LightSampling.*`, `EnvironmentSampling.*`: material/light transport semantics.
- `RayGeometry.*`, `Bvh.*`, `InstancedBvh.*`: geometry and acceleration structures.
- `ReferenceComparison.*`, `ImageOutput.cpp`, `tools/ReferenceRender.cpp`: deterministic output and comparison artifacts.
- Read the matching section of `docs/reference-path-tracer.md`; search by the SR-P1 stage or owning type instead of loading the whole document when unnecessary.

## Tests and fixtures

- `tests/AssetImportTests.cpp`: importer and material/animation fixture contracts.
- `tests/SceneTests.cpp`, `SceneDrawListTests.cpp`, `SceneDocumentTests.cpp`: hierarchy, submission, and persistence.
- `tests/PathTracingFoundationTests.cpp`, `ProgressiveRenderingTests.cpp`: geometry, transport, determinism, AOV, and output.
- `tests/ShaderHotReloadTests.cpp`, `PrismOpticsTests.cpp`: real-context shader replacement and pure optics.
- `assets/models/`: small deterministic import fixtures. `assets/scenes/`: versioned integration scenes. Reuse an appropriate fixture before creating a new one.
