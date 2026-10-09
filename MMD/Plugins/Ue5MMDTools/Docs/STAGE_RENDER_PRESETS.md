# 舞台与角色渲染预设使用说明

本文说明当前 `AMMDStageActor`、`UMMDStageRenderPreset` 和 Reference 角色材质的实际用法。资产根目录均为 `/Ue5MMDTools/Stages/`。

## 动态舞台

当前有三套可直接放入关卡的动态舞台。蓝图继承 `AMMDStageActor`，地图已放置对应舞台、主镜头和 PlayerStart。

| 舞台 | Blueprint | Map |
|---|---|---|
| 霓虹演唱会 | `/Ue5MMDTools/Stages/Blueprints/BP_MMDStage_NeonConcert` | `/Ue5MMDTools/Stages/Maps/MMDStage_NeonConcert` |
| 月光镜庭 | `/Ue5MMDTools/Stages/Blueprints/BP_MMDStage_MoonMirror` | `/Ue5MMDTools/Stages/Maps/MMDStage_MoonMirror` |
| 樱庭水镜 | `/Ue5MMDTools/Stages/Blueprints/BP_MMDStage_SakuraTheater` | `/Ue5MMDTools/Stages/Maps/MMDStage_SakuraTheater` |

舞台 Actor 自带主光、辅光、轮廓光、局部强调光和后处理。`bAnimate` 控制运行时动画，`bPreviewAnimation` 控制编辑器视口预览。需要 Sequencer 或固定帧截图时，可启用 `bUseManualTime` 并驱动 `ManualTimeSeconds`，也可直接调用 `EvaluateStageAtTime`。

## 七套渲染预设

七套 DataAsset 都能同时设置舞台灯光、后处理、toon 参数和角色父材质。只有前三套拥有配套动态舞台蓝图与地图；其余预设可应用到任意 `AMMDStageActor`。

| 显示名 | DataAsset | 角色材质方向 | 是否切换舞台主题 |
|---|---|---|---|
| 霓虹演唱会 | `/Ue5MMDTools/Stages/Presets/DA_Render_NeonConcert` | Genshin | 是 |
| 月光镜庭 | `/Ue5MMDTools/Stages/Presets/DA_Render_MoonMirror` | Wuthering | 是 |
| 樱庭水镜 | `/Ue5MMDTools/Stages/Presets/DA_Render_SakuraTheater` | Genshin | 是 |
| 银白柔光 | `/Ue5MMDTools/Stages/Presets/DA_Render_SilverCity` | SilverCity | 否 |
| 明快卡通 | `/Ue5MMDTools/Stages/Presets/DA_Render_OpenWorldToon` | Genshin | 否 |
| 工业动画 | `/Ue5MMDTools/Stages/Presets/DA_Render_IndustrialAnime` | Endfield | 否 |
| 电影冷暖 | `/Ue5MMDTools/Stages/Presets/DA_Render_CinematicAnime` | Wuthering | 否 |

“不切换舞台主题”表示预设保留 Actor 当前的几何主题，但仍更新灯光、后处理和角色材质参数。

## 四个角色材质方向

Reference 角色材质有四个风格方向，每个方向各有 `Opaque`、`Masked`、`Translucent` 三个父材质，共 12 个资产：

| 方向 | 父材质路径模板 |
|---|---|
| Genshin | `/Ue5MMDTools/Stages/Materials/M_MMD_Reference_Genshin_{Opaque|Masked|Translucent}` |
| Wuthering | `/Ue5MMDTools/Stages/Materials/M_MMD_Reference_Wuthering_{Opaque|Masked|Translucent}` |
| Endfield | `/Ue5MMDTools/Stages/Materials/M_MMD_Reference_Endfield_{Opaque|Masked|Translucent}` |
| SilverCity | `/Ue5MMDTools/Stages/Materials/M_MMD_Reference_SilverCity_{Opaque|Masked|Translucent}` |

四个方向共用通用几何光照，但分别调整 toon 分区、粗糙度、皮肤响应、发丝高光、布料、金属和轮廓光。应用时会按源材质的 BlendMode 选择同方向的对应父材质，并复制基础贴图、基础色、Opacity 和 matcap 参数。

## 物理环境响应与樱庭水镜

Reference 材质在 Unlit Custom 内实现逐灯 GGX/Smith/Schlick BRDF。光强、灯色与距离衰减参与真实直射；toon 分区只调整漫反射。皮肤、头发、布料和金属分别使用不同粗糙度及 F0，阴影侧不再生成固定主光补亮。

环境漫反射来自真实 SkyLight SH，镜面来自材质图的 `SkyLightEnvMapSample` 节点及粗糙度预过滤。`AmbientStrength` 控制环境漫反射，`EnvironmentSpecularStrength` 控制环境镜面；天空颜色只乘一次，SH 不重复除以 π。有效 LightDataRT 的零灯数据会保持零直射；仅在灯光数据缺失且天空也不可用时启用通用预览兜底。

这属于插件内的着色实现，尚未接入新的引擎 GBuffer ShadingModel。它使用全局天空反射和插件主平行光阴影，不会自动获得局部 Reflection Capture、Lumen/SSR 角色反射、引擎 AO 或点光阴影；庭院的 Default Lit 材质则沿用引擎环境响应。

头发支持可选通用偏移贴图 `HairShiftTexture`，默认使用自动生成的原创 128² G8 平铺资产 `T_MMD_GenericHairShift`。`HairShiftStrength` 默认 0.5，设为 0 时跳过偏移贴图采样；通用发丝法线由 `HairDetailStrength` 独立控制，两者均为 0 时退回几何法线与基础 BRDF。`HairShiftScale` 默认每厘米 0.045。贴图从物体空间三向投影采样，不要求模型 UV 布局，方向跟随 Actor 转动。直射采用双瓣各向异性 GGX，环境采用偏移法线配合引擎各向同性预过滤的近似；大幅骨骼变形可能产生细节游移。

角色预设默认启用 `bCharacterSelfShadow`。套用预设时组件通过 `MMDShadowInclude` 进入插件主光深度，刘海、领口与手臂可以产生真实遮挡；关闭选项会临时排除该组件，恢复原材质时恢复原有阴影标签。显式 `MMDShadowExclude` 与子系统排除列表仍优先。角色阴影采用较小接收偏移及逐 texel 接收平面校正，原环境阴影偏移与过滤质量设置保持独立。辅助灯暂不采单独阴影图，应控制补光和天空漫反射，保留主光暗部层次。

樱庭水镜使用原创亭阁、屋瓦、枝干、花簇、石灯、岩石与涟漪网格，并配落花、涟漪和局部灯光动效。木材、石材与 ARM 贴图通过世界三向投影使用；庭院水面由法线波动、Fresnel 与引擎反射形成，不用自发光模拟水面。默认播放保留 MMD Bullet 物理，截图从统一状态以 60 Hz 顺序推进。

庭院纹理与夜空 HDR 来自 Poly Haven CC0：来源、下载地址和 SHA256 记录在 `Resources/Garden/SOURCES.json`。这些是可选细节输入；缺失时仍能使用过程化底色、粗糙度和法线。舞台资产不包含测试角色模型。

## V2 通用法线细节

插件生成以下原创平铺资产，均位于 `/Ue5MMDTools/Stages/Textures/`，使用物体空间三向投影，不绑定模型 UV 或脸部阴影图：

| 材质参数 | 默认资产 | 用途 |
|---|---|---|
| `HairDetailNormal` | `T_MMD_GenericHairNormalV2` | 疏密变化的发束与低幅细丝法线 |
| `ClothDetailNormal` | `T_MMD_GenericClothNormalV2` | 连续折脊、宽谷面与轻微织纹 |
| `ClothDetailData` | `T_MMD_GenericClothDataV2` | R 粗糙度、G 小幅反射率调节、B 织纹光泽 |

`HairDetailStrength` 默认 0.20，`ClothDetailStrength` 默认 0.40；`HairDetailScale` 默认每厘米 0.040，`ClothDetailScale` 默认每厘米 0.010。布料漫反射使用过滤后的宽褶皱，镜面保留细节；直射高光和天空预过滤共用同一镜面法线与粗糙度。贴图只用于 Hair / Cloth，不给皮肤或眼球添加噪点。

细节强度为 0 时跳过对应细节采样，布料恢复几何法线与基础粗糙度；头发还需同时将 `HairShiftStrength` 设为 0，才完全恢复几何法线与基础 BRDF。法线不会改变模型轮廓或产生真实衣物形变。

V2 使用新资产名，生成器不会覆盖 V1 或已有同名纹理。父材质升级只将旧默认 `T_MMD_GenericHairNormal`、`T_MMD_GenericClothNormal`、`T_MMD_GenericClothData` 的精确链接迁移到 V2；自选贴图、实例覆盖与自定义强度保持原样。可在材质实例中替换三张通用图，法线图使用线性 BC5，数据图使用线性颜色采样。

## 可恢复的预设细描边

角色预设默认启用 `bCharacterOutline`。`OutlinePixelWidth` 是 1080p 参考宽度，默认 1.20，樱庭水镜为 1.25；应用时按透视距离与 FOV 换算，并继续乘 PMX `EdgeSize` 与顶点 `EdgeScale`。未启用 PMX 边线的材质槽保持无描边，PMX 边线透明度也会保留。

| 材质角色 | 颜色参数 | 宽度倍率 |
|---|---|---:|
| Skin | `SkinOutlineColor` | 0.65 |
| Hair | `HairOutlineColor` | 0.90 |
| Cloth | `ClothOutlineColor` | 1.00 |
| Metal | `OutlineColor` | 0.80 |
| Eye | `OutlineColor` | 0.45 |
| Generic | `OutlineColor` | 0.85 |

`OutlinePaletteBlend` 默认 1，使用预设部位配色；设为 0 保留原 PMX 边线色。细描边仅在透视视图生效，正交视图关闭，以免描边外壳进入正交自阴影采集。

`ApplyCharacterRenderPreset` 自动调用 `UMMDOutlineLibrary::ApplyMMDOutlinePreset(Component, Preset)`；也可在 Blueprint 单独应用。调整预设后可重新应用；C++ 可调用 `RefreshMMDOutlinePreset(Component)` 更新已经登记的预设，修改材质角色覆盖也会自动刷新。

`RestoreMMDOutline(Component)` 恢复首次应用前的全局与分槽 overlay、bounds scale；`RestoreCharacterMaterials` 会同时调用它。恢复只处理仍由预设持有的值，保留应用后另行更换的 overlay 或 bounds。原 PMX 世界空间描边仍可使用：先恢复预设描边，再调用 `UMMDOutlineLibrary::ApplyMMDOutline(Component, true)`。

## Blueprint 使用流程

推荐按以下顺序调用：

1. 获取关卡里的 `AMMDStageActor` 和角色 `USkeletalMeshComponent`。
2. 调用 `StageActor.ApplyStageRenderPreset(Preset)`。它会保存新预设，按需切换主题并重建舞台。
3. 调用 `StageActor.ApplyCharacterRenderPreset(Component, true)`。角色会登记到该舞台；以后再次切换舞台预设时会自动重应用角色参数。
4. 不再需要 Reference 外观时，调用 `StageActor.RestoreCharacterMaterials(Component)`。它会停止跟随舞台，并恢复首次应用预设前记录的材质覆盖。

`ApplyCharacterRenderPreset` 使用 Actor 当前的 `RenderPreset`，未设置有效预设时会失败。`bReplaceMaterials=true` 会换用预设中的 Reference 父材质；设为 `false` 时保留源材质作为 MID 父级，只写入它实际支持的同名参数。

不使用 `AMMDStageActor` 时，也可直接调用：

```cpp
UMMDStageLibrary::ApplyCharacterRenderPreset(Component, Preset, true);
UMMDStageLibrary::RestoreCharacterMaterials(Component);
```

恢复快照只对应应用时的同一个 SkeletalMesh。组件已经换网格时，恢复函数会清理由预设持有的覆盖，但不会把旧网格的材质写到新网格上。

## MaterialRole 分类与覆盖

每个材质槽会解析为以下角色之一：

| 值 | 角色 | 用途 |
|---:|---|---|
| 0 | Generic | 睫毛、绘制高光、口腔、头发阴影片和未识别材质 |
| 1 | Skin | 皮肤与脸部 |
| 2 | Hair | 头发 |
| 3 | Cloth | 衣物与布料 |
| 4 | Metal | 金属和配件 |
| 5 | Eye | 眼球、虹膜、瞳孔与白目 |

自动分类读取材质槽名、导入槽名和原材质名。睫毛、口腔和绘制高光先归入 `Generic`，其余按固定优先级匹配眼球、金属、头发阴影片、头发、皮肤和布料；无法识别时回退 `Generic`。

命名不可靠的模型应在应用预设前显式覆盖：

```cpp
UMMDStageLibrary::SetCharacterMaterialRoleOverride(Component, MaterialIndex, EMMDMaterialRole::Hair);
```

可用 `GetResolvedCharacterMaterialRole` 检查结果，用 `ClearCharacterMaterialRoleOverride` 清除单槽覆盖。覆盖属于当前组件与当前网格；如果该槽正在使用预设创建的 MID，修改角色会立即更新 `MaterialRole` 参数。

## PMX 必须重新导入

法线和透明度修复发生在导入阶段，旧 SkeletalMesh 与旧材质实例不会因切换预设自动获得修复。已有模型需要用当前插件版本从源 PMX 重新导入。

重导后，导入器会：

- 保留并清洗 PMX 顶点法线，关闭 UE 的法线、切线和加权法线重算；无效法线才回退到几何面法线。
- 按每个材质实际引用的三角形 UV 区域检查贴图 alpha，而不是用整张共享图集的透明像素比例判断材质。
- 为确实使用透明区域的材质切换到能消费 alpha 的父材质，并同步源材质 Opacity。

这里的 UV 只用于导入时判定材质是否需要透明混合，不参与角色阴影形状或 toon 明暗计算。

## `bUseMaskedHair`

`UMMDStageRenderPreset.bUseMaskedHair` 默认开启，用于优先保证头发深度写入和排序稳定。它只在以下条件同时满足时转换父材质：

- `bReplaceMaterials=true`；
- 源材质为 Translucent；
- 自动分类或覆盖结果为 `Hair`；
- 源基础色 alpha 与 `Opacity` 都接近 1。

转换后使用同方向的 `Masked` 父材质。头发阴影片被分类为 `Generic`，整体 Opacity 小于 1 的淡出材质也不会被转换。

发梢依赖柔和半透明、渐隐或半透明叠层时，应在预设中关闭 `bUseMaskedHair`。关闭后会保留软 alpha，但重新承担 Translucent 的深度写入限制、前后排序问题和更高的重叠像素成本。

## Face shader 边界

`TMMDAnimeFace.usf` 是用户维护文件，这套舞台与 Reference 材质不会修改或包含它。默认 Reference 角色材质使用通用几何光照：最少只需要 base color，脸部与其他皮肤一样由网格法线、主光方向和程序化 ramp 塑形。

默认方案不提供、也不依赖脸部 UV 阈值图、SDF 脸影或模型配套阴影贴图。启用 `bReplaceMaterials` 后，脸部材质会按原 BlendMode 进入相应 Reference 父材质；需要保留自定义父材质时可使用 `bReplaceMaterials=false`，或在完成预览后恢复原材质。

## 下一批工作

- 樱花冠层优先做手工 LOD：当前 1200 花簇约 518 万三角形；保留冠层位置，去背壳和厚边的中景版本可由 4320 降至 1080 三角形/簇。用同机位画面与 GPU 时间验证后再启用，避免以减少花簇数量破坏构图。
- 扩充可选通用细节库：布料粗糙度与微法线、皮肤微粗糙度、金属表面变化，继续使用几何空间投影和强度 0 兜底；局部场景反射需另设可编辑捕获方案。
- 用多种模型、角色旋转和镜头角度校准已有头骨几何基与过程化皮肤法线，保持缺少头骨时的通用兜底，不引入依赖 UV/SDF 的脸影。
- 把当前生成式舞台资产接入可编辑的创作模块，支持用户在编辑器中组合、保存和复用舞台内容。
- 用 GPU 测量划分质量档位，分别记录舞台实例、灯光、LightDataRT、阴影、Masked 与 Translucent overdraw 的成本，再决定移动端和桌面端默认值。
- 明确 Sequencer 与实时 MMD Bullet 物理的边界。舞台 `EvaluateStageAtTime` 可按绝对时间确定性评估，但实时物理是有历史状态的前向模拟；任意跳帧、反向拖动和重复定位目前不能视为确定性结果。需要稳定成片时应优先烘焙物理，或从统一初始状态顺序预滚。
