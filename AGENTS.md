# Ue5MMDTools 项目说明

MMD 相关 UE5 工具插件项目。核心代码在 `MMD/Plugins/Ue5MMDTools/`，shader 在 `MMD/Plugins/Ue5MMDTools/Shaders/TMMDShader/`。

---

## 🚨 通用性铁律（最重要，所有 shader 必须遵守）

着色器必须**所有模型通用、不依赖 UV 布局、不依赖任何配套贴图**：

1. **最少输入 = 一张 base_color**（`light_data_tex` 可选）就要能完整着色。
2. **禁止依赖特定 UV 布局 / 配套贴图**：不许在"脸的 UV 空间"采样阴影阈值图来决定阴影形状
   （如 MMD SDF 脸阴影、R/G 分半贴图那套）——那是模型+贴图绑死、换模型就错位的方案，**弃用**。
3. 所有着色只能从**几何数据**计算：
   - 明暗/阴影 → `n_dot_l` + toon ramp + `shadow_color`（阴影色用 base_color 压暗派生，保留色相，无需贴图）；
   - 高光 → `n_dot_h`；边缘光 → `1 - n_dot_v`；环境 → SH。
4. 想要的"阴影形状/脸部细节"必须**过程化**（从法线/位置/角度算），不能靠贴图阈值。
5. 节点输入要有**默认值/兜底**，漏连也正常显示（退回通用 toon），不能漏连就黑/错。

---

## ⚠️ Shader 编写铁律（UE 材质 Custom 节点）

材质 **Custom 节点的代码会被粘贴进一个生成的函数体内**（`CustomExpressionN(...) { ... }`），因此：

1. **禁止定义独立的函数** → 会报 `function definition is not allowed here`。
2. **禁止用 `#define` 宏** 包装逻辑（本项目已弃用）。
3. **禁止修改引擎文件**（`MaterialTemplate.ush` 等）——插件要分发，用户引擎不是魔改的。
4. **禁止内联语句块代替函数**——那会丢失函数级代码复用。

### ✅ 正确写法：struct + 成员函数（MStar 风格）

struct 声明（含成员函数）是"类型声明"，在函数体内合法——这是 Custom 节点里做函数复用的标准做法。

`.usf` 文件模板：

```hlsl
// xxx.usf
struct TMyShader
{
    // 成员函数 = 真正的 HLSL 函数，可复用
    float3 OutputColor(FMaterialPixelParameters Parameters, ...)
    {
        ...
        return result;
    }
};

// 自包含入口：文件末尾自行实例化 + return
TMyShader S;
return S.OutputColor(Parameters, ...);
```

材质 Custom 节点：

```
OutputType: float3
Code:
    #include "/Plugin/Ue5MMDTools/TMMDShader/xxx.usf"
    return 0;   // 兜底返回值（正常不可达），防某些编译变体报"无返回值"
```

### 节点输入约定

- **需连线**的输入（以各文件头注释为准）：`base_color`、`light_data_tex`、`shadow_step`、`highlight_step`、`shadow_color` 等。
- **自动可用、无需连线**：
  - `Parameters`（`FMaterialPixelParameters`）
  - `light_data_texSampler`（Texture2D 输入 `light_data_tex` 自动生成的采样器）
- include 虚拟路径：`/Plugin/Ue5MMDTools/TMMDShader/`。

### 命名规范（snake_case）

- **变量 / 参数 / struct 成员 / 循环变量**：全小写 + 下划线，如 `base_color`、`light_data_tex`、`world_normal`、`diffuse_light`；数学量 `NdotL`/`NdotH`/`NdotV` 在代码里写作 `n_dot_l`/`n_dot_h`/`n_dot_v`。
- **类型名（struct）**：PascalCase，保留 `TMMD` 前缀作类型标记，如 `TMMDSurfaceData`、`TMMDToonLight`。
- **函数名（成员函数）**：PascalCase，如 `ComputeLighting`、`ShadeSurface`、`SampleMMDShadow`。
- **引擎固定名不碰**：`Parameters`、`View`、`ResolvedView`、`LWCToFloat`、`GetWorldCameraOrigin`、`Texture2DSample` 等。
- **单字母局部量保留原样**：方向 `L`、半程向量 `H`、循环 `i`。
- **Texture 输入自动生成的采样器** = `<输入名>Sampler`（引擎固定后缀，不可改），故为混合形：`light_data_tex` → `light_data_texSampler`、`mat_cap` → `mat_capSampler`、`mmd_shadow_map` → `mmd_shadow_mapSampler`。

区分逻辑（四层一眼分清）：类型 = PascalCase + `TMMD` 前缀；函数 = PascalCase；变量/参数 = snake_case；HLSL 内置 = 全小写（`saturate`/`dot`/`lerp`）。

### 已按此规范写的文件（新 shader 照抄）

- `MMD/Plugins/Ue5MMDTools/Shaders/TMMDShader/MMDBaseToon.usf` — 通用基础 toon 着色器（struct `TMMDBaseToon`）
- `TMMDAnimeSkin.usf` — 皮肤（struct `TMMDAnimeSkin`）
- `TMMDAnimeCloth.usf` — 布料（`#include "MMDBaseToon.usf"` 复用）
- `TMMDAnimeHair.usf` — TODO 占位

---

## 📐 当前光照架构（现状，供后续 AI 快速对齐上下文）

### 总览：插件内自算光照（不改引擎 ShadingModel）

所有着色在**材质 Custom 节点**里用 HLSL 手算（等效"Unlit + 自己算光照"），不依赖引擎延迟光照。
数据来源是插件写入的灯光数据 RT `light_data_tex`（`/Ue5MMDTools/Rendering/LightDataRT`，
由 `UMMDAnimeLightDataSubsystem` 每帧写入）。

**light_data_tex 布局**（`MaxLights=16`，宽 `64 = 16*4`，**高 2**，RGBA32F）——第 0 行每盏灯占 4 个 texel：

| texel | 内容 |
|---|---|
| P (`+0`) | `xyz`=灯光世界位置，`w`=类型（<1.5点光 <2.5聚光 <3.5平行光 否则面光） |
| C (`+1`) | `rgb`=灯光颜色，`w`=强度 |
| D (`+2`) | `xyz`=方向（平行光=光照方向），`w`=半径 |
| T (`+3`) | 附加（聚光内外锥角、面光朝向） |

**第 1 行（y=1，x=0..3）** = MMD 场景阴影相机基（`UMMDShadowMapSubsystem` 写入，材质侧 `SampleMMDShadow` 读）：
texel0=`(Origin.xyz, Valid)`、texel1=`(Right/OrthoWidth, GlobalBias)`、texel2=`(Up/OrthoWidth, 0)`、texel3=`(Forward.xyz, TexelSize)`。
阴影深度存 `/Ue5MMDTools/Rendering/MMDShadowMapRT`（RGBA16F 2048²，`ASceneCapture2D` + `SCS_SceneDepth` 从主平行光视角渲染，R=沿光轴线性深度 cm）。

### 核心工具库 `Shaders/Core/MMDToonLighting.ush`（struct `TMMDToonLight`）

| 函数 | 作用 |
|---|---|
| `LightParams(P, D, T, world_pos, out L, out atten)` | 单灯方向 L（指向光源）+ 衰减 atten（点/聚光按距离，平行光=1） |
| `ComputeLighting(..., out diffuse_light, out dir_light, out specular_light)` | 累加全部灯：`diffuse=Σ C.rgb*(C.w/π)*n_dot_l*atten`；`dir_light=Σ C.rgb*n_dot_l*atten`（**不含光强**）；`specular=Σ C.rgb*(C.w/π)*pow(n_dot_h,power)*atten` |
| `ToonRamp(shadow_step, n_dot_l, softness)` | 卡通阴影硬阈值 ramp（softness 标准 0.05 / 脸 0.12） |
| `ApplyToonShadow(diffuse, dir_light, specular, shadow_step, shadow_color, softness, out shaded_specular)` | toon 阴影合成：**用 dir_light 亮度做 ramp**（光强不改变阴影位置），混阴影色，高光按 ramp 门控 |
| `ApplyNormalMap(Parameters, normal_map, sampler, strength)` | BC5 解码 RG→[-1,1]，`n_xy *= strength` 缩放后重建 z，TBN→世界；强度 0 退化为几何法线 |
| `SkyAmbient(world_normal)` | 天空 SH 环境光（与 PBR 同公式） |
| `SampleMatcap(world_normal, mat_cap, sampler)` | 视图空间 matcap 查找采样（乘/加由材质定） |
| `TotalLightIntensity(Parameters, light_data_tex, sampler)` | 总光强（不含天空），给 Rim 等用 |
| `SampleMMDShadow(Parameters, light_data_tex, sampler, shadow_map, sampler, bias)` | 场景阴影（主平行光遮挡）：读 light_data_tex 第 1 行阴影相机基 → 世界坐标投到阴影空间 → 采样 MMDShadowMapRT 深度比较（4-tap PCF）。Enabled<=0 或 valid==0 返回 1 |
| `GetMainLightDirection(...)` / `GetMainLightData(...)` | 找最强主光方向/颜色 —— **face 已回退，当前暂未被使用** |

### 材质 usf 一览（`Shaders/TMMDShader/`）

| 文件 | struct | 状态 |
|---|---|---|
| `MMDBaseToon.usf` | `TMMDBaseToon` | **基础**：ApplyNormalMap → ComputeLighting → SampleMMDShadow(场景遮挡) → ApplyToonShadow(0.05) → 天空(乘 base_color) → matcap。输入：base_color/light_data_tex/shadow_step/highlight_step/shadow_color/specular_power/normal_map/normal_map_strength/mat_cap/sphere_mode/**mmd_shadow_map/mmd_shadow_bias** |
| `TMMDAnimeCloth.usf` | — | `#include "MMDBaseToon.usf"` 复用基础 |
| `TMMDAnimeSkin.usf` | `TMMDAnimeSkin` | ⚠️ **旧实现**：自带 toon 循环，未用 Core 工具库（可后续迁移） |
| `MMDAnimeEye.usf` | `TMMDAnimeEye` | 完全特化（虹膜自发光/白高光/睫毛阴影/湿润高光），自带循环 |
| `TMMDAnimeFace.usf` | `TMMDAnimeFace` | **用户自行维护，AI 别动**。当前为只依赖 base_color 的通用版本 |
| `TMMDAnimeHair.usf` | — | TODO（计划 Kajiya-Kay 各向异性高光） |

### 光照/阴影关键点（改代码前必读）

1. **天空环境光必须乘 base_color**：`base_color * (shaded_diffuse + SkyAmbient*SkyLightColor) + specular`，否则天空光洗掉色相。
2. **toon 阴影 ramp 用 `dir_light`（不含光强）**：`C.w`=光强（预览主光 4.0 → C.w/π≈1.27），若用含光强 diffuse 做 ramp 会提前饱和、阴影消失。
3. 归一化：每灯 `C.rgb * (C.w / π)`，默认强度 ~3.14 的平行光 ≈ 1.0（接近 PBR）。
4. UE 世界坐标：**X=右 Y=前 Z=上**，水平面是 `.xy`（不是 Unity 的 `.xz`）。
5. 材质 Custom 节点新增输入需在 Details 里加同名并设默认值；Texture 输入自动生成 `<输入名>Sampler`。

---

## 🧩 插件功能总览（按当前源码；`Docs/` 里旧文档已过时，别照抄）

### 导入（PMX / VMD）
- `TPMXParser` — MMD **PMX** 模型解析：顶点/材质/骨骼/变形(Morph)/物理刚体关节/边线数据（`EdgeColor`/`EdgeSize`/`EdgeScale`）。
- `TVMDParser` — MMD **VMD** 动作解析。
- `TMMDMeshBuilder` — 从 PMX 构建原生 UE `SkeletalMesh` + 材质实例，并把 `EdgeColor`/`EdgeSize` 等写入材质参数。
- 产出：`SkeletalMesh` / `AnimSequence` / `LevelSequence`（见 `.uplugin` 描述）。

### Actor / 动画 / 物理
- `AMMDActor` — MMD 角色 Actor（加载 PMX、挂骨骼网格 + 物理）。
- `AMMDLevelSequenceActor` — LevelSequence 支持。
- `AGN_MMDSkeletalControl`（AnimNode）— MMD 骨骼控制节点，配合 `MMDPhysicsSimulator`（Bullet 物理，刚体/关节数据见 `FMMDPhysicsRigidBodyData` / `FMMDPhysicsJointData`）。

### 渲染 / 光照（LightDataRT 系统）
- `UMMDAnimeLightDataSubsystem` — 每帧把场景灯光写入 `LightDataRT`（`/Ue5MMDTools/Rendering/LightDataRT`，**64×2**：第 0 行 16 灯×4 texel P/C/D/T，第 1 行阴影相机基，见上文"当前光照架构"）。
- `UMMDShadowMapSubsystem` — MMD 场景阴影（主平行光遮挡）：隐藏 `ASceneCapture2D`（正交 + `SCS_SceneDepth`）每帧渲染主光视角场景深度到 `MMDShadowMapRT`（RGBA16F 2048²），并把阴影相机基写入 LightDataRT 第 1 行；默认排除 AMMDActor（不自阴影）。BP：`SetShadowEnabled/SetShadowMapRenderTarget/SetShadowDistance/SetOrthoWidth/SetGlobalBias/SetHideMMDActors`。
- `FMMDAnimeLightViewExtension`（Private/Rendering）— SceneViewExtension，SetupViewFamily 里先更新阴影相机再收集灯光，渲染期注入灯光写入 compute pass。
- `MMDAnimeWriteLights` + `Shaders/PostProcess/MMDAnimeWriteLights.usf` — 灯光→RT 的 compute shader（64 线程写第 0 行 + 4 线程写第 1 行）。
- `MMDAnimeEnvironmentUniformBuffer` — 环境光照 UniformBuffer（平行光/点光/聚光/面光/雾/toon shadow color），材质里可采。
- 材质侧 toon 光照：`Shaders/Core/MMDToonLighting.ush` + `Shaders/TMMDShader/*.usf`（见上文"当前光照架构"）。
- `UMMDHeadBoneSubsystem` — 头骨朝向 → 材质参数 `MMDHeadForward/MMDHeadRight`。**face 已回退，当前冗余**（待删/待定）。

### 编辑器 / UI
- `MMDToolPanelWidget` — 工具面板；`MMDToolPreviewRenderer` — 预览渲染（KeyLight=4.0 等）；`MMDViewPanel` — 编辑器视口（加载/预览模型、物理烘焙预览）；`MMDImportSetting` — 导入设置。
- `MMDMaterialPickerMode` — **材质拾取模式**：复用 UE 原生选区描边（EditorSelection + `PostProcessSelectionOutline.usf`），进入时临时把 `SelectionOutlineColor` 改成亮品红；overlay 网格描边 + 抑制源网格自身描边。

### 描边（Outline）现状
- ✅ 已导入：PMX 边线数据 `EdgeColor`/`EdgeSize`/`EdgeScale`（写入材质参数）。
- ✅ 编辑器：材质拾取模式的 UE 原生选区描边（`MMDMaterialPickerMode`）。
- ⚠️ **渲染期描边 shader 未实现**：当前 `TMMDShader/*.usf` 没有消费 Edge/Outline 的代码，PMX 边线参数已进材质但没 shader 用（待实现 / 用户自己加）。

### ⚠️ 注意：`Docs/` 旧文档已过时
- `Docs/ANIME_RENDERING_GUIDE.md` 描述的是**旧的后处理卡通渲染系统**（`MMDAnimeToonRemapCS` / Stencil 分类 / `ToonShadowRemap.ush` / `ScreenSpaceRim.ush` / `AnimeSpecularPP.ush`）——**当前源码里已没有这套**，现在是 LightDataRT + 材质内算光照。**别按旧文档实现**，以 `Shaders/` 实际源码 + 本 AGENTS.md 为准。

---

## 其他约定

- 注释用**中文**，只保留必要注释。
- 引擎 `MaterialTemplate.ush` 每个进程只读一次缓存，改了必须重启编辑器才生效——**不要动它**。
- 若需在 struct 基础上叠加（如 rim/SSS），删掉文件末尾的实例化+return，改为在节点里手动调用 `S.OutputColor(...)` 后继续拼。
