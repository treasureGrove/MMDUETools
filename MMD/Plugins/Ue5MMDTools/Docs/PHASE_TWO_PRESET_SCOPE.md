# 二期唯一预设范围核对

**历史快照（2026-10-19 标记）**：本文保留原批次的研究、状态与验证，内部“当前／下一步”和历史聊天指令不作为今日架构或执行约束。现状先读[当前架构](RENDERING_ARCHITECTURE.md)和[接手状态](ENDFIELD_V2_FOR_GPT.md)，原日期与证据保持。

日期：2026-10-11。用户已回答「终末地人物方向」。本轮新增独立的 EndfieldPhase2 预设与三种父材质，旧 Genshin、旧樱庭和其他预设保留当前文件与绑定。此前暂停及中断记录保留为历史事实。

## 已确认的独立目标

- 预设：`/Ue5MMDTools/Stages/Presets/EndfieldPhase2/DA_Render_EndfieldPhase2`。
- 三种父材质：`/Ue5MMDTools/Stages/Materials/EndfieldPhase2/M_MMD_EndfieldPhase2_{Opaque|Masked|Translucent}`。
- 独立源码：`TMMDEndfieldPhase2Character.usf` 与 `TMMDEndfieldPhase2BRDF.ush`。主着色、表面细节及环境粗糙度三处 Custom 都改用独立源码；公共 `MMDToonLighting.ush` 保持当前内容。
- 新 commandlet `MMDEndfieldPhase2` 只允许写上述四个新包；不运行原全量 `MMDStagePresets -Generate`。既有六处生产改动、用户 Face 与公共 Lighting 均冻结当前字节，不擅自恢复 HEAD。
- 首轮实际消费验证于 2026-10-11 21:06:58 SGT 输出三图但十项失败，机械隔离没有带来合格画质。两个专属 shader 的 Hair 宏/微法线与双带、Cloth 褶皱/纤维响应、Skin 归一包裹光已于 21:54 实际编译并捕获，改善很小，完整资源检查九项失败。22:33–22:34 使用完整 material-map 编译完成正常/保留 alpha 的平色 Hair 两组三视图，运行检查通过；仍有程序性长亮轨、灰脸和衣料平板感，画质未通过。详见[专属记录](PHASE_TWO_ENDFIELD_DEDICATED.md)。
- 使用原樱庭资源和已有 native 舞台作测试夹具，不生成新地图/BP，不把它记作保存地图工作流通过。此轮保留真实应用库写入的 MID 细节参数，背景也有此前尚未捕获的候选，因此不声称与 Revision4 为单变量视觉 A/B。
- 22:50 发带方位约束批次仅改一个专属 BRDF 函数，三parent生成与正常/平色两组三视图运行检查通过；实拍只略减亮度，长亮轨形状基本不变，目标未达。所有候选仍只在私有工程消费，未发布主 Content 或 DLL。

## 主工程实际七套预设

均为 `UMMDStageRenderPreset`，资产目录为 `/Ue5MMDTools/Stages/Presets/`。下表 ID 是真实资产后缀，不是新造的显示名称。family 根据序列化父材质方向和 builder 的编号映射推得；该 DataAsset 没有独立的 `ReferenceFamily` 属性。

| 显示名 | ID / 真实 package path | family | 当前父材质方向 |
| --- | --- | --- | --- |
| 霓虹演唱会 | `/Ue5MMDTools/Stages/Presets/DA_Render_NeonConcert` | 0 | Genshin |
| 月光镜庭 | `/Ue5MMDTools/Stages/Presets/DA_Render_MoonMirror` | 1 | Wuthering |
| 樱庭水镜 | `/Ue5MMDTools/Stages/Presets/DA_Render_SakuraTheater` | 0 | Genshin |
| 银白柔光 | `/Ue5MMDTools/Stages/Presets/DA_Render_SilverCity` | 3 | SilverCity |
| 明快卡通 | `/Ue5MMDTools/Stages/Presets/DA_Render_OpenWorldToon` | 0 | Genshin |
| 工业动画 | `/Ue5MMDTools/Stages/Presets/DA_Render_IndustrialAnime` | 2 | Endfield |
| 电影冷暖 | `/Ue5MMDTools/Stages/Presets/DA_Render_CinematicAnime` | 1 | Wuthering |

每个预设引用该方向的三种 blend 父材质，完整路径为：

| 方向 | Opaque / Masked / Translucent 父材质 package 模板 |
| --- | --- |
| Genshin | `/Ue5MMDTools/Stages/Materials/M_MMD_Reference_Genshin_{Opaque|Masked|Translucent}` |
| Wuthering | `/Ue5MMDTools/Stages/Materials/M_MMD_Reference_Wuthering_{Opaque|Masked|Translucent}` |
| Endfield | `/Ue5MMDTools/Stages/Materials/M_MMD_Reference_Endfield_{Opaque|Masked|Translucent}` |
| SilverCity | `/Ue5MMDTools/Stages/Materials/M_MMD_Reference_SilverCity_{Opaque|Masked|Translucent}` |

本轮只读读取了七个主预设与七个私有同名预设的序列化 reference 字符串，并与 generator 的风格名称、既有使用文档互证；不是新 UE 对象加载或实际 MID 读回。主七预设和十二 Reference 父材质共十九文件的 SHA256 全部与 Revision4 的 `MainProtectedFiles` 一致，未覆盖主资产。

隔离 Revision4 的 `DA_Render_SakuraTheater` 引用 Endfield / family 2，主工程同名资产仍是 Genshin / family 0。名称相同不代表它们的内容相同，不把私有副本自动发布到原樱庭。十九文件及十四预设的逐项路径、引用和哈希记录保存在 Saved 的 `ScopeCorrection20261011/preset-inventory.json`。

## 参考与角色证据

- **B「樱庭水镜」**：用户此前明确选过的舞台方向，文件 `Saved/ArtDirection/20261009/B_sakura.png`，2026-10-09 生成式目标；sources.json 只说明日式庭院/月光/灯笼和柔和动漫，并未把 B 的人物定义为钟离或商业游戏原角色。
- **终末地人物方向**：用户本次明确确认；本机定向离线记录为庄方宜的 Material → CharacterNPR 引用，社区参考是终末地风格重建。旧 IndustrialAnime / Endfield 不覆盖，新目标使用上方独立 EndfieldPhase2 路径。
- **原神钟离**：未找到钟离 / Zhongli 专属预设命名或将已有预设、参考图、实际角色绑定到钟离的证据。Genshin 是通用方向，不能将 OpenWorldToon、NeonConcert 或 SakuraTheater 擅自视为钟离专属。
- **现有实拍模型**：旧有效主视图使用相同 Miku 测试模型、动作与物理夹具，不能标成钟离或庄方宜实拍。

此前「钟离/终末地」问题已由用户明确回复解决；不继续等待同一确认，也不将测试角色标作原作人物。

## 已有源码改动的精确影响

以下是当前相对已提交 HEAD 的工作区改动，保留现状，不自行回滚或覆盖。Saved 私有候选和实际主源码分开记录。

| 真实路径（相对插件根） | 现有改动与影响 | 当前状态 |
| --- | --- | --- |
| `Shaders/Core/TMMDReferenceBRDF.ush` | family 2 的 Skin 平滑和 Hair 双偏移带；另有通用切向兜底与 Cloth broad-spec 函数。共享 include 被四方向消费，并非全文件仅影响 Endfield | 已在工作区；BRDF 为已实测 Revision4 E5854A03…；不能保证旧风格行为完全未变 |
| `Shaders/TMMDShader/TMMDReferenceCharacter.usf` | family 2 cloth 镜面比例；另有全风格 Hair 几何镜面/环境系数、遮罩兜底和 Cloth 混合等共享变化 | 已在工作区，C49FBCC1…；旧父资产文件未变不等于旧渲染行为未变 |
| `Shaders/TMMDShader/TMMDGardenSurface.usf` | 保存显式粗糙度、细节小幅调制；影响消费该公共庭院 shader 的材质 | 已在工作区，487C43BC…；不是唯一人物预设专属 |
| `Shaders/TMMDShader/TMMDStageBackdrop.usf` | 背景贴图混合/映射及月亮合成；公共 shader，部分混合逻辑不限单一预设 | EC29F5DF…已应用源码并同步 private，随后随 21:06 与 22:33/22:50 主视图捕获；未独立完成背景材质/画质验收或发布主资产 |
| `Source/Ue5MMDTools/Private/Stage/MMDStageActor.cpp` | Sakura 主题前庭湿石、纸墙开口和花簇布局变化；同一主题可由不同预设使用 | 已在工作区，77022F62…；不能当作唯一人物预设改动 |
| `Source/Ue5MMDToolsEditor/Private/Editor/MMDStagePresetsCommandlet.cpp` | `ReferenceFamilyFor` 把 Sakura 加入 family 2；Sakura 参数变化；捕获 helper FilmSlope .82→.86。全量生成会重建多预设/父材质 | 已在工作区，D15AFDF6…；在收窄范围下不得直接执行现有全量 Generate |

用户维护的 `TMMDAnimeFace.usf` 未修改。主 Content 未覆盖，主 DLL 未重编或发布。本批没有提交、推送、分支或 stash 操作。

## 历史暂停边界与中断批次

WorkflowProbe1 已启动的私有宿主 C++ 编译日志于 18:03:20 SGT 显示 Succeeded，随后 `Start-Process -Wait` 等待本次 VCTIP 辅助进程。原执行会话 45397 已通过 Ctrl+C 中断，返回 exit 1。18:49:40 SGT 收尾观测中包装 PID 122252/126128 已退出，三个 VCTIP 保留，没有 UE/编译子进程。没有新生成目录、新截图或完整 Build 进程回执；不标为整个批次通过。

收到范围纠正后，先保存已批准 review JSON 的原始版本，再将待审控制文件 `RootApproved` 撤回为 false。取消后仍为 false，Generate 和 Native2/Map1 均未执行。没有热改冻结的 host/runner，没有强杀辅助进程、UE 或其他任务，没有更改生产资产。原批准 SHA 与撤销记录保留，不伪造成功回执。

门禁日志中没有本轮 MMDPlugin 的 `released` 事件，`Build/build-process.json`、`Build/summary.json` 和整个 `batch-summary.json` 也不存在，不能声称 gate 的 finally 完整执行。日志显示另一项 Component 任务在 18:48:44.042 SGT 取得同一互斥锁，并在 18:48:58.837 SGT 释放；这证明锁已可用，但不证明该任务检查通过。无需为补齐回执重启 MMD 批次。中断状态与日志/文件哈希保存在 `Saved/Phase2Reference/Preparation20261011/WorkflowProbe1/interruption-evidence.json`。

Hair4、PointSpec、ProductionHandoff 等旧 Saved 草案仍未批准、未应用。旧 WorkflowProbe1 控制文件继续保持 false；确认新目标后另建 EndfieldDedicated 批次与输入清单，不恢复旧全量生成路线。

## 当前验证边界与换路线条件

1. 已确认终末地人物方向，使用新的独立资产路径，不改名覆盖旧预设。
2. 冻结旧七套预设、十二父材质及当前共享代码。保留原有改动，不声称旧风格已经恢复为一期画面。
3. 目标确定后，仅对白名单内目标预设及专属父材质/源码生成和验证；报告实际父材质、纹理、role、灯光、最终 PP。旧预设文件哈希、绑定及输出行为另作保留检查。
4. 首轮只固定角色、相机、曝光、动作和阴影，改变一个有效因素。没有参考角色几何/原始 GPU 帧时，验收为指定参考方向，不声称商业游戏精确复刻。
5. 预先设换路线条件：实际材质/灯 RT 不匹配先修消费路径；发带在镜面隔离后仍沿平面发片不移动，先核 N/T/曲率；目标依赖缺失的大褶皱/独立眼角膜则转资产路线，不继续用 BRDF 参数掩盖；专用 UV/SDF 必须弃用并选通用几何方案。

此前[完整工作流审查](PHASE_TWO_WORKFLOW_AUDIT.md)只作结构证据；其中旧樱庭绑定、历史 DLL 和预览缺口不自动成为本轮修复项。

## 后续本机交付证据

新的完整运行截图或录像先保存在本机。每批清单记录真实存在的文件、捕获/生成时间、项目与源码来源、UE 版本、验证范围、通过/失败/未执行项、文件大小和 SHA256。历史实拍及生成式目标分别标注，不计入新批次结果；本次中断没有新实拍。当前不上传云附件或发布文件，既有自动审批拒绝不通过其他通道重试。
