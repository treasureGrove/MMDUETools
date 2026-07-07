# MMD Anime 渲染系统 使用指南

## 概述

Ue5MMDTools 内置了一个基于 Compute Shader + RDG（Render Dependency Graph）的后处理式 Anime/Cel-Shading 渲染系统。它会在 Tonemap 之前劫持场景颜色，对标记了 Custom Depth/Stencil 的 MMD 模型像素进行卡通化重映射，实现类似 ARC System Works / Genshin 风格的二次元渲染效果。

---

## 工作原理

```
SceneColor → [MMDAnimeToonRemapCS] → Toon SceneColor
                ↑
     GBufferA / Depth / Stencil
```

1. 引擎正常渲染所有物体到 `SceneColor`
2. **Tonemap 前**，`FMMDAnimeViewExtension` 注入 compute pass
3. 对于 stencil 标记的 MMD 像素，执行以下处理：
   - **Toon Shadow Remap** — 基于亮度阈值做阴影色阶重映射（支持身体/皮肤/面部独立参数）
   - **Screen Space Rim** — 深度/法线边缘检测 + 背光边缘光
   - **Anime Specular** — 卡通化高光（头发各向异性 / 眼睛锐利 / 通用）
4. 非 MMD 像素直接 passthrough，不影响其他场景物体

## 材质分类（Stencil）

插件通过材质名自动分类，默认值：

| 类别 | Stencil 值 | 触发关键词 |
|------|-----------|-----------|
| 身体/衣物 | 1 (BodyCloth) | `body`, `cloth`, `衣服` 等 |
| 皮肤 | 2 (Skin) | `skin`, `face`, `head`, `body` 等 |
| 头发 | 3 (Hair) | `hair`, `髪`, `tail` 等 |
| 面部 | 4 (Face) | `face`, `head`, `目`, `eye` 等 |
| 眼睛/金属 | 5 (EyesMetal) | `eye`, `瞳`, `metal`, `芯` 等 |

---

## Shader 文件结构

```
Shaders/PostProcess/
├── MMDAnimePostProcess.usf      # 主 compute shader
├── ToonShadowRemap.ush          # 卡通阴影重映射 （3 种模式）
├── ScreenSpaceRim.ush           # 屏幕空间边缘光
└── AnimeSpecularPP.ush          # 卡通高光

Shader 编译的 class 映射路径：
  /Plugin/Ue5MMDTools/PostProcess/MMDAnimePostProcess.usf
```

---

## 参数说明

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `ShadowThreshold` | 0.35 | 阴影判定阈值，越大阴影区域越多 |
| `ShadowSoftness` | 0.05 | 阴影边缘柔化度（0=硬边，接近 cel） |
| `ShadowColor` | (0.6, 0.5, 0.7) | 一级阴影色 |
| `DeepShadowColor` | (0.3, 0.25, 0.4) | 二级深阴影色 |
| `RimWidth` | 0.3 | 边缘光宽度 |
| `RimColor` | (1.0, 1.0, 1.0) | 边缘光颜色 |
| `EnvironmentStrength` | 0.5 | 环境光对阴影色的影响强度 |
| `SpecularSize` | 0.15 | 高光大小（越小越大块） |
| `SpecularHardness` | 0.85 | 高光硬度（1=完全硬边 cel 高光） |

---

## 如何启用

插件加载时会自动注册 `FMMDAnimeViewExtension`，无需额外操作。

### 条件检查：
- 引擎 **必须是 Deferred Rendering** 路径（Forward 下 GBuffer 不可用）
- 需要 **SM6** Feature Level（`ShouldCompilePermutation` 已限定）
- MMD 模型需设置 **Render CustomDepth Pass** 才能写入 Stencil

---

## 自定义渲染效果

### 调整参数

编辑 `Ue5MMDTools.Build.cs` → 修改 `FMMDAnimeRenderParams` 中的默认值：

```cpp
Params.ShadowThreshold = 0.35f;   // 改这里
Params.ShadowSoftness  = 0.05f;
Params.RimWidth        = 0.3f;
Params.SpecularSize    = 0.15f;
```

### 修改 Shader

直接编辑 `Shaders/PostProcess/` 下的 `.usf` / `.ush` 文件：

- **改阴影分层** → `ToonShadowRemap.ush` 的 `RemapToToon()` 函数
- **改边缘光算法** → `ScreenSpaceRim.ush` 的深度/法线检测阈值
- **改高光形状** → `AnimeSpecularPP.ush` 的 `ComputeAnimeSpec()` / `ComputeHairSpec()` / `ComputeEyeSpec()`

修改后重新编译项目和 Shader 即可生效。

---

## 代码架构

```
Public/Rendering/
├── MMDAnimeViewExtension.h       # SceneViewExtension 接口
├── MMDAnimePostProcessPass.h     # FMMDAnimeRenderParams 参数结构
├── MMDAnimeStencilValues.h       # Stencil 分类常量
└── MMDAnimeRenderSettings.h      # DataAsset（蓝图可配置）

Private/Rendering/
├── MMDAnimeToonRemapCS.h         # FGlobalShader class 定义
├── MMDAnimeShaders.cpp           # IMPLEMENT_GLOBAL_SHADER
├── MMDAnimePostProcessPass.cpp   # RDG Compute Pass 实现
└── MMDAnimeViewExtension.cpp     # SceneViewExtension 实现
```

### 注册流程

```
StartupModule()
  → FSceneViewExtensions::NewExtension<FMMDAnimeViewExtension>()
  → SubscribeToPostProcessingPass(Tonemap)
  → PostProcessCallback_RenderThread()
  → AddMMDAnimePostProcessPass()
  → FComputeShaderUtils::AddPass(FMMDAnimeToonRemapCS)
```

---

## 已知限制

1. 当前 ViewExtension 使用硬编码的默认参数，`UMMDAnimeRenderSettings` DataAsset 的运行时绑定尚未实现（需后续版本）
2. GBufferA / SceneDepth / CustomStencil 纹理绑定为 fallback 黑色纹理（需接入实际 SceneTextures）
3. 仅支持 Deferred Rendering + SM6 平台
4. 面部阴影使用特殊的更柔和的算法，目前肤色固定为暖色调

---

## 参考

- B 站演示：https://www.bilibili.com/video/BV1M2RXBPExk/
- 项目主页：https://treasureGrove.github.io/MMDUETools/
- UE RDG 文档：https://docs.unrealengine.com/5.5/en-US/render-dependency-graph-in-unreal-engine/
