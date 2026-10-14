# 终末地渲染 V2 交接记录

历史记录日期：2026-10-13（Claude 中断）；最新实施记录：2026-10-14。接手从 [ENDFIELD_V2_FOR_GPT.md](ENDFIELD_V2_FOR_GPT.md) 的最新节开始。

**后续更新：GPT 已完成灰模修复、原生 V2 十资产生成、回读与运行时/回退验证。现在从 [ENDFIELD_V2_FOR_GPT.md 的最新状态](ENDFIELD_V2_FOR_GPT.md) 继续，以下为 Claude 中断时的历史记录。用户随后已允许多个子代理。旧工作区副产物没有被本批还原，不得按下方历史建议笼统 checkout 未提交资产。画质仍未通过，当前采用 UnifiedDiffuse=0。**

2026-10-14 已完成插件原生着色后端的实际编译、原生阴影／GI 对照和 Cook／打包游戏验证。安装引擎源文件不改，精确 UE5.8.3 私有 shader 入口在工程 Intermediate 副本中适配，`.ush` 替换自有 Profile 的逐灯光响应，并接最终间接光／反射贡献。Editor 共77图／490槽检查，最终五种打包游戏对照均 exit0；首次 game0 的一次堆异常仍未定位，保留失败。2,378项源资产／配置／引擎 shader 哈希保持，临时13包已移到 Saved。机制与限制见 [ENDFIELD_NATIVE_BACKEND.md](ENDFIELD_NATIVE_BACKEND.md)。这批三档响应是基础设施验证；SDF 侧向错误、正式 face／spec payload、真实 ramp 与完整十项画质范围继续保留，不再重复灰模排查或把旧并行限制当现行指令。

## 1. 为什么重做

- 用户验收 `Screenshots/2026-10-13-endfield-preset/*.png`（Unlit + Custom 节点自算光照版）：**"好丑，感觉是纯 unlit，之前的方案是错的"**。
- 根因已查实：Unlit 材质不进引擎延迟光照，Lumen GI、VSM 阴影接收、AO、反射都不会作用到角色。这是架构问题，调参数救不回来。
- 该 Unlit 版本身已真实编译、SM6 通过、预设已入库，作为**非 Substrate 工程的回退**保留，不要删。

## 2. 新方向（待验证，尚无结论）

UE 5.8 引擎原生 **Substrate Toon BSDF + UToonProfile**，不改引擎：

- 节点 `UMaterialExpressionSubstrateToonBSDF`（Experimental，`MaterialExpressionSubstrate.h:860`）。输入：BaseColor、Metallic、Specular、Roughness、Normal、EmissiveColor、PatternUVs、Anisotropy、Tangent；属性 `ToonProfile`。
- `UToonProfile`（`Engine/Classes/Engine/ToonProfile.h`）：`DiffuseRamp`（颜色曲线，输入为 NoL*0.5+0.5，可含阴影）、`DiffuseRampOffsetTexture/Strength/Size`（偏移纹理，文档建议距离场，是引擎原生的 SDF 入口，靠 PatternUVs 采样）、`SpecularRamp`、阴影网纹、`DiffuseIndirectScale`、`SpecularIndirectScale/Ramp`、`bDiffuseRampIncludeShadow`。
- shader：`Shaders/Private/Substrate/SubstrateToonBSDF.ush`、`Shaders/Private/ToonProfileCommon.ush`。
- CVar 约束：`r.Substrate` 只读、默认 0；`r.Substrate.Experimental.ToonUnifiedDiffuse` 与 `...ToonReflectionQuantizationEnabled` 均只读、默认 0；`r.Substrate.ProjectGBufferFormat` 默认 1（新工程模板为 0）。都只能启动时经 ini 设置。
- `UMaterialInstance` 有 `ToonProfileOverride` / `bOverrideToonProfile`，所以"一个父材质 + 按角色覆盖不同 ramp"在引擎层面可能通；**MID 运行时是否生效未验证**。
- 插件运行时只按混合模式选父材质（`Source/Ue5MMDTools/Private/Stage/MMDStageLibrary.cpp:299 ApplyCharacterRenderPreset`），角色靠 `MaterialRole` 标量切。按角色换 ramp 需要改这里。
- 取舍：要求用户工程开 `r.Substrate=True`（重启 + 全量重编）。**不要改仓库 `MMD/Config/DefaultEngine.ini`**，探针用命令行覆盖；方案成立后由用户决定是否正式开。
- 待验证：Lumen/VSM/AO 在 SceneCapture2D 路径下是否真作用到角色；Translucent 是否支持 Toon BSDF（预期不支持，要退路）；SkeletalMesh/变形目标/遮罩发片/双面限制；各向异性高光能否做出天使环；球面法线 + 偏移纹理做脸部阴影；冷启动开销。

## 3. 逆向（对齐真实客户端）

客户端与工具位置见记忆 `endfield-local-client-reference`（真实路径 `E:\Games\Installers\Hypergryph Launcher\games\Arknights Endfield`）。

已产出，位于 `MMD/Saved/Phase2Reference/Endfield2/`（Saved 不入库）：

| 路径 | 内容 | 状态 |
|---|---|---|
| `material-values/material_values.json`（7 MB，schema `mmd.endfield.material-values.v2`） | shaders / materials / zhuangfy / role_summary / character_summary / outline_summary / shadow_color_eval / texture_inventory | **有数据，未写 MD 摘要，结论未人工复核** |
| `material-values/ramps/*.json`（67 张） | 真实 ramp 贴图像素（256×1 的高光 ramp 等）：rows、luma/chroma/alpha 曲线、used_by | 同上 |
| `mv_work/ramp_summary.json`、`mv_work/shadow_eval.json` | ramp 汇总；300 条"亮部→阴影色"评估（含色相偏移、明度比） | 同上 |
| `global-lighting-post/scripts/` | `glp_*.py` 与 C# `GlpDump`（typetree 读取已通，已读到管线资产） | **无结论文档**，后处理/打光/描边数值未取得 |
| `shader-equation/` | — | **无产出** |
| `toon-probe/` | 仅 `_logs/build*.json` 与空的 `ddc-*` | 探针**没有出过图** |

仍缺：每路光照方程（字节码）、后处理（tonemap / bloom / grading / 曝光）、角色打光 rig、描边规则、ramp 行号语义。

上一轮（2026-10-11）遗留的 `StaticProbe/`（Hair/Cloth DXBC asm、属性表）仍可用；`*.reconstructed.hlsl` 重编译未通过，**以 `.asm.txt` 为准**。上一轮只取了 shader 属性默认值，真实材质覆盖值现在才开始取。

## 4. 考虑项清单（全部过完、自己看图后才给用户截图）

| # | 考虑项 | 落实方式 |
|---|---|---|
| 1 | 光照接入 | 引擎延迟光照（GI、阴影、AO、反射）真的作用到角色 |
| 2 | 明暗结构 | 多段 ramp + 暗部色相偏移，数值取自真实 ramp |
| 3 | 阴影 | 自阴影与场景投影并入 ramp；多灯统一漫反射 |
| 4 | 脸部 | 球面法线 + 偏移纹理/自动烘焙 SDF；无灰块；鼻影与眼窝 |
| 5 | 头发 | 各向异性高光环；内部暗体积；刘海投影 |
| 6 | 材质分路 | 皮肤/布料/金属/眼球各自 ramp、高光、粗糙度，取真实值 |
| 7 | 边缘光与描边 | 深度边缘光；描边色随底色；参数取真实值 |
| 8 | 后处理与融合 | 曝光/tonemap/bloom/grading 对齐真实配置 |
| 9 | 通用性 | 只有一张 base_color 也完整；SDF/ramp 贴图可选 |
| 10 | 回退与分发 | 非 Substrate 工程回退；半透明发片退路 |

## 5. 接着做的顺序（省额度）

1. **只跑探针一个实验**，别再四路并行。命令（后台，无前台）：
   `UnrealEditor-Cmd.exe D:/projectGithub/MMDUETools/MMD/MMD.uproject -run=MMDEndfieldToonProbe -OutDir=<目录> -Tag=p1 -Exp=ab -AllowCommandletRendering -RenderOffscreen -d3d12 -unattended -nopause -nosplash -nosound -NoCrashDialog -stdout -UTF8Output -DisablePlugins=RenderDocPlugin -ddc=InstalledNoZenLocalFallback -LocalDataCachePath=<DDC> -abslog=<日志> -ini:Engine:[/Script/Engine.RendererSettings]:r.Substrate=True`
   先**读 `MMDEndfieldToonProbeCommandlet.cpp`（1621 行，agent 写的，未经我复核）**确认实验开关含义。冷 DDC 编 shader 要十几分钟，属正常。
2. 可行 → 写正式生成器（创建 `UToonProfile` 资产、Toon BSDF 材质、按角色覆盖 profile）；不可行 → 评估"Lit + 后处理 toon 重映射"。
3. 逆向按需补：先复核 `role_summary`、`outline_summary`、`shadow_color_eval`；再取后处理；光照方程最后。
4. 看完图、清单过完，再给用户截图。

## 6. 工作区现状（未提交、未推送、未动分支）

- 已修改源码 8 个：SDF 烘焙器（`uint8`、`Animation/Skeleton.h`）、`MMDEndfieldPresetsCommandlet.cpp`（`base_color` 改 `TextureSampleParameter2D`、opacity 取 alpha、签名加 `EndfieldMaterialGraphVersion`）、五个 unity build 撞名前缀修复（Garden/Outline/StageMaterialBuilder/Phase2/StagePresets）、`MMDStagePresetsCommandlet.cpp` 的 `-EndfieldCapture`。
- 新增未跟踪：`MMDEndfieldToonProbeCommandlet.cpp/.h`（探针，已编译通过，无结果）；`Content/Stages/Materials/Endfield/*` 与 `Content/Stages/Presets/Endfield/DA_Render_Endfield`（Unlit 版）；`Docs/Screenshots/2026-10-13-endfield-preset/`（12 张 Unlit 版，画质已被用户否决，不要当成果归档）。
- 27 个已跟踪 `Content/` 资产被旧生成器重写（当时记录为副产物）：保留现状；后续清理须逐文件核对并保存已有改动，不能直接笼统 `git checkout --`。
- `CHANGELOG.md` 有 agent 追加的 8 行（"首次出图"），提交前复核措辞。`ENDFIELD_RENDERING.md` 已更新"尚未完成"一节。
- 已清理：中断后残留的探针 `UnrealEditor-Cmd.exe` 进程树已结束。

## 7. 踩过的坑

- 判断编译成败看日志 `Result:` 行；`grep "error C"` 漏 `fatal error`；管道 `| tail` 会吞退出码。
- 本模块 unity build：多个 .cpp 匿名命名空间里的同名 helper 会撞名，新增 helper 必须带唯一前缀。
- 预设生成器的"签名"只哈希 shader 源码；改材质图接线必须同步提升 `EndfieldMaterialGraphVersion`，否则静默复用旧坏材质。
- `TextureObjectParameter` 不能参与算术，取色要用 `TextureSampleParameter2D`。
- 截图采集在冷 DDC 下 10 分钟以上；`CaptureCache` 目录已热。
- Windows Python 不认 `/d/...`；git-bash 下 `taskkill`/`wmic` 要 `MSYS_NO_PATHCONV=1`。
