# Ue5MMDTools 并行卡通渲染任务书

> 修订日期：2026-08-25  
> 目标引擎：Unreal Engine 5.8（`E:\ae\UE_5.8`）  
> 基线提交：`c614c29 UpdateMMDShading`

## 1. 本轮目标

本轮不修复、不接管现有 toon 流程。当前 `MMDToonLighting.ush` 和 `TMMDShader` 下已有 shader 的回退状态，是用户为了停止较差视觉结果而主动保留的中间状态，后续由用户自行判断和维护。

本轮只做三件事：

1. 先建立一张清晰的二次元渲染目标参考图。
2. 恢复 UE 默认曝光观感，移除插件强加的固定 EV100 和物理光强标尺。
3. 新写一套完全并行的通用卡通 shader，供用户与现有方案对照，不替换现有父材质、不改变导入器默认选择。

## 2. 禁止修改范围

除非用户后续明确指定，以下现有 shader 不修改：

- `Shaders/Core/MMDToonLighting.ush`
- `Shaders/TMMDShader/MMDBaseToon.usf`
- `Shaders/TMMDShader/TMMDAnimeFace.usf`
- `Shaders/TMMDShader/TMMDAnimeSkin.usf`
- `Shaders/TMMDShader/TMMDAnimeHair.usf`
- `Shaders/TMMDShader/MMDAnimeEye.usf`
- `Shaders/TMMDShader/TMMDAnimeCloth.usf`

新方案不得通过 include、全局变量或材质输入改名间接破坏这些文件。

## 3. 视觉目标参考

当前目标图：

- `ShaderReferences/Target/AnimeToon_Target_v2_Glamorous.png`
- SHA-256：`D88C909BC85F4252DB9E34C26C2B9B156F95456C468DD1CFFC42D7B143BE9B9D`
- v1 保留为基础材质校准参考，不再作为最终华丽度目标。

该图不是要求逐像素复刻，而是用于锁定以下可实现特征：

- UE 默认曝光下中间调稳定，白衣不过曝、深色衣物不死黑。
- 两到三阶几何 toon 明暗，边界清楚但不锯齿。
- 阴影由 base color 压暗派生，青色头发在暗部仍保持青色色相。
- 脸和皮肤的明暗交界较柔；布料更硬；硬表面只有少量锐高光。
- 头发有宽而克制的方向性亮带，不呈金属或油塑料质感。
- 轮廓光只辅助剪影，不形成白色发光边。
- 场景投影可读，但不把材质环境底色整体乘黑。
- 层叠服装、金属扣件、晶体饰件、深浅布料和发束在同一曝光下保持清晰材质分区。
- 青色主色、品红边光与少量暖色高光形成舞台级色彩层次，但角色白色细节不过曝。
- 不使用 UV 阈值图、脸部 SDF 图或配套 mask。

## 4. 当前曝光/光强问题证据

当前源码存在多套非默认曝光和物理光强强制逻辑：

- `MMDViewPanel.cpp` 强制 `ExposureSettings.bFixed=true`、`FixedEV100=10`，并关闭 Eye Adaptation。
- `UMMDLightingEnvironmentLibrary.cpp` 多处强制 Manual Exposure、Physical Camera Exposure 和固定 EV100。
- 同文件通过 `GetPhysicalIntensity` 将艺术强度换算为 lux/lumen，并把局部灯设置为 `ELightUnits::Lumens`。
- `UMMDAnimeLightDataSubsystem.cpp` 明确按方向光 lux、局部灯 candela 传递真实强度。
- `DefaultEngine.ini` 额外覆盖 Auto/Local Exposure 项，其中存在重复键。

这些设置不适合本轮的艺术化 Unlit + 自算 toon：它们会把材质输出、预览光强和关卡曝光绑定到物理摄影标尺，使简单的 0～几倍艺术强度难以直接判断。

## 5. MCP + Skill 实施闭环

- Skill 流程层：固定参考图、禁止范围、输入契约、实现顺序与验收标准。
- 本地工具层：源码编辑、静态检查、UE Build Tool 编译和日志过滤。
- UE MCP 层：在 UE 5.8 中打开独立材质和 LookDev 地图、运行材质编译、调整实例参数、截取同机位对照图并读取 Output Log。

每个任务按 `基线截图 → 单项修改 → C++/shader 编译 → MCP 同机位截图 → 与目标图比较 → 用户判断` 执行。

## 6. 任务卡

### R00 生成视觉目标图（已完成）

输入参考：

- `参考/High_quality_MMD__MikuMikuDanc_2026-08-24T06-44-12.png`
- `ShaderReferences/Miku_HighSculpt_Refs/03_miku_face_hair_material.png`

交付：

- `ShaderReferences/Target/AnimeToon_Target_v1.png`：基础校准版。
- `ShaderReferences/Target/AnimeToon_Target_v2_Glamorous.png`：当前华丽细节目标版。

### R02 恢复 UE 默认曝光与艺术光强

目标：插件不再替用户固定 EV，不再要求物理摄影和 lux/lumen 标尺。

改动范围：

- `Private/UI/MMDViewPanel.cpp`
- `Private/Rendering/UMMDLightingEnvironmentLibrary.cpp`
- `Private/Rendering/UMMDAnimeLightDataSubsystem.cpp`
- `Config/DefaultEngine.ini`
- 必要时更新对应头文件注释和 LookDev 生成逻辑

实现要求：

- 删除预览视口的 `FixedEV100=10` 和 `SetEyeAdaptation(false)` 强制覆盖，让视口使用 UE 默认曝光。
- 生成/清理 LookDev PostProcessVolume 时不覆盖 Auto Exposure Method、Bias、Physical Camera Exposure。
- 删除 `GetPhysicalIntensity` 的千倍物理换算；灯光规格中的 `Intensity` 直接作为艺术强度使用。
- 插件创建的局部灯使用 UE 非物理/Unitless 强度；LightDataRT 传递同一艺术强度，不再出现 lux/candela 注释和 `/PI` 归一化假设。
- 删除插件额外添加且非必要的 Auto/Local Exposure 配置覆盖；保留 UE 5.8 项目默认值。
- 不修改现有 toon shader；即使它们暂时仍按旧强度解释，也由用户后续自行处理。

验收：

- 新建默认 UE 关卡与插件预览窗口的曝光行为一致。
- 插件不创建固定 EV 或 Manual Exposure 覆盖。
- `Intensity=1` 是直观艺术基准，不发生 1000/5000 倍换算。
- UE 5.8 C++ 编译通过，LookDev 地图打开无错误。

### R01 新建独立 Reference Toon 核心与入口（已完成）

新增文件建议：

- `Shaders/Experimental/MMDReferenceToonGlamorousLighting.ush`
- `Shaders/TMMDShader/MMDReferenceToonGlamorous.usf`

类型建议：

- `TMMDReferenceSurface`
- `TMMDReferenceLight`
- `TMMDReferenceToon`

设计原则：

- 完全独立，不 include 或修改现有 `MMDToonLighting.ush`。
- 最小视觉输入只有 `base_color`；灯光 RT、场景阴影 RT 均有安全回退。
- 只使用几何法线、世界位置、视线、灯光方向、可选几何切线与场景阴影。
- 不使用固定 UV 位置、SDF 阴影图、toon 阈值图或额外 mask。
- 光照是艺术化归一值，不使用 lux/lumen、`C.w / PI` 或物理 BRDF 能量守恒。
- 输出围绕 UE 默认曝光设计，避免通过高 emissive 数值硬顶亮度。

第一版视觉组成：

1. 中性环境底色：保证无灯时仍能读出 base color。
2. 主灯两/三阶 diffuse：`n_dot_l` 决定明暗位置，灯强只控制亮度与效果权重。
3. base color 派生阴影：保留原色相，不提供配套阴影贴图入口。
4. 场景阴影：只压直接光和必要高光，不把环境底色、自发光整体乘黑。
5. 几何高光：`n_dot_h` 阶梯化；默认克制，可通过参数关闭。
6. 几何 rim：`1-n_dot_v`，限制亮度和宽度，避免白边发光。
7. 可选头发方向亮带：仅使用几何切线；切线无效时回退普通高光。

建议输入：

- `base_color`：float3
- `light_data_tex`：Texture2D，可选实际数据
- `shadow_threshold`：float，默认 0.0（`n_dot_l` 的 [-1,1] 域）
- `shadow_softness`：float，默认 0.08
- `shadow_strength`：float，默认 0.48
- `midtone_offset`：float，默认 0.18
- `specular_threshold`：float，默认 0.92
- `specular_strength`：float，默认 0.12
- `rim_power`：float，默认 3.0
- `rim_strength`：float，默认 0.08
- `mmd_shadow_map`：Texture2D，可选
- `mmd_shadow_bias`：float，默认 0

验收：

- Custom 节点只使用 `struct + 成员函数`，无独立函数、宏或引擎文件修改。
- 无 LightDataRT/ShadowRT 时不黑屏、不报错；有灯时方向变化能移动明暗边界。
- 白、黑、红、青四种 base color 都保持色相和可读层次。
- 新 shader 编译问题不影响任何现有父材质。

完成记录（2026-08-25）：

- 新增完全独立的 `TMMDReferenceToonLight` 与 `TMMDReferenceToon`，没有 include 或修改现有 toon 核心。
- 使用艺术强度压缩、三阶几何 diffuse、base color 派生阴影、SH 环境、阶梯高光、几何 rim、可选切线亮带和级联场景阴影。
- UE 5.8 `MaterialEditingLibrary.recompile_material` 返回空错误列表，HLSL 编译通过。

### R03 新建独立测试父材质（已完成）

建议新增资产：`M_MMD_ReferenceToon` 和 `MI_MMD_ReferenceToon_Test`。

要求：

- 不修改 `M_MMD_Base_*`、Face、Hair、Eye 等现有资产。
- 不把新材质设为 PMX 导入默认值。
- 提供单独入口或手动替换方式，让用户逐材质测试。
- 所有输入有默认连接；第二次校验运行保持幂等。

完成记录（2026-08-25）：

- 已新增 `/Ue5MMDTools/Resources/MaterialInstance/M_MMD_ReferenceToon`，没有替换导入器默认材质。
- 父材质含 22 个表达式、17 个 Custom 输入和 17 个可调参数；创建脚本遇到同名资产会直接退出，不覆盖用户调整。
- 父材质使用 Masked + TwoSided，并以 `BaseColorMap Alpha * opacity_strength` 驱动裁切；默认 `opacity_strength=1`，不要求额外透明贴图。
- UE 5.8 独立验证脚本确认 Custom 入口、输入、参数、Emissive 连线与 HLSL 编译均通过。
- 已新增 `/Game/MMDReferenceToon/MI_ReferenceToon_LookDev`；另有 49 个模型材质验证实例，仅复制原材质的 `BaseColorMap` 与 `DiffuseColor`，没有修改原材质。

### R04 UE 5.8 MCP LookDev 对照调优（首版拒收，重做中）

测试矩阵：

- 至少两个 UV/贴图布局不同的 PMX 模型。
- 白布、深色布、皮肤、青色头发、硬表面配件。
- 正面光、侧光、背光、无灯和带场景投影五种状态。

MCP 证据：

- 同一相机、同一背景、同一 UE 默认曝光下保存现有材质与 Reference Toon 对照截图。
- 保存材质编译结果、关键实例参数和 Output Log 摘要。
- 只按目标图逐项调阴影强度、边界宽度、高光和 rim，不用曝光补偿掩盖 shader 问题。

最终由用户决定：保留实验材质、继续迭代，或人工移植其中某段逻辑。没有用户确认，不替换现有 shader。

首版拒收记录（2026-08-25）：

- UE 5.8 MCP 在 `/Ue5MMDTools/Maps/LE_NeonNight` 中完成同机位基线、角色覆盖、PIE 和标准球截图。
- 截图归档在 `Saved/MMDReferenceToonValidation/`；`00` 是旧材质基线，`01`～`08` 是角色迭代，`09`～`10` 是标准球材质本体验证。
- `14_neon_default_exposure_failed_shader.png` 为拒收证据：曝光恢复后，新材质仍存在大面积灰死、主次明暗缺失、彩光只停留在零碎轮廓、高光和材质分区不足等问题。
- 标准球能看到分段不构成角色视觉验收；此前以标准球结论代替完整角色结论是错误的验收方式。
- 首版 Reference Toon 标记为失败原型，不再继续只调阈值、高光和 Rim 参数；重做多灯大面着色后重新开始完整角色截图闭环。
- 裙摆法线近似一致会限制局部层次，但不能作为角色大面积灰平的主要解释或免责理由。
- 测试角色的 Eye/Face/Hair 旧父材质在 UE 5.8 启动日志中已有编译失败，且多个表情层共用整张身体贴图；面部黑带暂不作为 Reference Toon 的视觉结论。
- MCP `LogShaderCompilers` 中没有 `M_MMD_ReferenceToon`、`MMDReferenceToonGlamorousLighting` 或 `MMDReferenceToonGlamorous.usf` 错误。
- 已补充第二个 UV/贴图布局不同的 PMX 角色，但其重导资产本身存在脸部 UV/材质错位，不能作为着色器视觉通过样例；详见 R05 记录。

Revision 16 迭代记录（2026-08-25，仍未通过）：

- UE 当前进程会缓存已加载的 `.usf/.ush` 内容；仅改原路径并重编译 Custom 节点不能可靠刷新。验证过程改用全新入口路径，`44_fresh_top_level_v2.png` 首次证明新彩色分层真实生效。
- `44` 的青粉大面积染色过强；`45`～`46` 收敛为暖白亮面、蓝紫阴影和饱和细边光，但完整角色的面部与材质细节仍未达到目标图品质。
- 重载关卡清除了旧角色幽灵代理；随后把两盏点光与聚光统一为 `Unitless` 艺术强度，截图不再依赖物理曝光补偿。
- `47` 为重载后物理灯光过曝的作废证据；`48` 起使用默认曝光与 Unitless 灯光；`50`、`59`、`60` 仍因正脸偏暗、裙面细节不足而拒收。
- MCP 读取确认 SkeletalMeshComponent 的 49 个 `OverrideMaterials`：验证实例已覆盖全部槽位；补建 01、02、28 三个实例，不修改源材质。
- `body_png_asset.png` 与 `eye_png_asset.png` 证明源面部/眼睛贴图内容正常；面部问题属于当前材质/模型渲染链，不能归咎于贴图损坏。
- 修复验证脚本参数更新顺序：scalar 预设写入后再调用 `update_material_instance`。当前版本只作为继续迭代基线，不标记视觉合格。

### R05 干净重导与 Universal Anime Toon 重建（进行中，未获用户验收）

本轮不再沿用失败的 Reference Toon 原型，新增完全独立的通用 Opaque/Translucent 父材质组合：

- `/Ue5MMDTools/Resources/MaterialInstance/M_MMD_UniversalAnimeToon`
- `/Ue5MMDTools/Resources/MaterialInstance/M_MMD_UniversalAnimeToon_Translucent`
- `Shaders/TMMDShader/MMDUniversalAnimeToonR03.usf`
- `Shaders/TMMDShader/MMDUniversalAnimeToonR05.usf`

实现与约束：

- 未修改现有 `MMDBaseToon`、Face、Skin、Hair、Eye、Cloth 或 `MMDToonLighting.ush`。
- 最小颜色输入仍是一张 `BaseColorMap`；明暗、高光、终止线和 Rim 全部来自几何法线、位置、视线及 LightDataRT，不使用脸部 SDF、UV 阈值图或配套 mask。
- 主 Opaque 材质保证贴图 Alpha 不可靠时模型仍完整可见；Translucent 配套材质只处理原始材质本来就是透明/叠加层的槽位。
- 验证脚本始终从 SkeletalMesh 资产的原始材质槽读取贴图、颜色和混合模式，避免第二次运行读取组件 OverrideMaterials 后污染迁移判断。
- PMX `DiffuseColor.rgb=(0,0,0)` 被视为缺失 Tint 并回退白色；非零 Tint 仍正常参与乘色。

干净重导证据：

- 猫女仆：`YYB 猫猫女仆.pmx`，重建网格 `62,230` 顶点、`49` 个 section/材质槽；49 个新实例输出到 `/Game/MMDToonRebuild/CatMaid/Materials`，源材质未改。
- Miku 16th：`YYB Hatsune Miku_16th.pmx`，重建网格 `40,979` 顶点、`39` 个 section/材质槽；39 个验证实例输出到 `/Game/MMDToonRebuild/Miku16th/Materials`。
- 猫女仆当前可用截图：`121_catmaid_reimport_r05_clean_full.png`、`122_catmaid_reimport_r05_clean_face.png`、`123_catmaid_reimport_r05_clean_threequarter.png`。
- 第二模型截图 `114_second_original_routing_face.png` 仍有黑脸；`105_second_original_mesh.png` 证明源网格缩略图也存在同样问题，`115_second_face_texture.png` 与 `102_second_body_texture.png` 证明贴图文件并未损坏。
- 诊断确认脸部正在采到 `body.png` 的黑色丝袜渐变区；提高 `ambient_strength` 只能提亮外围，不能改变中心黑纹理。因此该问题单列为 PMX 重导 UV/材质兼容缺陷，不作为 Universal Toon 视觉结论。

当前判断：R05 已排除此前的黑屏、霓虹错色、全槽 Opaque 覆盖透明层和零 Diffuse Tint 四类致命错误；猫女仆的颜色、脸部、黑白布料和裙面高光已经稳定。但整体华丽度、头发方向性亮带、轮廓表现和最终舞台级层次仍未达到目标图，必须继续用完整角色截图迭代，不能标记为视觉合格。

### R12 点光恢复与华丽局部光（技术通过，视觉继续迭代）

本轮先修复“场景里有点光但 Universal Toon 完全不响应”的底层问题，再把局部光从宽泛补光改成华丽辅助光：

- 根因不是点光 `NdotL` 公式，而是 `FMMDAnimeWriteLightsCS` 的参数名契约不一致。当前加载 DLL 绑定 `LightData` / `OutputTexture`，源码和 `.usf` 一度改成小写，导致启动日志出现 unbound parameters，LightDataRT 实际保持全零。
- `MMDAnimeWriteLights.usf`、`MMDAnimeWriteLightsCS.h` 与 `UMMDAnimeLightDataSubsystem.cpp` 已统一回 DLL 兼容的 `LightData` / `OutputTexture`。`135_r10_debug_after_rt_fix.png` 直接显示点光颜色与距离衰减，证明 RT 写入链恢复。
- R12 保持稳定中性主 Toon；只有真正的平行光才接管主阴影。点光、聚光和面光单独累加，不能再既充当主光又重复进入局部和。
- 局部光拆为低强度色相漫反射、双层几何高光和高饱和逆光 Rim；所有项仅依赖世界位置、法线、视线与 LightDataRT，不使用 UV 阈值、脸部 SDF 或配套 mask。
- 猫女仆验证角色位于 `X=300`，旧点光仍在 `X=-120/120`，其中一盏接近衰减半径外。MCP 验证时把粉/青点光对称放到角色后侧 `X=220/380, Y=-45, Z=165`，保留 Unitless 强度和 UE 默认曝光。
- 同机位关灯证据 `149_r12_glam_clean_point_off_front.png`；开灯正面与三分之四证据为 `147_r12_glam_clean_front.png`、`148_r12_glam_clean_threequarter.png`。开灯后黑裙、肩饰和发丝边缘出现粉/青分色，脸部固有色基本保持。
- 现有 BaseToon、Face、Skin、Hair、Eye、Cloth 材质均未改；只更新独立 Universal Anime Toon 父材质与验证实例。

当前判断：点光数据通路和材质响应已经技术通过，R09 强参数造成的全身青白漂色已被拒收并修正。R12 的双色边缘比 R05 更接近华丽参考，但头发各向异性亮带、运行期描边、材质类别差异和舞台级背景层次仍不足，因此不标记为最终视觉验收通过。

R12～R14 视觉复核修正：用户指出结果油、丑且远离参考后重新复核，确认三版均不合格。旧 v2 参考同时改变服装几何、姿势、镜头与舞台，不能作为单一材质的直接验收图。新增 `AnimeToon_Target_v3_CatMaidShaderOnly.png`，严格锁定当前猫女仆模型与构图，只定义 shader 可实现的色阶、发丝亮带和双色细 Rim；后续以同模型同机位对照，不再用泛光或大面积高光伪造华丽感。

R15 对照结果：`156_r15_reference_aligned_front_on.png`、`157_r15_reference_aligned_threequarter_on.png` 与关灯图 `158_r15_reference_aligned_front_off.png`。R15 已移除点光漫反射和面状点光高光，黑裙不再被整体抬灰，皮肤油感下降；但双色细 Rim 和头发切线双亮带仍明显弱于 v3 参考，白布、黑缎、皮肤、头发共用同一响应也限制了材质分离。R15 继续标记为视觉未通过。下一版应增加有默认兜底的可选 `surface_profile`，只改变几何过程化响应，不引入 UV mask 或配套贴图。

R25～R34 迭代记录（2026-08-27，继续拒收）：

- 新实现已收敛为 `MMDUniversalAnimeToonImplementationR25.usf` + R31/R32 分层实现 + `MMDUniversalAnimeToonR34.usf` 入口；只更新两个独立 Universal 父材质，没有改现有材质或 `TMMDAnimeFace.usf`。
- `surface_profile` 已覆盖皮肤、头发、白布、黑布四类；材质仍只要求 base color，档位只改变由 `n_dot_l`、`n_dot_v`、灯光方向和位置计算的过程化响应。
- 修正验证实例的语义分类优先级：黑裙 `スカート`、黑蝴蝶结 `リボン` 不再因共用贴图名含 `white` 被误判成白布；49 槽位重新生成并挂载。
- R30.1～R34 的 MCP 固定机位证据为 `226`～`253`；其中 `253_r34_targetframe.png` 是与 v3 参考最接近的腰部构图。结构校验持续通过：22 expressions、18 Custom inputs、17 parameters、Translucent pass。
- R34 已恢复青/品红点光对表面与轮廓的影响，头发色相更浓，白袖出现冷灰侧影；但脸部仍偏灰白、白布大面偏平、主裙的目标宽灰面仍没有可靠命中，可见材质槽与语义名还需实机定位。
- 当前版本不标记视觉合格。下一步先做临时 profile 可视化诊断，精确定位主裙、胸衣、蝴蝶结的可见槽位，再分别设定黑布响应；禁止继续通过抬高全局高光、曝光或 Emissive 硬凑华丽度。

R35～R38 面部专项记录（2026-08-27，明显改善但未完全对齐）：

- 输入审计确认脸部主体、身体和表情层共用 `body_png`；旧 skin profile 用常量桃色覆盖大部分 base color，是五官与肤色层次被洗平的直接原因。
- R35 改为保留原脸纹理的暖肤三阶光照，并新增 `surface_profile=5` 的通用 Eye 档位；`eyeball`、`eyes`、`HL` 已重新分类，虹膜、眼白和高光层不再走普通材质响应。
- R36～R38 依次增加 `n_dot_v` 正面暖桃塑形、侧面收暗、下向法线红润、相机右向几何淡腮红与 `n_dot_l × n_dot_v` 宽柔光；全部为过程化几何计算，无脸部 SDF、UV 阈值或配套遮罩。
- R38 把虹膜从深青高饱和收敛到更亮的浅蓝，并保留原贴图高光。固定机位证据为 `269_r38_front.png`、`271_r38_face.png`、`272_r38_bust.png`、`273_r38_targetframe.png`。
- UE 5.8 校验继续通过：22 expressions、18 Custom inputs、17 parameters、Translucent pass。当前脸部已恢复眉线、嘴唇和虹膜细节，肤色更暖且没有油亮尖峰；与目标图相比，发帘投影式的上脸阴影和更强的双颊色差仍不足，因此暂不标记最终视觉合格。

R39～R44 发影与暗场亮度记录（2026-08-27，继续迭代）：

- 隐藏槽位 43 的 MCP 对照确认额头红色斜线来自原材质 `Hairshadow`，不是头发本色；恢复材质后把 `hairshadow` / `hair_shadow` / `髪影` / `发影` 优先分类为独立 `surface_profile=6`。
- 发影档位只使用原 base color 的亮度派生低饱和暗酒红，不依赖脸部 UV、SDF 或阴影贴图；49 槽重新应用后 `Hairshadow` 稳定命中 profile 6，底层真实发束仍保留。
- 关卡灯光审计确认当前没有天空光或平行光，只有强度 4.0、4.5、4.0 的两盏点光和一盏聚光；因此降低固定环境底色、局部填充和宽边光，避免 Unlit 自算光照角色脱离黑场亮度。
- R41～R43 因当前 UE 进程的嵌套 include 缓存只产生参数级变化，图像差分证明 R42→R43 几乎未真实更新；不把这几版计为有效视觉结果。
- R44 新增全新顶层 struct 与入口路径，在最终输出统一执行由 `ambient_strength` 驱动的场景曝光收敛，并按皮肤、头发、白布、黑布和发影分别保持可读性。相对 R40，同一角色 ROI 平均亮度下降约 22%，白布不再像自发光，粉青轮廓仍可见。
- 固定机位证据为 `295_r44_front.png`、`296_r44_face.png`、`297_r44_bust.png`、`298_r44_scene.png`；UE 5.8 结构校验继续通过：22 expressions、18 Custom inputs、17 parameters、Translucent pass。
- 本轮仍只更新独立 Universal Anime Toon 父材质、验证实例和新 shader；未修改用户现有材质或 `TMMDAnimeFace.usf`。R44 作为新的暗场基线继续迭代，暂不标记最终视觉验收通过。

R45～R51 华丽暗场收敛记录（2026-08-27～2026-08-28，技术通过、视觉继续迭代）：

- R45～R46 把发影从固定酒红改成冷暗低饱和层，头发顶部高光回染原发色，并为黑色材质增加正面宽灰体积；所有颜色仍只从 base color、原 DiffuseColor 与几何光照派生。
- R47～R50 从真实局部灯位置计算窄青/品红轮廓，逐步拆分皮肤柔和正面层、绿色发面、白布补光和黑缎灰阶；没有增加脸部 SDF、UV 阈值、mask 或配套贴图。
- R49 把皮肤重建层提高到约 86%，固定近景中脸部硬侧光明显减弱，原贴图眉眼与嘴部仍保留；头发大面从灰绿恢复为更明确的浓绿，没有重新出现大面积油亮高光。
- 槽位复核确认主裙 `スカート` 为 `surface_profile=4`。R51 针对双面裙摆反向法线使用 `abs(dot(N,V))` 恢复宽灰体积，不改已经稳定的脸部与发色响应。
- 固定机位证据为 R47 的 `307`～`310`、R48 的 `311`～`314`、R49 的 `315`～`318`、R50 的 `319`～`322`、R51 的 `323`～`326`。R51 最终结构验证通过：22 expressions、18 Custom inputs、17 parameters、Translucent pass；49 个验证槽重新挂载，`Hairshadow` 保持 profile 6。
- 默认 UE 曝光下，R51 的青粉轮廓、脸部和发色已比 R44 更接近 v3 参考，但黑裙宽灰面仍偏暗，整体华丽度仍未达到目标图。下一轮应直接校准黑缎输出级与局部 Rim 能量，不应提高全局曝光或恢复宽面油亮高光。
- 本轮只更新独立 Universal Anime Toon 父材质、验证实例和 R45～R51 新 shader；用户现有材质、`TMMDAnimeFace.usf`、引擎 shader 与曝光设置均未修改。R51 作为当前已编译并经 MCP 验证的迭代基线，继续标记为视觉未最终通过。

R52～R56 多环境转正候选记录（2026-08-28）：

- 使用 ImageGen 以同一猫女仆、同一服装、姿势和镜头为锚点，新增三张只改变光照与材质响应的 v4 参考：`AnimeToon_Target_v4_NeonHero.png`、`AnimeToon_Target_v4_WarmStage.png`、`AnimeToon_Target_v4_BrightNeutral.png`。三张图分别约束暗场高能青粉窄 Rim、暖金舞台颜色守恒、明亮环境黑白材质分离。
- 旧 8 个 LookDev 关卡含不同角色和历史手动曝光，不能直接用于结论。新增 `Saved/mcp_capture_universal_multi_env.py`：固定 `MMD_Reimport_CleanValidation` 和 UE 默认曝光关卡，只瞬时切换霓虹、暖光、中性三套 LightData；每套输出全身和脸部两张图，最后重新加载关卡丢弃测试灯光，不保存环境改动。
- R52 首次统一双面法线朝向 `N *= sign(dot(N,V))`，让反向法线裙摆也能得到稳定主光二段；同时重建皮肤、白布、头发与黑缎的纹理保真正面层。
- R53～R55 把局部灯 Rim 从主光逻辑拆出，按真实局部灯方向、距离衰减、光色、逆光夹角与几何剪影独立积分；R55 根据 MMD 实际顶点法线把 Rim 阈值从理论剪影放宽到 `0.28～0.78`，避免只剩一像素细线。
- R54 之后三环境近景确认脸部均保持哑光，暗场无油斑、暖场不过橙、明场不发灰；绿色头发在暖光下仍守住固有色，白布不被霓虹染满。
- R56 只提高黑缎观察面输出级与既有宽珠光 Rim，不再改脸和发色。最终证据为 `Saved/MMDReferenceToonValidation/MultiEnv/r56_neon*.png`、`r56_warm*.png`、`r56_neutral*.png`，共 6 张；49 槽重新挂载，结构验证通过：22 expressions、18 Custom inputs、17 parameters、Translucent pass。
- R56 可作为转正候选：跨三种环境的颜色、亮度与材质类别响应稳定，华丽轮廓明显强于 R51，同时未恢复宽面油亮高光。概念图仍代表艺术上限，黑裙中心灰阶和最终运行期描边可继续独立优化，但不再阻塞这套 Universal Toon 进入候选主流程。
- 本轮没有修改用户现有材质、`TMMDAnimeFace.usf`、引擎 shader、曝光设置或物理光照流程；新增参考图和瞬时灯光验证脚本均保存在项目内。

## 7. 执行顺序

```text
R00 目标参考图（完成）
  └─ R01 独立 Reference Toon shader（完成）
       └─ R03 独立测试父材质（完成）
            └─ R02 UE 默认曝光 + 艺术光强（源码已改，待 C++ 编译）
                 └─ R04 UE 5.8 MCP 完整角色重做与对照调优（进行中）
```

R04 只接受完整角色同机位截图。标准球只用于排查数学错误，不再作为视觉通过证据。R02 已移除插件固定 EV/Manual Exposure 与物理光强换算；由于本机缺少 VS2022 x64 工具链，源码尚未完成 C++ 编译，当前 MCP 截图使用关卡内等价的默认曝光与 Unitless 艺术光强进行验证。

## 8. 每步交付

每完成一张任务卡，交付：

- 修改文件与未触碰文件。
- UE 5.8 C++/shader 编译结果。
- MCP 同机位截图或明确说明尚未进行实机检查。
- 与目标图的差距，只讨论 shader/材质，不用 EV 或曝光补偿遮盖问题。
- 下一步建议，等待用户判断后再继续。
