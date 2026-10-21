# 二期实验性交接

**历史快照（2026-10-19 标记）**：本文保留原批次的研究、状态与验证，内部“当前／下一步”和历史聊天指令不作为今日架构或执行约束。现状先读[当前架构](RENDERING_ARCHITECTURE.md)和[接手状态](ENDFIELD_V2_FOR_GPT.md)，原日期与证据保持。

交接日期：2026-10-12。用户接手后续制作，本轮停止继续调 shader。这里交接已修改的源码、研究记录和少量截图；**画质尚未达到终末地人物目标**。运行检查通过不能代替视觉验收。

## 用户未完成 shader 的核对

相对一期已提交基线 `009d90a11f3dbe98d40fb46fac564238099111a2`，五个已跟踪 `.ush` 中四个保持 Git blob 一致：

| 文件 | 状态 |
| --- | --- |
| `Shaders/Core/MMDToonLighting.ush` | 未修改；原文件 SHA256 `8893D13D3ECD3F51F666C517811450C7838A6E05857B267567ABA6A0B243203F` 与 10 月 11 日冻结值一致 |
| `Shaders/Core/MMDToonLighting_Archived.ush` | 未修改 |
| `Shaders/MMDStandard/MMDStandardCore.ush` | 未修改 |
| `Shaders/Experimental/MMDReferenceToonGlamorousLighting.ush` | 未修改 |
| `Shaders/Core/TMMDReferenceBRDF.ush` | **有修改**；这是一期 `add91e1d` 新增的 Reference 文件，二期修改几何 Skin 平滑、Hair 双带/发流兜底与 Cloth 宽瓣，不能宣称所有 `.ush` 都没动 |

用户自行维护的 `Shaders/TMMDShader/TMMDAnimeFace.usf` 未修改，原文件 SHA256 为 `59B39C645D3AE36270F5E79254A1CE29540B23DE96DD97700FAF6A61627D57A4`；原 Skin/Hair/Cloth、Universal 各版和其他已跟踪 shader 均未出现本轮差异。Git blob 检查应用仓库文本过滤，另按原文件字节 SHA256 检查了 10 月 11 日冻结的旧主 691 文件和新四源码。逐项结果见 [PHASE_TWO_SHADER_AUDIT.json](PHASE_TWO_SHADER_AUDIT.json)。

既有 shader 实际修改共四个：`TMMDReferenceBRDF.ush`、`TMMDReferenceCharacter.usf`、`TMMDGardenSurface.usf`、`TMMDStageBackdrop.usf`。新增 `TMMDEndfieldPhase2BRDF.ush` 和 `TMMDEndfieldPhase2Character.usf`。详细影响见[预设范围](PHASE_TWO_PRESET_SCOPE.md)，这些早期共享变化可能影响旧风格；旧资产不变不代表旧渲染行为完全不变。没有删除或恢复用户文件。

## 接手入口与资产状态

- 新专属 shader 位于 `Shaders/Core/TMMDEndfieldPhase2BRDF.ush` 和 `Shaders/TMMDShader/TMMDEndfieldPhase2Character.usf`，包含 Skin/Hair/Cloth 分路；最近发带方位门控只略减亮度，长亮轨和灰脸仍在。
- 新入口源码位于 `Source/Ue5MMDToolsEditor/Private/Editor/MMDEndfieldPhase2Commandlet.cpp/.h`。先按项目原构建流程编译 Editor 模块，再运行 `-run=MMDEndfieldPhase2 -Generate -Report=<本机JSON路径>`。仅生成 `Stages/Materials/EndfieldPhase2/` 下三种父材质及 `Stages/Presets/EndfieldPhase2/DA_Render_EndfieldPhase2`，完整路径和输入合同见[专属记录](PHASE_TWO_ENDFIELD_DEDICATED.md)。
- 独立四资产只在本机 `Saved/Phase2Reference/Preparation20261011/MainViewProbe/Plugins/Ue5MMDTools/Content/` 生成；**没有复制到主 Content，也没有随本次提交上传**。主 DLL 未发布。因此拉取源码不会自动启用实拍里的独立二期预设，需要编译并定向生成。不要使用旧全量 `MMDStagePresets -Generate` 来代替这一入口，它会重建其他父类/预设并改变旧 Sakura family。
- 旧主七套预设、十二个 Reference 父资产文件保留；已有舞台 C++ 和旧 generator 源码变化随实验一起保存，影响与风险已列在范围记录中。共享代码没有擅自恢复一期。
- 仅提交插件源码、文档和少量截图，不提交整个 Saved、构建产物、解包器、游戏/测试角色资产。另一项任务的 `MOBILE_MMD_EDITOR_ARCHITECTURE_TASKS.md` 不纳入本次提交，原文件保留。

## 已有验证与未完成项

实际 UE 验证均发生在 **2026-10-11**，不是 10 月 12 日重新测试。UE 5.8.3 / CL58210709 / PCD3D_SM6，最近两批后台主视图共 12 张 PNG；每批三 parent/35 MID 完整 shader-map、灯光、四级阴影覆盖、156 物理步/178 刚体及最终 PP 检查通过。只归档少量代表图，失败批次及局限没有改成通过。10 月 12 日仅核对文件、文档和 Git，未启动 UE 或前台。

最新结果见[发带候选实拍](Screenshots/2026-10-11-phase-two-endfield-hair-azimuth/README.md)及[正常/平色输入归因](Screenshots/2026-10-11-phase-two-endfield-input-attribution/README.md)。原 Hair 底图有画死亮纹，平色后仍有程序性长亮轨；单 mip alpha 对照不证明原压缩/mip链及逐 LOD 边缘等价。脸部灰带与衣料塑造不足仍未解决；可选贴图全部关闭尚未实拍验证。无原作当前帧 GPU 绑定证明，不称商业游戏完整 shader 复刻。

预览消费、主工程资产发布、打包复验和画质达标仍未完成。后续由用户接手；保留实验结果供继续修正，不自动合并或覆盖共享分支。
