# 2026-10-15：YYB 16th 带翼款实际引擎对照

独立插件最终完整脚本`EF8f`及空白宿主检查也已通过，具体范围与SHA另见[分发证据](distribution-sources.json)和[分发说明](../../ENDFIELD_PLUGIN_DISTRIBUTION.md)。本页以下仍只说明角色实拍。

这次换成仓库已有的 **YYB Hatsune Miku 16th 带翼款**。以下 5 张均为 2026-10-15 的 UnrealEditor-Cmd 离屏实拍，1280×720，直接复制原 PNG；没有裁图、拼图、调色或生成式处理。完整来源与 SHA256 见 [sources.json](sources.json)。

模型制作署名：SANMUYYB／霁行Ralker／Delta幻黑，按原模型 `readme.txt` 记录。导入到项目 Saved 验证目录，本归档不包含模型或角色资产。导入结果为 610,819 顶点、997,023 三角形、39 材质、550 骨骼、71 个 Morph。

**本次只拍参考姿势。** VMD 导入有 196 条不匹配轨道警告；动作与 Morph 的导入数量不表示它们已经跑过渲染验证。

## 如何看这组图

“当前”是 Mode 2 的插件响应；“原生”是 **同一 V2 材质和场景中的 Mode 0 原生 Toon 响应**，不是旧 Unlit。两组使用同一个显式 SampleManifest、参考姿势及环境。报告内共有的 8 个机位逐项核对，相机位置、FOV 全部完全一致；环境设置对象也完全一致。这里核对的是报告记录的字段，没有宣称读回过完整相机矩阵。

当前全景：full 机位启用 `-FitSampleCamera`，按新模型包围球留出翅膀空间。

![Mode 2 当前全景](yyb16-current-full.png)

面部原生 Toon（Mode 0）：

![Mode 0 原生 Toon 面部](yyb16-native-face.png)

面部当前响应（Mode 2）：

![Mode 2 当前面部](yyb16-current-face.png)

衣料原生 Toon（Mode 0）：

![Mode 0 原生 Toon 衣料](yyb16-native-cloth.png)

衣料当前响应（Mode 2）：

![Mode 2 当前衣料](yyb16-current-cloth.png)

## 这次确认了什么

两组均为 UE 5.8.3 CL58210709、D3D12／SM6，启用 Substrate，GBuffer 格式 1、ToonProfile 分辨率 256、VSM、Lumen GI／反射与硬件光追；UnifiedDiffuse=0。探针使用同一套灯光和预设后处理，并显式设置手动曝光。`sources.json` 保留两次报告中的原始 CVar 值；全局 AutoExposure 默认值不代表捕获视图的手动覆盖设置。

`clothes01..07` 已通过通用名称规则识别为 Cloth，这 7 个槽均在最终报告中确认；加上裙摆、袖子和靴子等，整模型实际有 14 个 Cloth 槽。`body01`、耳机和翅膀保留 Generic，不凭名字贸然当成金属；**本模型没有覆盖 Metal 分路**。

Mode 2 的 `yyb16-final2` 输出 24 张 PNG、78 次槽检查、0 失败；Mode 0 的 `yyb16-final0` 输出 8 张 PNG、78 次槽检查、0 失败。这里仅归档 5 张代表原图。最终 Editor `build4`、Game `game-build2` 已通过；2,378 项保护基线中，2 个自有 V2 包按计划改变，2,376 项不变，无意外变化，新 Outline 为另加的自有资产。

这些是运行与来源检查，**图片数量、槽检查通过都不是终末地画质验收**。目前白布与翅膀的层次仍偏平，头发完整响应、皮肤 SSS、眼睛分层、深度 Rim 和后处理仍待迭代。场景也仍用现有舞台，并未完成终末地环境与调色校准。完整十项目标不变，后续依照[复刻流程审计](../../ENDFIELD_REPRODUCTION_REFERENCE_AUDIT.md)继续推进。

本页是 Editor 角色实拍归档，不把它当作 Cooked／发行包证明；相应证明应使用独立的实际构建和运行报告。

补充：[Cooked 原生后端正／负对照](COOKED_VALIDATION.md)归档两张未经处理的 payload 诊断原图，并说明协议 8、vanilla 回退、RenderDoc 绕行及尚未完成的分发验收边界；哈希与运行来源见 [cooked-sources.json](cooked-sources.json)。
