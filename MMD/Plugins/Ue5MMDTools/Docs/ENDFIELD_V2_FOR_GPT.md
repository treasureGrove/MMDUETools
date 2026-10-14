# 终末地渲染 V2：给接手者的说明（Toon BSDF 探针阶段结论）

日期：2026-10-13。写给接手的 AI（GPT）。先读这份，再读同目录 `ENDFIELD_V2_HANDOFF.md`（含工作区现状与踩坑）。

读者入门入口：[终末地插件着色入门与维护](ENDFIELD_NATIVE_RENDERING_GUIDE.md)，含三版方案对比、数据流图、实际 GI 对照和代码入口。

## 原生着色后端实施（2026-10-14，已完成本批验证）

用户已授权实施“插件内写类似自定义 ShadingModel、取得原生渲染结果”的方案。新增固定 UE 5.8.3 / CL58210709 的 shader 目录适配：仅修改工程 Intermediate 中的副本，通过更深虚拟路径接入；安装引擎源文件不改。自有 `.ush` 接逐灯光 Context、ShadowTerms、AreaLight 和 50 项原生灯光／阴影输入，补传统聚簇灯光以及 GI／反射最终贡献入口。

使用带角色签名的瞬态 ToonProfile 隔离新响应；模式和 GI 倍率由 Profile alpha 传递，RGB 不变，普通材质保持原生。模式 0 保真，1 阴影诊断，2 三档原型；**模式 2 不是最终终末地复刻**。Editor build8、Game game-build3 构建通过。r3–r8 的模式／阴影／GI 对照各 11 图／70 槽检查，另 r11 的传统 GBuffer=0／聚簇开启也是 11 图／70 槽，全通过，共 77 图／490 槽。真实材质 shader 为 21／40／29，无默认或代理 fallback。

UAT package3 完整 Cook／Stage／Pak／Archive exit0，package4 用最新 Game 二进制重新 Stage。相同 Cook 的 final0/1/2/6/7 实际打包游戏均 exit0：12／12 专属槽重绑和 CPU 控制读回，第120帧真实视口 PNG、第160帧报告。阴影诊断与两路间接光归零有明确像素变化，发行包无需源目录映射；不能仅用启动参数或 CPU metadata 证明内核。最终 v4 签名 `E3E3466769C5FCEEDE7D0860A85BF3829288DA93`。r9/r10（含关闭后端）暴露原生传统聚簇 VS／PS 接口不匹配，v4 在工程缓存中修补，r11 重新实测通过。

失败及中断日志保留 Saved：pass0 移动宏条件、r1 实验白名单已修，r2 中断；前两次 Cook／Stage 的资源过滤及 Zen 记录读取失败已换命令行配置重新跑通。首次打包 game0 第1帧 `0xc0000374` 原因未定位，同命令 game0r 和 final0 成功；不要写成已修复。安装引擎 shader、原有 Content／Config 与用户 Face 共2,378项 SHA256 保持，13个测试包已移回 Saved 备份，未提交／推送。代表开发实拍见[本日归档](Screenshots/2026-10-14-endfield-native-backend/README.md)，不能当最终视觉验收。

仍需传递正式的逐像素头部／SDF／独立高光法线 payload；原生阴影标量无法区分 caster 身份。SDF Version3 的侧向错误、专用 UV 和合理自阴影方案仍待继续，不能把下方旧检查通过当成画质验收。完整后端机制、启用方式及数据边界见 [原生着色后端](ENDFIELD_NATIVE_BACKEND.md)。

下一轮先接正式逐像素 payload，再恢复真实客户端 ramp 与各角色方程。现有后端已保留原生灯光、VSM、GI／反射结果，但原生剔除／早退、材质存储格式仍有边界；非零独立 SSR 和 BRDF 加权前天空／capture 辐射未接。本批 Cook 只验证通用几何夹具，不等于已经验证 SDF、骨骼/Morph、透明发片或最终终末地画面。

## 最新实施状态：ush、自动 SDF 与接触阴影（2026-10-13）

**本批已打通自动 SDF 的实际消费，鼻灰点用关闭角色接触阴影修复；完整终末地目标仍未完成。** `Core/TMMDEndfieldNative.ush` 通过一个 struct Custom 输出 normal、PatternUV、face mask 与可选 shadow coverage，原生 Toon 保留真实光照和普通投影。默认 `FaceShadowSuppress=0`、`bCharacterContactShadow=false`；不要为修鼻点关闭整个角色 CastShadow。接触开关随材质恢复，旧预设默认保留角色原开关。

- 预设仍为 `/Ue5MMDTools/Stages/Presets/EndfieldV2/DA_Render_EndfieldV2`，现在 11 个包（新增共享 identity offset gradient）、图版本 3。Skin 默认 SphereBlend=1，Boundary=.55，offset 限制 [-.9,.34]，原生漫反射和高光共用法线这一限制保留。没有修改引擎、用户 Face 或项目默认 Substrate 开关。
- SDF Version3：三角形栅格化到 UV，R/G=独立左右角阈值/π；B 标记经过几何与阈值检查的镜像共 UV，运行时按当前几何侧解码而不翻 UV；A=有效脸部。复杂冲突回退，RGB padding 不扩大 mask。75% 球面与25%几何，固定俯角 .35，仅描述正前方连续亮区，真实遮挡交给 UE。
- 导入的同步/异步路径自动烘焙、保存 `UMMDEndfieldFaceData` 并按槽绑定。已有网格用 `MMDEndfieldBindSdf` 默认只读检查，加 `-Generate` 才保存显式网格和派生纹理；旧单张 `MMDEndfieldBakeSdf` 继续可用。自动路径保护同名非工具纹理，重烘无覆盖会清除旧缓存。runtime 类使用强引用，实际 Cook/打包尚未验证。
- 最终构建 `build13`、11包 `generate4` 通过，静态 shader/C++ 检查通过。p17：29 图、140 次槽检查、0失败；5槽共29,472有效 texel，主脸15,777有效、15,510镜像、56复杂冲突。真实 GPU mask 与 SDF 开关对照均已自行看图。p18：Substrate=0 独立回读 Version3，12图/105槽/0失败，包含绑定、分类切换、恢复与接触开关恢复。
- 最终 `verify3` 原生资产只读校验通过、没有写包；p19 原生独立回读 Version3，14图/140槽/0失败，接触开关恢复通过。下一轮从这些最终结果继续。
- p15 同 SDF/姿态的接触开关因果对照确认鼻尖灰点来自接触阴影；仅关闭接触投影已改善，所以脸面 ShadowPassSwitch 仅作备用，默认不移除脸面对场景的投影。p13/p14 Version2 遮罩窄是镜像 UV 冲突，p16 保存已有夹具包的部分加载错误均保留失败日志，p17 已修复并重跑。
- [本批真实截图与来源](Screenshots/2026-10-13-endfield-sdf-contact/README.md)。Saved 下 `gpt-v2/` 保存详细报告、UV诊断与数学检查；测试模型/纹理不纳入插件 Content。保护范围598项只有两个 Endfield shader 为本批有意改动，其余596项哈希保持。未提交/推送。

下一项从真实客户端 ramp 继续：`Saved/Phase2Reference/Endfield2/toon-probe/gpt-v2/native_role_ramp_plan.json` 已核对9个代表材质、13张ramp及 asm证据。RD 的 A 是明暗权重、RGB 是色度 tint，不能直接复制 RGB 到 native Profile；RS 需包含波瓣幅度，二维纵轴与粗糙度相关。Skin body/face01像素相同；Hair 还需水平/仰角修正与内部暗部；Cloth04最常见；Metal并非独立客户端角色；眼球样例用Body RD+Matcap且Specular/Smoothness为0。完整十项清单仍有效，不把本批基础链路当作终点。

## 用户既定复刻方案重新核对（2026-10-13）

用户指出上一轮只交付通用 Toon 基础接入，偏离了已与 Claude 对齐的终末地复刻目标。已读取本机 Claude Code 原会话 `46e9bd89-c2be-4366-ac69-673d031aaace.jsonl`：2026-10-13 11:55 SGT 的完整清单明确包括以下全部内容；用户本聊天再次明确确认采用 SDF 方案。

1. 引擎光照接入：角色实际接收场景阴影、自阴影、GI、AO、反射，并与环境融合。
2. 明暗结构：从本机真实客户端的材质覆盖值与 ramp 像素对齐多段明暗、阴影色相和高光响应。
3. 阴影：自阴影与场景遮挡并入 ramp，完成多灯明暗合成；实验开关失败是待修问题，不是删除目标的理由。
4. 脸部：**球面法线 + 自动烘焙 SDF + 偏移采样**，随头部和主光方向更新，解决灰块、鼻影与眼窝。自动生成的 SDF 是用户已接受的派生数据；不能再以旧通用性约定为由禁用。仍须支持仅 base_color 的可用回退，不要求用户手工制作配套贴图。
5. 头发：各向异性高光环、内部暗体积与刘海投影；原生节点不消费 Tangent 时，寻找插件内的补充实现，不以普通 Anisotropy 参数代替最终效果。
6. 材质分路：皮肤、头发、布料、金属、眼球各自的 ramp、高光与粗糙度；眼部、眉毛、睫毛与头发阴影层按真实资料完善。
7. 边缘光与描边：深度边缘光、描边随底色变化、宽度及规则对齐真实数据。
8. 后处理与融合：角色打光、曝光、tonemap、bloom、grading 对齐本机客户端，不能继续只复制旧舞台参数。
9. 通用性：只有 base_color 也完整，自动派生数据和可选 ramp/细节贴图有兜底；这不等于取消已经批准的自动 SDF。
10. 回退与分发：非 Substrate 回退，半透明/遮罩发片、骨骼与 Morph 支持，实际应用到插件角色和预设。

**基础接入只是实施状态，不是最终方案或验收终点。** 下方记录上一轮 SDF 尚未接通时的历史缺口；本批完成状态以上方最新节为准。真实 ramp、深度 rim、眼部分层等继续按清单完善。验收依据实际终末地画面，不以编译、材质数量或 PNG 数量代替。所有项处理并自行看图之后才向用户展示验收截图；本批归档只用于开发验证。

## GPT 接手后的最新状态（2026-10-13）

**灰模已修复，独立 V2 原生 Toon 预设已生成并通过回读与运行时验证；终末地画质仍未验收通过。** 当前资产为 `/Ue5MMDTools/Stages/Presets/EndfieldV2/DA_Render_EndfieldV2`，包含三种原生父材质、六个共享 Profile，并保留旧 Endfield Unlit 父材质作为关闭 Substrate 时的回退。

- 灰模根因是 UE5.8 `PostEditChange()` 使用 `EMaterialShaderPrecompileMode::None`，只建立材质表达式数据，没有主动编译绘制 shader。新增 `ForceRecompileForRendering(Default)` 后底色恢复。`IsValidForRendering()` 在本机引擎只检查冻结数据非空，不能代替 `CompiledSuccessfully` / `IsCompilationFinalized` / `GetShaderNum()>0`。
- 探针 `FParse::Value` 默认在逗号处停止，旧 `-Exp=a,b,c` 实际只执行首项；现在显式关闭该行为并拒绝未知实验。新增已保存预设入口 `-NativePreset=<路径>`，支持 `ab,runtime,shadow,lighting`。
- 三种原生材质真实编译通过，21 / 40 / 29 个绘制 shader。半透明使用 Coverage Weight（底色 alpha × Opacity）与 `TLM_SurfacePerPixelLighting`；GreyTransmittance 和 Legacy 是同一个枚举值，不再当两种能力测试。
- `UMMDStageLibrary` 在 Toon 可用且对应新父材质存在时选择原生路径。新 MID 先设置 Profile、刷新代理，再复制源输入；部位分类改变时仅重建仍由预设持有的当前槽，恢复快照保持不变。原有预设不配置新字段时保持旧行为。
- `p12`（Substrate=1、UnifiedDiffuse=0）：53 图 / 0 失败；105 次槽位检查无默认材质或渲染代理回退，角色切换、贴图保留、其他槽保留和恢复均通过。`p11`（Substrate=0）：9 图 / 0 失败，自动选旧父材质。最终 `verify2` 磁盘回读通过、没有写包。
- `p10` 只把多灯统一漫反射开关改为 1 后明显过暗；目前保留默认 0。阴影仍能进入每灯 ramp，但多灯统一方案没有视觉通过，不能以它作为生成前提。
- 上一轮 V2 Profile 禁用 offset/hatching 贴图，**未接入已批准的自动 SDF，这是实施缺口**。几何球面法线仅在有效头部半径附近生效，默认关闭。引擎 Toon 节点未消费自定义 Tangent，移除了无效控制；各向异性目前只能用引擎默认基，尚不能满足既定高光环目标。
- 原有 598 个 Content / shader / Config 文件与接手时的哈希相同；未还原工作区旧改动，未改用户 Face、引擎文件或 `DefaultEngine.ini`，未提交/推送。用户在本聊天已明确允许 GPT 使用多个子代理，旧的并行限制不再适用。

实际截图与来源见 [原生 V2 归档](Screenshots/2026-10-13-endfield-native-v2/README.md)。完整日志在 `Saved/Phase2Reference/Endfield2/toon-probe/gpt-v2/`。`p9` 的 35 项失败是新增代理检查没有初始化输出指针造成的误报；原失败报告保留，`p10/p12` 已修正并重新实测。第一次 `verify` 的六个 Profile 失败来自反射比较 transient 曲线句柄；现精确比较全部序列化曲线字段，`verify2` 通过，没有放宽 ramp 数值。

### 下一步从这里继续

1. 固定 `p12` 的资产、镜头与光照作为原生基线，处理鼻部灰点、脸部阴影、发片高光与布料层次。不要重新排查已经解决的灰模或继续使用 `p5` 的成功判定。
2. 按上方既定方案接通自动 SDF 与球面法线，处理鼻部灰点；头发天使环须补插件内可实现的方案。自动派生 SDF 已获用户许可，不再排除；仍不修改引擎、不要求用户制作专属配套贴图。
3. 天空、自阴影与外部遮挡响应明确；关闭 GI 的像素变化接近时序噪声，不能宣称 Lumen GI 已完整验证。继续用实际主视图和受控间接光场景检查，之后再做 Cook/打包。
4. 后处理、深度边缘光、原作描边及眼部专用分层仍未对齐。现有真实材质值是参考依据，UE Profile/粗糙度等候选值不是原作等价方程。
5. 项目默认未开启 Substrate；本批全部开关仅作用于后台进程。资产已支持自动回退，不要把回退画面当新的画质成果。

## 0. Claude 中断时现状（历史记录，以下成功判定已更正）

用户否决了现有 Unlit 版（"好丑，纯 unlit 感"）。改用 UE5.8 原生 **Substrate Toon BSDF + UToonProfile** 的路线，探针已**跑通并出图**，引擎灯光确实作用到了角色；但**材质没有显示 base color 贴图（灰模）**，原因未查清。没有任何成果可给用户看。

## 1. Claude 当时记录的事实（历史，须结合上方更正）

探针：`-run=MMDEndfieldToonProbe`，源码 `Source/Ue5MMDToolsEditor/Private/Editor/MMDEndfieldToonProbeCommandlet.cpp`（未提交，约 1630 行，全部 Transient，不写盘）。输出目录 `MMD/Saved/Phase2Reference/Endfield2/toon-probe/p5/`（8 张图 + `p5_report.json`）。

| 事实 | 证据 |
|---|---|
| Substrate 命令行即可开启，不必改 ini | `-ini:Engine:[/Script/Engine.RendererSettings]:r.Substrate=True` → 报告 `substrate_enabled: true`，`ProjectGBufferFormat=1`（非 blendable），`ClosuresPerPixel=4`。仓库 `DefaultEngine.ini` 未改 |
| 当时把 Toon BSDF 判作已编译 | p4/p5 的 `ready=1` 是宽松判定，实际 success/finalized=0；不能证明原生 Toon 已渲染，现以 p12 为准 |
| 引擎光照作用到角色：有投影、有体积、接收阴影 | `p5_toon_full.png`：角色在地面有投影，身体有明暗起伏。对照 `p5_unlit_full.png` 是平的 |
| MID 能被应用到全部槽位 | 日志 `slot N parent=ToonProbe_Opaque base_tex=…(1)`，但见 §2 |
| 半透明槽位仍走旧 Unlit 父材质 `M_MMD_Endfield_Translucent` | 日志 slot 4/7/8 的 parent。Toon BSDF 的半透明用 `SubstrateWeight`（Coverage Weight）接，探针里实现了但 `toon_translucent_ready` 为 false，**未验证** |
| 当时放宽了 "ready" 判定 | 后续确认这是误判：冻结数据非空不代表绘制 shader 已编译。已恢复严格检查并主动编译 |

## 2. 当时未解决的主问题：为什么是灰模（已解决，保留排查历史）

p5 日志显示每个槽位 `base_tex=body_png(1)/head_png(1)/skin_png(1)…`，即 **MID 的 `base_color_tex` 参数确实被设了真实贴图**，`BaseColor=(1,1,1,1)`。但出图却是均匀灰，连眼睛、嘴唇颜色都没有（脸上只有红蓝眼线是别的东西）。

已排除：参数没传（上面日志证明传了）。

待查（按可能性排序，我没来得及验证）：
1. **Toon BSDF 的 `BaseColor` 引脚实际没吃到贴图**：材质图里 `Tinted = base_color_tex(RGB) × BaseColor` → `Toon->BaseColor`（见探针 `ToonProbeBuildMaterial`，约 290–360 行）。检查采样节点输出索引（RGB 是输出 0，alpha 是 4；若连错通道会得到灰）。
2. **漫反射 ramp 乘出来的灰**：`UToonProfile::DiffuseRamp` 的颜色（探针里 Lit=0.92 灰、暗部偏蓝灰）与 BaseColor 相乘。如果 ramp 曲线 key 设置有误（`ToonProbeSetRamp`，约 140–165 行），或 `FRuntimeCurveLinearColor` 的 alpha/通道取值有问题，整体会被压成灰。
3. **贴图 sRGB/压缩问题**：`body_png` 等是否在 commandlet 离屏下已完成流式加载（日志有 `Waiting for textures to be ready`，p4 的 `Blocking on texture streaming`）。若 mip 未就绪可能出占位灰。可对比 unlit 版同一贴图是否正常（unlit 版颜色正常，说明贴图本身可用，所以这条概率低）。
4. **`SetShadingModel(MSM_DefaultLit)` + Substrate** 下材质 `FrontMaterial` 的混合：确认没有被引擎默认材质覆盖。
5. 脸部两个红蓝眼线颜色是正确的贴图色，说明**部分槽位的贴图是通的**（眼睛槽 `Eyetex_png`、眉毛槽）；灰的是 body/skin/head 槽 —— 它们的贴图都带 alpha / 或是同一张 body_png。重点查这几个槽，可能是 `Opaque` 路径下 alpha 通道被当作什么处理，或它们实际走的是另一个材质。

建议第一步：在探针里临时把 `Toon->BaseColor` 直接接 `BaseTexture`（去掉 Tinted），再接一个纯红常量，二分定位是贴图问题还是 ramp 问题。每次只改一个变量。

## 2.5 探针其余实验尚未跑

`-Exp=` 支持：`ab, shadow, ramp, lighting, face, hair, override, translucent, all`。目前只跑了 `ab`。其余（多灯统一漫反射、ramp 是否含阴影、球面法线脸、各向异性发丝、**MID 覆盖 ToonProfile 是否生效**、半透明）都没验证。

注意：冷 DDC 首次编 Substrate shader 约 150 秒；热 DDC 后一次运行约 20 秒。DDC 目录 `toon-probe/ddc-sub`。

## 3. 已知引擎约束（已读源码确认）

- `r.Substrate`、`r.Substrate.Experimental.ToonUnifiedDiffuse`、`r.Substrate.Experimental.ToonReflectionQuantizationEnabled` 均 **ReadOnly**，只能启动时设置。UnifiedDiffuse 默认 0（多灯时每盏灯各过一遍 ramp，暗部可能出台阶），需要在 `-ini:` 里一起设并 A/B。
- `UMaterialInstance` 有 `ToonProfileOverride` / `bOverrideToonProfile`（`MaterialInstance.h:671,693`），"按角色分路换 ramp"理论可行，**运行时生效未验证**。
- `FToonProfileStruct` 构造函数未导出（缺 ENGINE_API），插件里要用 `GetDefault<UToonProfile>()->Settings` 拷贝默认值。
- 插件运行时只按混合模式选父材质：`Source/Ue5MMDTools/Private/Stage/MMDStageLibrary.cpp:299 ApplyCharacterRenderPreset`，角色靠 `MaterialRole` 标量区分。
- 用户工程必须开 `r.Substrate=True`（重启 + 全量重编）。这是分发层面的重大取舍，要用户拍板；Unlit 版保留作非 Substrate 回退。

## 4. 逆向数据（对齐真实终末地）

客户端：`E:\Games\Installers\Hypergryph Launcher\games\Arknights Endfield`（Unity 2021.3.34f5 / HGRP）。已取得（在 `MMD/Saved/Phase2Reference/Endfield2/`，**未复核**）：

- `material-values/material_values.json`（7 MB，含 role_summary / outline_summary / shadow_color_eval）
- `material-values/ramps/*.json`（67 张真实 ramp 像素）
- `mv_work/shadow_eval.json`（300 条"亮部→阴影色"的色相/明度评估）

**没取到**：字节码光照方程、后处理（tonemap/bloom/grading）、角色打光 rig、描边规则。上一轮 `StaticProbe/` 的 `.asm.txt` 是字节码真值，`*.reconstructed.hlsl` 不可信。工具与路径坑见记忆/交接文档。

## 5. 用户的验收标准（重要）

- 不要再给"没有灰脸 = 通过"这类自说自话的结论。用户一眼看出 Unlit 版是平的。
- **所有考虑项处理完、自己看过图之后**才给用户截图。清单见交接文档 §4。
- 用户的订阅额度吃紧（一次会话烧掉约 50%）：**不要开多个子代理并行**，不要重复读大文件，先读已有产出再决定补什么。
- 不弹前台窗口：UE 一律 `UnrealEditor-Cmd.exe -RenderOffscreen`。

## 6. 工作区（未提交）

以下为 Claude 中断时的记录，探针的宽松就绪判定现已更正。见 `ENDFIELD_V2_HANDOFF.md` §6；27 个被旧生成器重写的 `Content/` 资产本批保持接手时现状，后续必须逐文件核对和保存现有改动，不能直接笼统还原。
