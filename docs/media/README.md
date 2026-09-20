# 作品集与文档媒体

本目录存放**文档插图**：用来解释功能、UI 或前后对照的截图与视频，不参与任何自动像素比对。规范见 [`../README.md`](../README.md) 第 3 节。

回归基线与历史证据在 [`../images/`](../images/)、[`../reference-images/`](../reference-images/) 与 [`../performance/`](../performance/)，它们的清单在各自的 `README.md`；不要把说明性截图放进那三个目录。

## 现有素材

| 文件 | 内容 | 重拍方式 |
| --- | --- | --- |
| `prism5_demo_reel.mp4` | Prism-5 确定性 360 帧参数动画，24 fps 编码为 15 秒 1280 × 720 作品集预览 | 见 [`../prism5-validation.md`](../prism5-validation.md)；PNG 序列只生成在构建目录，不入库 |
| `p1-workspace-1440x900.png` | 默认 Dock 工作区：中央 Viewport、Scene Explorer、Inspector、底部多标签工作区 | `MYRENDERER_EDITOR_WINDOW_WIDTH=1440 MYRENDERER_EDITOR_WINDOW_HEIGHT=900` + `MYRENDERER_EDITOR_SCREENSHOT=docs/media/p1-workspace-1440x900.png` 运行 `build-ci-msvc/Release/MyRenderer.exe assets/scenes/18_atmosphere_sky.myscene` |
| `p1-workspace-1100x680.png` | 同一工作区在 1100 × 680 应用下限下的布局与可达性 | 同上，窗口尺寸改为 `1100` × `680` |
| `p1-workspace-render-queue.png` | Render Queue 标签页：任务路径输入、Enqueue、空队列状态与恢复诊断 | 加 `MYRENDERER_EDITOR_SCREENSHOT_TAB=render-queue` |
| `p1-workspace-modules.png` | Modules 标签页：真实 Module Registry 清单（ID / Name / Kind / Target / Source / API 版本 / Build ID） | 加 `MYRENDERER_EDITOR_SCREENSHOT_TAB=modules` |
| `p1-workspace-log-profile.png` | Log / Profile 标签页的汇总诊断 | 加 `MYRENDERER_EDITOR_SCREENSHOT_TAB=log` |
| `c1-module-inspector.png` | Inspector 的 Module 页：模块选择、Seed 与空状态文案 | 加 `MYRENDERER_EDITOR_SCREENSHOT_TAB=module` |
| `p1a-atmosphere-keylight-before-after.png` | 逐通道关键光颜色接入前后的金时刻对照（960 × 540 双栏合成） | `MYRENDERER_SUN_ELEVATION=10 MYRENDERER_SUN_AZIMUTH=120` 下各拍一张 960 × 540 截图后并排合成 |
| `p1a-aerial-perspective-on-off.png` | Aerial Perspective 开关对照：关闭时地面一直铺到地平线，开启后远景失去对比度并向天空色靠拢（960 × 540 双栏合成） | `MYRENDERER_SUN_ELEVATION=14 MYRENDERER_SUN_AZIMUTH=128 MYRENDERER_SKY_TURBIDITY=1.4` 下分别用 `MYRENDERER_AERIAL_PERSPECTIVE=0` 与 `=1`（`MYRENDERER_AERIAL_SCALE_HEIGHT=12`）各拍一张后并排合成 |

新增素材后在本表补一行：文件名、这张图证明什么、怎么重拍。
