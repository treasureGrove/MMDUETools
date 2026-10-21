# 终末地人物方向：二期独立预设

**历史快照（2026-10-19 标记）**：本文保留原批次的研究、状态与验证，内部“当前／下一步”和历史聊天指令不作为今日架构或执行约束。现状先读[当前架构](RENDERING_ARCHITECTURE.md)和[接手状态](ENDFIELD_V2_FOR_GPT.md)，原日期与证据保持。

日期：2026-10-11，时区 Asia/Singapore。用户已确认终末地人物方向；舞台延续此前选择的 B 樱庭水镜。该图为 2026-10-09 生成式目标，现有模型为插件 Miku 测试夹具，不是终末地角色或图中长外套资产。

## 实现与范围

新入口 `-run=MMDEndfieldPhase2 -Generate -Report=<本机 JSON 路径>` 只写四个独立包：

| 资产 | 路径 |
| --- | --- |
| 不透明父材质 | `/Ue5MMDTools/Stages/Materials/EndfieldPhase2/M_MMD_EndfieldPhase2_Opaque` |
| 遮罩父材质 | `/Ue5MMDTools/Stages/Materials/EndfieldPhase2/M_MMD_EndfieldPhase2_Masked` |
| 半透明父材质 | `/Ue5MMDTools/Stages/Materials/EndfieldPhase2/M_MMD_EndfieldPhase2_Translucent` |
| 渲染预设 | `/Ue5MMDTools/Stages/Presets/EndfieldPhase2/DA_Render_EndfieldPhase2` |

独立 shader 为 `Shaders/TMMDShader/TMMDEndfieldPhase2Character.usf` 与 `Shaders/Core/TMMDEndfieldPhase2BRDF.ush`。三个 Custom 节点分别覆盖主着色、共用表面细节、角色粗糙度；共同签名包含独立 Character、公共 Lighting 与独立 BRDF 的实际源码。检查 44 个主输入、17 个表面输入、输出类型/接线、环境镜面的法线与粗糙度来源。漏失或旧依赖直接报错。

首次实现只是将现有 Revision4 算法机械隔离，并固定角色测试参数，未新增视觉提升。旧七套预设、十二个 Reference 父材质、已有六处生产改动、用户 Face 与公共 Lighting 保留当前字节。详情见[范围记录](PHASE_TWO_PRESET_SCOPE.md)。未运行旧全量生成器，没有发布主 Content 或主 DLL。

## 本机验证状态

2026-10-11 20:59:30 SGT 完成 Saved runner 的静态解析与路径核对，未构建或运行。随后 root 复审源码、宿主差异、生成白名单、子进程等待和实际读回条件。21:02:50 SGT 重新冻结清单；静态预检发现 `$Host` 保留变量冲突，修正为专属路径变量，原脚本和原冻结清单保留。该失败发生在启动 UE 之前。

新清单保护 691 个旧主 Source/Shader/Content 文件，另列四个新源码；私有输入 117 个、shader 90 个、旧 Content 496 个、原二进制九个、测试 fixture 96 个。归档原私有宿主和二进制、前批中断证据与清单。仅允许在私有 Content 追加上述四资产。

后台有限链于 21:04:40 SGT 取得共享门，21:07:05 SGT 自然释放。Build 真实 exit 0（21:05:10 完成），Generate 真实 exit 0（21:06:34 完成），三父材质 SM6 编译及四新包保存通过；旧 496 私有 Content 与主保护文件哈希保持不变。Capture3 真实输出三图，运行进程 exit 0，但验证十项失败：三父材质资源合并检查每图三项失败，cloth 第四级阴影中心 patch 是清屏深度。整个 batch exit 1，只完成 Build/Generate，**未通过渲染验收**。不得将有图或引擎退出成功视为验证通过。

两张[新诊断实拍](Screenshots/2026-10-11-phase-two-endfield-dedicated-baseline/README.md)本机归档，捕获时间 21:06:58 SGT，原图未修改。预设路径、35 槽 parent/role/style/源签名、156 次物理与178刚体、Manual exposure/FilmSlope .86 和灯光读回有效。体积/脸部/发带仍未达到目标；用户明确指出这轮没有落实其思路，接受此评价。下一步重点改为实际 Hair/Cloth 数据流与角色材质实现，不以隔离预设或验证链当成画质成果。

读取同一角色姿态的全身、脸部、衣料；使用 native 舞台与现有资源，不生成地图或 BP，不计作保存地图工作流验证。保留 ApplyPreset 后实际 MID 的细节参数：HairDetail .20、HairShift .50、ClothDetail .40。当前 DataAsset 没有这些字段，来源是父材质默认值，不能写成预设直接设置它们。各服装槽实际 Roughness .62 / SpecularPower 64 / SpecularStrength .50；角色 BRDF 内另有 role 缩放。背景也包含此前未捕获的候选，这一首轮不能称与旧 R4 为单变量 A/B。

## 视觉验收与限制

收到画质反馈后，第二批只修改两个专属 shader，旧共享源码保持冻结。实际实现为：

- Hair：同一通用法线图分别提供过滤后的宏观漫反射法线与镜面细法线；shift 图只小角度旋转正交发流，另调双带位置/破碎度。头骨几何高度仅增加 ±0.15 的曲率偏移；主/次瓣保留独立 -0.24/+0.20 基线，避免同向合并成整圈头冠亮带。缺纹理/强度零时仍使用几何发流。
- Cloth：cavity 不再预乘底色，仅使漫反射变化于 0.92–1；宏法线控制独立抗锯齿三段受光，微法线控制 GGX，通用织纹分别调制粗糙度、0.020–0.045 的 F0 和 0.25–0.50 的纤维比例。Charlie/Neubelt 近似与 GGX 混合，不额外叠加白色亮块。三段曲线没有额外的 1.25 曝光增益。
- Skin：归一包裹光 `(N·L+w)/(1+w)^2` 真正进入漫反射，仍乘真实灯辐射和 CSM；不再仅移动 toon 阈值。用户维护的 Face 文件未动。

静态复审已修正 Cloth 过宽 ramp、未明确限制的 F0、Hair 两瓣基线合并风险。新增直射项在有效灯光 metadata 且零灯时归零；无效灯数据且天空/环境为空时保留既有兜底灯。以上已于第二批真实编译及截图中消费；仍只是自有的通用近似，未达到终末地画质。未实拍验证所有可选贴图同时关闭，不能将静态兜底检查称为该场景实测。

## 角色实现实测与输入归因

21:50 的 EndfieldRoleShading2 私有诊断宿主因 `VisualWorld` 未声明而构建失败，未运行 Generate 或 Capture。保留原失败后另开 EndfieldRoleShading3，修正宿主访问为角色所属 world；21:53:55–21:54:55 SGT Build、Generate 成功，只重写三个独立 parent，preset 与旧 496 包保持原 SHA。三张实拍中的新 Hair/Cloth/Skin 变化很小，鼻部和眉眼仍灰，衣料仍偏平。实际 SM6 game 材料资源为有效但未完成完整 map，九项资源检查失败；没有降低验收条件。

本机 UE 源码确认游戏模式的 shader job DDC 可以只编译按需 shader，不能用完整 map 标志直接替代实际 pass 判断。后续诊断在启动阶段设 `r.ShaderCompiler.JobCacheDDC=0`，使用完整 material-map 缓存/编译；仅更改 Saved 验证启动配置，不修改引擎。

22:32:39–22:34:04 SGT 新批次 EndfieldInputAttribution4 完成 Build → Normal3 → FlatHair3，均真实成功退出，两组六张实际图、三 parent 与 35 MID 的完整 SM6 map、四级联覆盖及物理/灯光/最终 PP 检查通过。复用 Roles3 的真实生成资产，不写新包或重写旧包。完整 map 政策也使背景正确消费完整 shader，因此本轮与 Roles3 不能称整幅图单变量 A/B；同批 Normal3/FlatHair3 才是固定协议下的 RGB 输入对照。

[三张实际图与来源摘要](Screenshots/2026-10-11-phase-two-endfield-input-attribution/README.md)记录正常脸部、平色 Hair 脸部和正常衣料。原 `Hair.png` 有手绘白亮纹；替换 RGB 后部分静态细纹消失，但宽长亮轨仍在，不能全归因于底图。平色仅逐像素保留源 Mip0 alpha，只有一个 mip，未复现原压缩/mip链；不用于最终画质或逐 LOD 透明等价验收，也未拦截 GPU draw call。脸部灰带和衣料塑造仍不合格。

22:49–22:50:19 SGT EndfieldHairAzimuth5 只在 `EvaluateHairBand` 添加 shifted-normal 方位约束，原带乘 `saturate(N'·H)^2`，其他 shader、灯光及输入不变。Build/Generate/Normal3/FlatHair3 成功，精确重写三个专属 parent，旧 691/496 和 preset 原 SHA；新签名 `d52b5c39c709b5485647dfc266fb860d`，完整 SM6 与运行检查通过。[两张新原图](Screenshots/2026-10-11-phase-two-endfield-hair-azimuth/README.md)显示只有少量亮度减弱，长亮轨、片间暗沟和灰脸基本不变。约束数值有效但没有解决主导形状问题；这是无积分能量补偿的艺术近似，不能称严格物理 BRDF 或发带已修复。

先确认真实消费，再按固定灯光和镜头比较发带、脸部受光与衣料。画质以实际图评判，shader 编译和读回成功只说明验证路径有效。脸部继续使用几何方案，不依赖角色 UV 或 SDF 阈值图；通用可选细节贴图可以使用。

生成式目标具有现有测试角色没有的长外套、大褶皱、眼部几何和发片形状，shader 参数无法生成这些结构。参考方向的材质改善与模型缺失分别记录，不宣称商业游戏精确复刻。

新截图和报告先仅本机保存，记录真实来源、捕获时间、UE 版本、范围、结果、字节数和 SHA256。当前没有云上传、提交、推送或分支/stash 操作。
