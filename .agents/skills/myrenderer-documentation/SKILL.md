---
name: myrenderer-documentation
description: Write or revise MyRenderer stage documentation in docs/ following the project language, structure, and screenshot rules (Chinese prose, English technical terms, captioned reproducible figures). Use when adding a stage document, updating an existing one after a milestone, refreshing docs/images/README.md or docs/media/README.md, or auditing documentation consistency.
---

# MyRenderer documentation

`docs/README.md` is the normative standard; this skill is how to apply it. Read `docs/README.md` before rewriting a document, and read `docs/reference-path-tracer.md` for the fullest existing example of the target style.

## Language

- Prose is Chinese. Write complete Chinese sentences for narrative, conclusions, tradeoffs, limits, and next steps. Do not emit an English bullet outline.
- Keep technical terminology, APIs, type names, file names, parameters, commands, and error codes in English. On first use, pair the term with a short Chinese explanation: `Kasten-Young 相对大气质量（relative air mass）`.
- Never mix a half-Chinese, half-English clause. Write `该 pass 只写 CPU staging image`, not `the pass 写 staging image 到内存`.
- Section headings are Chinese with the English term kept, for example `## Aerial Perspective：相机到场景的积分` or `## 验证`.

## Structure

Follow the order in `docs/README.md`: 一级标题 → 元信息块（日期、revision、构建目录、GPU/驱动/OpenGL）→ 目标与范围 → 实现（按数据流：持久化 → 运行时 → 渲染 → UI → 诊断）→ 截图 → 验证 → 限制与取舍 → 复现命令 → 下一步.

One document per stage. Do not append an endless chronological log; the cross-stage overview belongs in `todolist.md`. Write only what has happened — unimplemented capability goes under 限制 or 下一步, never as an accomplished fact. Every claim needs a number with a unit, a comparison baseline, and a test name.

## Screenshots

**A stage document without a figure is incomplete.** Every feature document carries at least one image whose caption states what the image proves.

- **Baselines** (`docs/images/`, `docs/reference-images/`, `docs/performance/`) are compared pixel-wise by regression targets. Only touch them during explicitly authorized baseline maintenance, and then update `docs/images/README.md` and `docs/regression-baseline-audit.md` in the same change. Never overwrite historical stage evidence.
- **Illustrations** (`docs/media/`) exist to explain a feature, a UI state, or a before/after. Capture them freely; they are not compared automatically. When unsure where an image belongs, put it in `docs/media/` — putting an explanatory screenshot into `docs/images/` pollutes the regression inventory.
- Caption in Chinese, state the conclusion, not the filename. Prefer one composed before/after, On/Off, or Low/High figure over two loose images at matching camera, resolution, and exposure. For UI captures, name the tab, the section, and the window size (1440×900 default workspace or 1100×680 minimum window). Every figure must be reproducible from a target, environment variable set, or command recorded next to it.

Useful capture and composition commands:

```powershell
# Editor / workspace capture at a chosen window size
$env:MYRENDERER_SMOKE_TEST='1'
$env:MYRENDERER_EDITOR_WINDOW_WIDTH='1440'; $env:MYRENDERER_EDITOR_WINDOW_HEIGHT='900'
$env:MYRENDERER_EDITOR_SCREENSHOT_TAB='modules'   # assets|timeline|modules|render-queue|log|object|renderer|module
$env:MYRENDERER_EDITOR_SCREENSHOT='docs/media/p1-workspace-modules.png'
build-ci-msvc/Release/MyRenderer.exe assets/scenes/18_atmosphere_sky.myscene

# Rendered capture of a fixed scene
$env:MYRENDERER_SCREENSHOT='build-ci-msvc/p1a-sky-golden.png'
build-ci-msvc/Release/MyRenderer.exe assets/scenes/18_atmosphere_sky.myscene

# Raster vs path-traced comparison (raster.png / path-traced.png / difference.png / triptych.png)
$env:MYRENDERER_REFERENCE_COMPARE_DIR='build-ci-msvc/p1a-atmosphere-comparison'
build-ci-msvc/Release/MyRenderer.exe assets/scenes/18_atmosphere_sky.myscene
```

Compose multiple captures into one captioned figure with `System.Drawing` through `Add-Type -AssemblyName System.Drawing`; draw Chinese captions with the `Microsoft YaHei` font.

## Keep documents true

- Every target, `.myscene`, environment variable, and type name a document mentions must exist. Renaming one means updating every document that names it.
- A stage is not complete until the document carries implementation, tests, numbers, a figure, and limits — see section 8 of `todolist.md`.
- Link both ways: `todolist.md` points at the document as stage evidence, and the document's 下一步 points at a concrete `todolist.md` entry.
- Cross-link the sibling skills instead of repeating them: `$myrenderer-visual-regression` for baseline surfaces and thresholds, `$myrenderer-build-and-test` for the verification matrix, `$myrenderer-feature-development` for the rendering contracts a document describes.

## Review checklist

Before finishing a document change, confirm: Chinese prose with explained English terms; metadata block present; at least one captioned, reproducible figure; numbers carry units and a baseline; limits and unverified items are written down; commands are paste-ready; named targets and paths exist; no regression baseline moved unintentionally.
