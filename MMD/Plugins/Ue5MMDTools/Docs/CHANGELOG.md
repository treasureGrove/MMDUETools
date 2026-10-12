# 修改记录

每批功能、修复和资源调整在这里追加日期、具体变化、验证结果及必要限制。渲染改动同时链接实际引擎截图；截图捕获日期和文档归档日期分别记录。

## 2026-10-12 — 二期收尾与用户接手

- 更正规则修改范围：用户指的是本机 Codex 全局 `C:/Users/admin/.codex/AGENTS.md`，已撤销此前误改的 `MMD/AGENTS.md`，恢复项目原模型路由及截图归档约定。全局规则改为授权范围内自主完成普通远端操作，无额外 push 确认或强制个人分支；全局文件已本地备份，不纳入项目仓库。本次仅修改规则与记录，未改 shader 或运行 UE。
- 新增[交接说明](PHASE_TWO_HANDOFF.md)及[逐 shader 审计](PHASE_TWO_SHADER_AUDIT.json)。相对一期基线核对全部已跟踪 shader，既有改动仅 Reference BRDF/Character、GardenSurface、StageBackdrop 四个；新增两个 EndfieldPhase2 shader。明确 `TMMDReferenceBRDF.ush` 有修改，不能笼统声称所有 `.ush` 未动。
- `MMDToonLighting.ush`、Archived、MMDStandardCore、实验版 lighting 和用户 Face 均未修改；原文件 SHA256 与 10 月 11 日冻结值核对。旧主 691 文件及新四源码仍与最后真实验证的冻结值一致，未继续调 shader。
- 交接源码、研究/失败记录和代表截图；独立四资产仍留在私有验证工程，主 Content/DLL 未发布。文档说明编译与定向生成入口、共享变更影响及后续未完成项，当前画质仍未达标。另一个任务的 Mobile 文档不纳入提交。
- 10 月 12 日仅做收尾文件/文档/Git核对，没有新 UE 运行；所有实拍保持真实 10 月 11 日日期。按用户授权准备独立 Git 交接分支与 dry-run，实际推送须按本聊天 AGENTS.md 第 3 条确认后执行；不删除分支、不 force、不动 stash。

## 2026-10-11 — 独立 Hair/Cloth/Skin 实现与真实输入归因（画质未通过）

- 22:49–22:50:19 SGT 发带候选只在专属 `EvaluateHairBand` 加 shifted-normal 方位门控，保持灯光/输入/其他 shader 不变；Build/Generate/两组三视图成功，精确重写三parent，旧691/496/preset保持。归档[两张实际原图](Screenshots/2026-10-11-phase-two-endfield-hair-azimuth/README.md)；只略减部分亮区，长亮轨、灰脸基本不变，视觉目标未达，不称发带已修复。该门控仅减能，仍是艺术近似。
- 在两个专属 shader 中分离 Hair 宏观/镜面细法线、发流与双带偏移；Cloth 将 cavity 限于漫反射、明确 F0 上下界、独立三段受光与 GGX/纤维混合；Skin 使用归一包裹光而非仅改 toon 阈值。新直射响应继续乘真实灯辐射与 CSM，未改用户 Face 或共享 shader。详见[实现与实测记录](PHASE_TWO_ENDFIELD_DEDICATED.md)。
- 21:50 首次诊断宿主构建失败，保留失败证据后另开批次。21:54 私有 Build/Generate 成功，只重写三个专属 parent，preset 与旧 496 包不变；三实拍仅有小幅变化，实际游戏资源完整 map 九项检查失败，不记为通过。
- 查明 UE 游戏模式 shader job DDC 与完整 material-map 状态差异，Saved 宿主改为启动阶段完整编译；无引擎修改。22:32:39–22:34:04 后台 Build、Normal3、FlatHair3 均 exit 0，两组三视图检查通过，三个 parent/35 MID 完整 SM6 资源、四级阴影、156 物理步/178 刚体及最终 PP 读回通过。本批复用真实 Roles3 Generate，未写资产。
- [归档三张实际原图](Screenshots/2026-10-11-phase-two-endfield-input-attribution/README.md)：正常底图、平色 RGB 加原 Mip0 alpha 的发丝对照、正常衣料。底图手绘细亮纹确有贡献，但平色后程序性宽长亮轨仍在，不能将问题全归于底图；脸部灰带与衣料层次仍不合格。临时平色纹理只有单 mip，不能证明原压缩/mip链与逐LOD透明边缘等价。
- 旧主 691 文件、旧 Content 496、独立四资产按阶段哈希保护；本机少量截图和摘要，不归档测试角色、游戏资产、Saved 或二进制。没有前台、主Content/DLL发布、云上传、提交、推送或 Git ref/stash 操作。运行检查通过不等于目标画质通过。

## 2026-10-11 — 确认终末地人物方向与独立二期入口（画质未通过）

- 用户已明确回答「终末地人物方向」，更新[目标范围](PHASE_TWO_PRESET_SCOPE.md)与[独立预设记录](PHASE_TWO_ENDFIELD_DEDICATED.md)。新建独立 Character/BRDF shader 和 `MMDEndfieldPhase2` commandlet，严格只写三种新父材质与一个 EndfieldPhase2 预设。旧全量生成路线继续撤回，旧七预设、十二父材质及既有源码改动保留当前内容。
- 完成所有三处 Custom 的依赖隔离与同步签名、44/17 输入和环境接线校验、显式预设参数与全属性检查。新 shader 首轮只是机械隔离 Revision4，不能记作外观改善；用户 Face 和公共 Lighting 未动。
- 20:59:30 SGT Saved 脚本静态解析及路径核对通过；root 静态预检修正保留变量名冲突后，于 21:02:50 SGT 重新冻结输入。691 旧主文件、496 旧私有 Content、原宿主/九二进制与历史中断证据受保护；新四源码独立列出。
- 21:04:40–21:07:05 SGT 经共享门完成后台有限执行：Build/Generate 真退出成功，三 SM6 父类与四包保存通过；Capture3 实际三图，但父资源状态合并检查九项、cloth第四级中心深度检查一项失败，整个 batch exit 1。旧主保护文件、496旧私有Content哈希保持；本批未通过运行/画质验收，原失败回执保留。
- 归档[两张21:06:58实际诊断图](Screenshots/2026-10-11-phase-two-endfield-dedicated-baseline/README.md)，来源/时间/状态/大小/SHA256全部记录。鼻部灰块、宽纵向发条和衣物平板感仍在；用户明确指出结果不合格，后续转向真实 Hair/Cloth shader与材质数据流实现，不再把验证链当画质提升。没有主Content发布、主DLL更新、云上传、提交或推送。

## 2026-10-11 — 唯一专属预设范围核对与撤回后续生成

- 按用户最新要求，仅推进准确的独立参考预设；钟离/终末地转写歧义未解决，依赖目标的源码修改、生成和 UE 验证停止。新增[预设范围与已有改动](PHASE_TWO_PRESET_SCOPE.md)，列出七套真实资产、family/父类、参考角色证据、关键假设、最小验证及换路线条件。
- 主七预设和十二 Reference 父材质共十九文件哈希与 Revision4 主保护清单全部一致；主 Sakura 仍 Genshin，私有同名 Sakura 为 Endfield，不能自动覆盖。未找到钟离专属命名或角色绑定证据。
- 报告已有六处生产源码改动的实际影响，包括跨风格共享 shader 和全量 Generate 会把旧樱庭分配 family 2 的风险；保留所有现状和用户内容，不擅自回滚。
- WorkflowProbe1 私有宿主编译日志成功后等待编译器辅助进程；保存原批准文件并撤回 RootApproved。原会话随后 Ctrl+C 中断、exit 1，18:49:40 SGT 确认包装父子已退出、三个 VCTIP 保留，无 UE/编译子进程。Generate/Native2/Map1 未执行；Hair4/PointSpec 等 Saved 草案保持未批准。
- 本轮没有 MMD released 事件、完整 Build 回执或 batch summary，不记为有限批次通过。Component 于 18:48:44 取得同一锁、18:48:58 释放，证明互斥锁已可用，不声称另一任务验收成功。不为补齐日志重启批次；本机中断证据保存在 Saved，十九受保护主资产保持原状。
- 后续完整截图/录像仅本机保存，逐文件登记真实时间、项目/源码/UE 版本、范围、通过/失败/未执行、大小与 SHA256；历史实拍与生成式目标不充作新验证。本批没有新实拍或云文件上传。
- 没有前台操作、主 Content 发布、提交、推送或 Git 分支/stash 操作。旧 Genshin/旧 Sakura 绑定作为诊断事实，不自动作为应修复项。

## 2026-10-11 — 二期完整参考与实际消费链核对

- 新增[完整工作流核对](PHASE_TWO_WORKFLOW_AUDIT.md)，固定三个社区参考提交，审查输入、预处理、材质/pass、环境和最终后处理；区分实际启用能力与仅存在的属性。不移植专属 SDF、角色资产或缺明确许可的管线。
- 核实主 Sakura 预设仍指向历史 Genshin 父类，主 DLL 是历史构建；隔离预设与 shader 成功不能说明主工程已更新。旧主视图宿主加载地图后另建 native 舞台，也不能替代真实保存地图的消费验证。
- 核实两条编辑器预览都未自动应用舞台 Reference 预设；缩略 SceneCapture 被当前灯光 view extension 跳过，全局 preview light override 未绑定 world。记录为独立交付缺口，不以简单替换材质宣称预览已接通。
- 背景恢复候选保留人物/灯光不变，恢复建筑/水岸映射并单独合成月亮；新增私有 Native2 / Map1 协议，准备读取实际保存的舞台、镜头、35 MID、source signature 与最终 PP。18:03 SGT 私有宿主 C++ 构建日志成功；随后发生辅助进程等待。本批已在范围纠正后中断（详见上条），生成及新截图未执行，不记为视觉通过。
- 本批未打开前台、未提交或推送；主 Content 未覆盖。新实际渲染结果完成后再归档截图和结论。

## 2026-10-11 — 樱庭主视图材质、舞台候选与真实差异诊断

- 更新[樱庭目标与诊断](PHASE_TWO_SAKURA_RENDERING.md)，区分 B 樱庭水镜生成式目标、终末地人物方向和实际插件截图。当前 Revision4 **视觉未通过**，不以技术检查数量作为目标达成证据。
- family 2 Hair 新增独立双偏移正弦幂带、切向正交/退化兜底和灯循环前屏幕足迹；Skin 按几何夹角保留鼻部受光，并在 V2 黑鼻回退后提高有限平滑。Cloth 降低宽镜面占比，湿地修正显式粗糙度被默认值覆盖的问题；用户维护的 Face、引擎文件和 BaseColor 最小输入约束保持不变。
- 樱庭生成默认改 family 2，主光 −32°/48°、Key/Fill/Rim 3.8/0.28/0.9、Ambient 0.12，收窄描边并统一 FilmSlope 0.86。新预设只在私有工程生成，主插件原有 496 个 Content 文件未覆盖；未将未通过的资产推广到主工程。
- 舞台前庭候选换成 51 块错缝湿石；花簇总数 1260→884，保留下游随机布局；中部两扇纸墙改格栅；背景映射调整使月亮入画。实际出现远岸建筑/水岸丢失和灰石偏平，明确记录为构图/材质回退。
- UE 5.8.3 有效主视图批次完成于 14:57:30（修改前）、15:30:35（V2）和16:05:23（Revision4）SGT，每组17 PNG；Revision4 构建通过、生成50/50及23个SM6材质编译通过。实际 shader/资产队列归零、GPU灯光/四级联读回、178刚体顺序推进2.6秒；后台未打包，无前台。基线与V2同时改shader/灯光，不能称单变量A/B。
- 保留 V2 原生成批次因 AndroidFileServer 自动改写私有配置而失败的原报告；精确恢复后独立 Recover3 捕获通过，Revision4 禁用该自动写配置模块后守卫通过。早先 DDC、动画压缩、就绪等待等修复属于验证链工作，不能当成画质提升。
- [归档五张代表性实际主视图与一张原生成式目标](Screenshots/2026-10-11-phase-two-sakura-v2/README.md)，逐文件 SHA256 及真实来源存入清单。实拍显示发片白亮减少、鼻部黑块有所减轻；宽亮带、脸部层次和暖湿地反射仍不足，关闭自阴影后鼻侧灰块仍存在。
- 未执行的主/副发带偏移交换候选先保存到 Saved，再仅恢复其两常量及一行注释到已实测 Revision4 BRDF；背景替代和 Aux6/Revision5 也只保留草案。下一步改用固定镜头、灯位与镜面开关的有限对照，再决定算法或资产方向。源码、草案、构建产物和测试角色不归档入Git。本批未提交、推送或发布。
- 16:51 SGT 收尾只读复核：691个主保护文件、115个私有源输入、9个编译产物、88个私有shader均与Revision4冻结清单一致；47个本地文档链接及13张二期归档图哈希通过，Git diff空白检查通过。诊断归档期间没有新增UE运行。

## 2026-10-11 — GitHub 终末地解包方案复核

- 更新[公开参考与离线方法](PHASE_TWO_PUBLIC_REFERENCES.md)，固定 EndfieldStudio、Research Kit/AnimeStudio、Ruri 工具链的审查提交，区分容器读取、材质与编译程序恢复、独立材质重建及私服后端。
- 核对正式版 Shader 尾部解析差异：Variante 具有 LOD/Blob、HG type33/DXBC、参数及 descriptor 恢复路径，与本机既有 TypeTree 结果更匹配。仅源码审查，未重新运行导出，不宣称资源版本24577065完整兼容或原始 HLSL 恢复。
- 新确认 RuriRipperImporter 公开的终末地 Blender 材质库、Substance GLSL 与输入映射清单，补充此前只查到生成器描述的记录；同时记录作者列出的屏幕阴影、雾与管线功能缺失，不将生成物称作完整原作渲染。
- EndfieldUnpacker 当前 GitHub 访问返回 HTTP451，只取得搜索缓存的作者说明，保留为未核验线索。没有下载或运行新解包器、挂接游戏、启动 UE、修改 shader 或新增截图。本批验证为主仓库页面/代码/提交核对和文档链接检查；没有提交或推送。

## 2026-10-11 — 二期视觉验收纠正

- 用户指出当前画面明显不像终末地，接受此判断：上一批只做了小幅通用材质修正与运行验证，**未达到终末地外观目标**。47项通过、23个材质编译成功不能当成视觉验收。
- 当前四套 Reference 方向共用 `TMMDReferenceCharacter` 与 `TMMDReferenceBRDF`，由风格参数和材质角色调整；不是四款游戏人物 shader 的完整实现。Endfield 命名只标识目标方向，不作为复刻完成的证据。
- 复核实际 `stages.json` 和 `ReferenceFamilyFor`：已展示的 SakuraTheater 截图绑定 `reference_family=0`，即 Genshin 方向父材质；Endfield 为 `reference_family=2`，对应 IndustrialAnime。上一批虽然编译了四个方向的材质，但只捕获 SakuraTheater，没有独立验收 Endfield 画面。更正截图标签，保留真实历史图和来源。
- 实际看图记录：头发有大块浅色、沿发片延伸的高光；脸颊/额头明暗接近，鼻部体积不易读；布料响应宽而平，分材质层次不足。景深使近景手部、肩胸和部分衣物变软，现有图不能证明微法线或织纹改善。可能根因需在固定曝光、关闭景深的对照中验证，不能仅按截图推定。
- 后台浏览器已查看[官方庄方宜战斗演示](https://www.bilibili.com/video/BV1woQ3BvEq6/)的少量人物近景及展示/战斗片段，确认页面标题与发布账号；未下载或归档原视频，菜单显示自动 360P，压缩和特效干扰不适合微法线或定量像素对比。
- 2026-10-11 10:54:55–10:55:54 SGT 经共享门禁完成一次正确目标的后台捕获：实际 Reference_Endfield / family=2 与三种父材质核对通过，57 项运行检查、23 个 SM6 材质、11 组/52 张 PNG成功；景深与运动模糊以启动阶段真实 GetString 读回 0 确认。没有修改 shader 或主 Content，496 个主资产、88 个 shader 和184个输入源文件哈希前后未变。归档[两张实际 Endfield 方向问题基线](Screenshots/2026-10-11-phase-two-endfield-baseline/README.md)，来源为插件测试角色，非原作实拍。
- 对这组三张实际 Endfield 近景看图后，**仍判为未达到目标**：头发仍主要沿发片形成纵向亮条，衣物黑灰渐变宽而平，脸部层次不足。本批纠正验证对象与消除景深干扰，没有新增外观提升。SceneCapture 仍缺主视图自定义 CSM；使用兼容历史 DLL，未重编或打包。后续需要主视图自阴影、镜面/填光分离和局部衣料粗糙度对照，不用检查通过数量代替视觉结果。

## 2026-10-11 — 二期公开参考核对与首批通用材质修正

- 用户反馈工具检测后停止 RenderDoc 抓帧，转向[公开资料与离线方法](PHASE_TWO_PUBLIC_REFERENCES.md)。查到社区人物材质配方、描边分析、MME 独立复现及公开反编译后改写样本；区分作者旧报告、重建源码与原始数据，尚未取得本机人物 shader 与画面的可靠绑定。
- EndfieldStudio 源码构建及 VFS 列表实测成功，识别资源版本 `24577065`；随后定向读取 manifest、四类 CharacterNPR shader bundle，以及庄方宜 root 与七个材质 bundle。347,942 个 asset hash、254,722 个 bundle hash 和字典关联重放一致；索引 CRC 已检查，另记录副本 SHA256，不宣称逐 payload CRC 或当前 GameAssembly 认证。
- 按文件内实际 TypeTree 解析四类 Shader 元数据，并以 external CAB/pathID 精确核对七个 Material→Shader 引用。Prefab 52 个 renderer/55 个槽中仅 10 个槽解析到这七个材质，不称完整角色导出或当前帧绘制认证。审查发现第三方 RDEF reader 的 slot/count 字段错位，记录限制，不照搬未经复核的槽位输出。
- Hair 的 D3D11 目录与首代码段解压长度匹配，从中定向保留两个真实 Pixel DXBC，均成功离线反汇编与 HLSL 重建。各用独立 DXC 编译一次：242 通过，184 因 TEXCOORD1 语义重叠失败，保留实际结果。完整 HG 程序分组直接把 Blob184/242 与 LOD600 ForwardLit 及参数70/25关联，关键词和 Pixel stage 独立吻合；没有 RDEF，不能从反射恢复名称，也不声称原始 HLSL、当前帧变体或画面等价。
- 后续补查常态 Hair126、透明 Hair131 与 Cloth572/577：六份参数表覆盖全部 34/32/32/33/34/36 项 ASM 资源声明，无遗漏或冲突。确认 SplitNormalMap 的 RG/BA 分别重建表面/镜面法线，LineMap 只采 R 调整条带；常态与 dither 的遮罩方向不同。Hair 为偏移方向的正弦幂高光带，区别于现有双瓣 GGX。Cloth 从 MRO 贴图 G/A 驱动镜面与粗糙度，并乘 SpecRamp；存档 `_Specular=0` 不门控所选贴图变体。记录雨雪、环境反射、纹理像素与当前帧仍未证明的范围。追加样本只反汇编，本批未新增重建编译。
- 修正 Reference Hair 的双瓣各向异性高光对可选贴图强度的依赖，零细节强度仍保留几何发流；环境镜面系数统一按 Hair 角色处理。Reference Cloth 使用现有 `SoftSpecularStrength` 控制最多 30% 的中性宽 GGX 混合，宏观法线驱动，0 保留原 Cloth 路径。未修改用户维护的 Face、材质节点输入、已有粗糙度配置或主插件 Content。
- 2026-10-11 09:37:10 SGT 隔离工程实测：47 项通过/0 项失败、23 个 `PCD3D_SM6` 材质编译通过、12 张实际 PNG；日志 0 error/4 warning。178 个物理刚体有效，主 Content 496 个 SHA256 全部未变。归档[三张代表性实拍及来源](Screenshots/2026-10-11-phase-two-material-v1/README.md)。
- 默认画面变化很轻微，不宣称整体塑造感大幅改善。未实测所有可选贴图同时关闭；离屏自定义 CSM 走既有回退，未覆盖运行时 CSM 或打包。验证后核对引擎更新会话的哈希证据，确认 5.8.2/5.8.3 文件混合；该轮报告只作历史基线，本任务引擎进程已自然退出，当时后续发布复验等待修复，静态研究继续。没有提交或推送本批改动。
- 后续安装修复完成，按官方 manifest 独立复查16个关键文件 SHA1 全部一致，无 Pending/bps；范围为抽样关键文件，不是全引擎逐文件校验。2026-10-11 10:21–10:30 SGT 经共享门禁完成一次 UE **5.8.3/CL58210709** 离屏复测：47 项通过/0 失败，23 个 PCD3D_SM6 材质及12张实际 PNG通过，0 error/4 warning；引擎和子进程自然结束。主 Content 496 个文件、shader 88 个文件前后 SHA256 一致，含 Face。归档[两张新实拍及来源](Screenshots/2026-10-11-phase-two-material-v1-ue583/README.md)。
- 新引擎批次复测同一 MaterialV1 shader 实现，未额外更改外观。使用兼容的历史插件 DLL，未重新构建 C++、验证构建 SDK 或打包；离屏 CSM、全部可选贴图关闭等覆盖限制保留。跨版本像素差异不当成材质提升。只读审查记录后续 N/T 正交兜底、独立正弦幂带、几何镜面平滑的优先顺序，均未在本批实施。

## 2026-10-11 — 二期无账号离线 shader 可行性验证

- 新增[二期参考离线检查记录](PHASE_TWO_REFERENCE_PROBE.md)。用户没有游戏账号，暂停前台抓帧，改为只读检查本地终末地、鸣潮与无限暖暖客户端。
- 暖暖未加密 IoStore shader 容器成功列目录并过滤提取两个库头；解析内容库 7,245 个 shader 条目和 Global 库 6,733 个条目。六组压缩代码的 BLAKE3-160 哈希匹配；三个内容库样本组解压长度吻合，五个 DXBC 成功反汇编，一份重建 HLSL 经 DXC 重新编译成功（未验证与原程序画面等价）。
- 终末地裸 Unity 资源可列对象，但标准解析器未完整解析 Shader/Material；鸣潮抽检 Pak 未能列目录。尚未取得人物材质绑定或原始 HLSL，不把泛用样本声称为人物 shader。
- 本批仅新增研究摘要与记录；原始客户端数据、反汇编和临时工具保留在被 Git 忽略的 `Saved/Phase2Reference/`。未修改插件渲染、运行游戏、抓帧或新增引擎截图；不复用一期旧实拍作为本次验证。

## 2026-10-10 — 手机端完整编辑器架构任务

- 新增[手机端 MMD 完整编辑器架构与执行任务书](MOBILE_MMD_EDITOR_ARCHITECTURE_TASKS.md)，按用户确认的 Android 首发、iOS 第二阶段定义模型/材质深度编辑、物理、工程保存、PMX/VMD 写出与本机带音频视频导出。
- 基于当前源码记录 Editor 导入链、Windows Bullet 库、SM5 灯光与桌面阴影路径、SDEF/QDEF 近似等阻塞；给出 `TG/mobile-editor` 本地分支建议、模块与接口边界、Agent 工作包、阶段门禁及功能/性能/发布验收标准。
- 根据用户单人开发的实际情况，将默认流程简化为同目录开发、各工作包串行接手和小批次提交；独立worktree、并行写代码与PR/MR按需要采用，逻辑边界不要求一开始拆成全部物理模块。
- 验证范围：源码与官方移动文档核对、任务依赖审查、Markdown 本地链接与 Git diff 检查。性能数值为设计目标，尚无手机实测；本次仅修改文档，没有新建/切换/推送分支，没有执行构建、Cook、手机打包或真机渲染，不新增实拍截图。

## 2026-10-10 — 恢复一期提交与文档归档

- 将 `feature/ue5.8-mainline` 恢复到一期提交 `add91e1d`。逐一核对的 224 个一期文件均与原提交一致；恢复后工作区干净，原 GitHub Desktop 配置 stash 保留。
- 新增本修改记录、[一期截图归档](Screenshots/2026-10-09-phase-one/README.md)，并在项目约定中要求以后每批修改同步更新记录。
- 归档七张已有的引擎实拍：舞台全景、人物全景、脸部与衣物前后对照、关闭描边对照。保留原始 PNG，不对截图进行重绘或调色。
- 本次只整理 Git 状态、文档和截图，没有修改渲染代码或重新执行渲染。下方验证结果及归档截图来自 2026-10-09 的一期验证。

## 2026-10-09 — 一期舞台与通用角色渲染

提交：[`add91e1d`](https://github.com/treasureGrove/MMDUETools/commit/add91e1d49097d2b03b394a4d04971a78a84e6ef)。详细使用方式见 [STAGE_RENDER_PRESETS.md](STAGE_RENDER_PRESETS.md)。

### 舞台与预设

- 新增霓虹演唱会、月光水镜、樱庭水镜三套动态舞台蓝图和地图，以及七套灯光与渲染预设。
- 樱庭水镜加入亭阁、水岸、花簇、石灯、落花与涟漪动效；支持编辑器预览、手动时间和运行时播放。
- 提供原神、鸣潮、终末地和白银之城四个 Reference 风格参数方向，共 12 个角色父材质；作为后续依据实际参考继续调整的基础。

### 材质、法线和阴影

- 按皮肤、头发、布料、金属与眼球分别响应实际场景灯光、SkyLight SH 和天空镜面。原模型最少只需 BaseColor，不依赖特定 UV 布局或配套脸部 SDF 阴影图。
- 可选 V2 发丝法线、褶皱法线和布料数据图采用物体空间三向投影。布料宽褶皱塑造漫反射，细织纹进入镜面；主着色与天空反射共用细节结果。
- `HairDetailStrength` / `ClothDetailStrength` 默认 0.20 / 0.40。强度为 0 时跳过对应细节采样；头发还需将独立的 `HairShiftStrength` 设为 0 才完全回退基础响应。
- V2 使用独立资产名，仅迁移父材质中旧默认纹理的精确链接，保留 V1、自选纹理及实例覆盖。
- 加入双瓣各向异性发丝高光、角色主光自阴影与接收平面 PCF 修正，减少倾斜表面的自遮挡条纹。
- 加入可恢复的细描边，按材质角色选择颜色和宽度，保留 PMX 边线开关、透明度、EdgeSize 与顶点 EdgeScale；恢复时还原原始 overlay 和 bounds。

### 播放、导入与模块

- 默认播放启用 MMD Bullet 物理；一期测试角色成功初始化 178 个刚体。
- 分离 Runtime 与 Editor 依赖，补齐后台导入、材质、舞台和打包验证流程。
- 修复 PMX 法线保留，以及按实际三角形区域判定贴图透明度；已有模型需要重新导入才能获得这些导入修复。

### 验证结果

| 验证 | 一期结果 |
|---|---|
| UE 5.8 后台舞台检查 | 51 项通过，0 失败；PCD3D_SM6 |
| 舞台/角色材质编译 | 23 个材质检查全部通过 |
| 打包主视图 | 35 次 1280×720 捕获全部通过 |
| 运行时资源 | 七套预设、三套舞台蓝图和三张 Cook 地图通过 |
| 状态恢复 | 原材质、自阴影标签及描边 overlay/bounds 恢复通过 |

打包画面覆盖四个材质方向、细节开关、灯光分离、自阴影及描边开关。报告源为本地 `Saved/BackgroundValidation/SakuraDetailV5/stages.json` 和 `Saved/BackgroundValidation/RuntimePackageSakura/runtime-{smoke,visual}.json`；仓库仅归档必要截图和来源摘要。

### 当前限制

- 材质仍在插件 Custom 节点内自算光照，未新增引擎 GBuffer ShadingModel；角色不自动获得局部反射捕获、Lumen/SSR、引擎 AO 或点光阴影。
- 预设细描边仅在透视视图生效；正交视图关闭，以免描边外壳进入自阴影深度采集。原 PMX 世界空间描边仍可使用。
- 通用法线不会改变模型轮廓或产生真实衣物形变；较大骨骼变形可能使细节游移。实时物理任意跳帧和倒放不保证确定性，稳定成片需烘焙或顺序预滚。

### 实际画面

下图为打包版主视图实拍，捕获于 2026-10-09。完整前后对照见[一期截图归档](Screenshots/2026-10-09-phase-one/README.md)。

![一期角色近景](Screenshots/2026-10-09-phase-one/face_after.png)
