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

### Custom 节点参考的适用范围

上述 struct／成员函数和命名规范继续适用，但旧 `MMDBaseToon`／Anime 材质仅代表兼容路径，不能据此推断原生材质与延迟 BSDF 的入口。

---

## 当前渲染架构（2026-10-19 按源码核对）

渲染工作先读 [当前架构](MMD/Plugins/Ue5MMDTools/Docs/RENDERING_ARCHITECTURE.md) 和 [文档入口](MMD/Plugins/Ue5MMDTools/Docs/README.md)。本节替换了旧的“所有着色均为 Unlit + LightDataRT”摘要；旧摘要已失效。

### 终末地 V2：已接入原生延迟光照

- `MMDEndfieldToonPresetsCommandlet.cpp` 创建 Substrate Toon 材质，接 `FrontMaterial`，并生成专用 payload CustomOutput。
- 不透明／Masked 在 BasePass 写入 Substrate packed 材质缓冲，延迟逐灯读取，由 `TMMDEndfieldNativeBSDF.ush` 消费当前灯及原生 `FShadowTerms`。俗称“走 GBuffer”，实际为 Substrate packed 布局，不是新增插件 GBuffer RT。
- GI／反射接入 `TMMDEndfieldNativeIndirect.ush` 的原生合成阶段。透明 Skin／附脸层走 Surface Forward inline payload，能力不等于不透明 GI，也不称 packed GBuffer 读回。
- 当前共享 Backend `mmd-native-bsdf-v15-style-profiles`／Cooked协议15；Graph7、终末地Profile布局42、payload5／FaceData5／Frame1保持。终末地与暖暖共享原生入口，风格响应及 Profile 配方独立。
- 依赖固定 UE 5.8.3 / CL58210709；在工程 Intermediate 的受校验 shader 副本上适配并映射虚拟路径，安装引擎文件不写入。失败须保留原生／兼容回退，不模糊匹配私有接口。
- Substrate、原生父材质、自定义后端启用和 Profile 标记是不同条件；核对实际选材质路径，不只看预设名称或项目开关。完整说明见 [原生后端](MMD/Plugins/Ue5MMDTools/Docs/ENDFIELD_NATIVE_BACKEND.md)。

### Reference／旧材质：仍保留 Unlit 兼容路径

- `M_MMD_Reference_*`、旧 Endfield 和部分通用材质仍读取 LightDataRT、插件阴影 RT、天空 SH 与环境镜面。旧数据布局与参数以实际 C++／USH 为准，不再使用旧64×2／单级四texel／SceneCapture摘要。
- 原生 V2 不依赖旧灯光和阴影 RT 取光，但子系统仍可能运行；没有实测就不能声称消除了重复成本。
- 2026-10-19 最初暖暖实验误用旧 Unlit 模板并被用户否决，现已迁移为独立原生父材质／六类 Profile，复用共享后端。暖暖 Profile 不写终末地 RD／RS；旧三份 Unlit 材质仅留兼容回退。实现与验收见 [原生暖暖说明](MMD/Plugins/Ue5MMDTools/Docs/INFINITY_NIKKI_NATIVE_RENDERING.md)，不要把旧实验截图当作迁移后的画面。
- 后续暖暖等原生风格应先复用或扩展已有原生材质／载荷／逐灯入口，风格响应独立维护；不得因名称不同无意退回旧 Unlit 光照链。

### Face、描边与通用性

- `TMMDAnimeFace.usf` 仍由用户维护，AI 不改。
- 已有 Face V5 使用几何派生头部投影与自动缓存，不要求作者配套固定 UV 阈值图；没有有效数据时保留仅底色的通用几何兜底。以 [Face V5说明](MMD/Plugins/Ue5MMDTools/Docs/ENDFIELD_FACE_V5_GUIDE.md) 为准，不用旧 Version3 布局或“头骨子系统冗余”结论。
- 渲染期描边已经存在：`MMDOutlineLibrary`、PMX overlay 与 `TMMDEndfieldOutline.ush`；材质拾取的编辑器选区描边是另一套功能。不要再写“渲染期描边未实现”。
- 皮肤局部 LUT／暖红响应不代表真正空间 SSS、邻域扩散或厚度透射；原生接入也不代表完整原作画质验收。

## 插件其他入口

- 导入：`TPMXParser`、`TVMDParser`、`TMMDMeshBuilder`；PMX 产出 SkeletalMesh／材质，VMD 产出动作与表情曲线。
- 动画／物理：`AMMDActor`、`AMMDLevelSequenceActor`、`AGN_MMDSkeletalControl`、Bullet 模拟器。
- 编辑器：`MMDToolPanelWidget`、`MMDToolPreviewRenderer`、`MMDViewPanel`、`MMDImportSetting`、`MMDMaterialPickerMode`。
- 舞台与角色应用：`MMDStageLibrary` 负责实际选材质、角色覆盖、Profile／Face绑定和原材质恢复。

## 文档与验证约定

- `Docs/ANIME_RENDERING_GUIDE.md` 描述的旧全屏重映射已无源码，现已删除。不能把旧 Compute 重映射与原生延迟 BSDF 混为一谈。
- `PHASE_TWO_*`、旧路线图和有日期截图是历史材料；其中的“当前”“下一步”或旧聊天指令不能覆盖当前源码和用户授权。
- 先核对源码／实际材质路径，再判断文档。架构变化同步更新本节和文档总入口，不能笼统断言整个 Docs 都过时。
- 编译、槽绑定、协议读回与截图数量只证明对应功能；效果须目检并与目标对照。历史 GPU／Cook／截图注明真实日期，未重测不能写成本轮通过。

## 其他约定

- 注释用中文，只保留必要注释。
- 不修改安装引擎文件；原生适配只操作工程内受校验的副本。
- 用户既有未提交资产、历史截图、研究证据与无关工作保持，不据旧交接批量恢复。
- 若需在 Custom struct 输出上叠加计算，可将实例化和 return 移到节点，由节点调用成员函数后继续合成。
