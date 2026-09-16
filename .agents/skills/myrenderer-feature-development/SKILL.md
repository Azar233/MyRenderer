---
name: myrenderer-feature-development
description: Implement or modify MyRenderer rendering, editor, scene, asset-import, shader, or CPU path-tracing features while preserving the project's shared data model and rendering contracts. Use for feature work and architectural refactors in this repository; use the build or visual-regression skills for validation-only requests.
---

# MyRenderer feature development

Keep each feature integrated with the existing scene, renderer, editor, and validation paths instead of creating a parallel subsystem.

## Start from the affected contract

1. Inspect the working tree and preserve unrelated user changes.
2. Locate the owning types and read the corresponding project document before editing. Use [references/architecture-map.md](references/architecture-map.md) to route the task.
3. Trace the complete data path from persisted/input state to runtime state, render or compute execution, diagnostics, and tests. Change every affected layer in the same slice.
4. Prefer extending an existing abstraction over adding a second source of truth.

## Preserve these invariants

- Importers normalize OBJ, DAE, glTF, and GLB into `ModelData`; renderer and path-tracer code must not depend on Assimp or source-format types.
- `Scene`, `Camera`, materials, lights, and renderer settings are the shared source of truth. CPU path tracing consumes immutable `SceneSnapshot` data and must not add another asset or scene loader.
- Background work may build CPU data or staging images. OpenGL object creation, upload, and destruction remain on the context-owning thread. A cancelled or stale task must not publish over newer state.
- Top-level passes declare and restore their state through `RenderPassContext` and `OpenGlStateCache`; do not rely on state leaked by an earlier pass.
- Camera, transform, skinning, shader-output, render-path, and size changes must invalidate the temporal history they affect. Preserve current/previous-frame data together.
- Forward and Hybrid Deferred should implement the same opaque material and lighting semantics where supported. Transparent and refractive materials remain in the Forward Refractive pass.
- Lighting is computed in linear space. Color textures use sRGB decoding, data textures stay linear, and final display encoding happens once.
- `.myscene` changes need backward-compatible defaults, relative resource paths, deterministic round trips, and transactional loading: invalid input must not replace the current scene.
- Inspector controls use the `EditorUi` property helpers and semantic sections. Keep Viewport primary, support the 1100x680 application minimum and 260x120 panel minimum, and never use color as the only state signal.

## Complete a vertical slice

As applicable, update the setting/data structure, scene serialization, runtime application, Forward/Deferred or CPU counterpart, UI control, debug view, deterministic fixture, focused test, and nearby documentation. New renderer behavior should normally have an off/default state that preserves existing scenes and versioned baselines.

Use `$myrenderer-build-and-test` to choose proportionate checks. Use `$myrenderer-visual-regression` when pixels, pass ordering, temporal behavior, shaders, camera framing, or GPU performance may change.
