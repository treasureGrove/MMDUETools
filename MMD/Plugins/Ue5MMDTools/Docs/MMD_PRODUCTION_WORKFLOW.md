# 后台制作 MMD 与重开编辑

更新日期为 2026-11-05。后台制作使用 UE 离屏渲染、正式 VMD 导入与 Bullet。原生风格的条件见[当前架构](RENDERING_ARCHITECTURE.md)。

## 从面板制作

保存角色网格、骨架和自选动画后，确认当前动作、内置风格、展示场景和透视机位，点击 **按当前预览制作**。该入口适用于 Windows D3D12 SM6，需要系统能找到 `ffmpeg` 和 `ffprobe`。可直接使用工程 Content 内的模型，内置动作则由插件随附的 VMD 在后台重新导入。

后台固定为 30 fps，画幅匹配预览区，从动作第 0 秒开始。沿用预览倍速、物理开关，以及 Substrate、原生后端和阴影设置。支持八套角色展示场景，以及使用原材质或已保存的内置风格。定格、烘焙预览、完整舞台、独立 LookDev、自定义材质覆盖需先切换到受支持的状态。

制作完成后，在状态栏点 **播放成片** 调用默认播放器，或点 **打开目录** 查看 `Saved/MMDProduction/` 下的帧、序列和日志。面板保留最近一次任务的入口，换场景或镜头后也能继续使用。点击 **取消** 会保留已有文件。任务失败或取消后，有输出目录时可直接打开排查，失败提示会显示可用的阶段和原因。

## 使用脚本指定素材

插件提供 [CreateMMDMovieBackground.ps1](../Scripts/CreateMMDMovieBackground.ps1)，宿主仍可使用 [Scripts/CreateMMDMovieBackground.ps1](../../../Scripts/CreateMMDMovieBackground.ps1)。指定 `-ModelAsset "/Game/.../MyMesh.MyMesh"` 可直接使用已保存的工程网格，或使用 `-ModelDirectory` 选择当前工程 `Saved/BackgroundValidation/ImportedAssets/` 中的旧缓存。动作可选 `-Motion` 或同骨架的 `-AnimationAsset "/Game/.../MyAnimation.MyAnimation"`。以下路径需替换为本机素材，输出目录须使用新名称。

```powershell
.\Scripts\CreateMMDMovieBackground.ps1 `
  -EngineRoot "D:/software/EpicEngine/UE_5.8" `
  -ModelDirectory ".\Saved\BackgroundValidation\ImportedAssets\MyModelCache" `
  -Motion "D:/MMDSource/Dance.vmd" `
  -Face "D:/MMDSource/Face.vmd" `
  -Camera "D:/MMDSource/Camera.vmd" `
  -Audio "D:/MMDSource/Music.wav" `
  -OutputDirectory ".\Saved\MMDProduction\MyMovie" `
  -Preset InfinityNikki -Stage Studio -Width 1280 -Height 720 -Fps 30
```

`Face`、`Camera`、`Audio` 可省略。追加表情按名称替换原动作中的对应表情；未提供独立相机时使用动作内的相机，仍没有相机才使用自动全身机位。独立相机读取 VMD Bezier、距离、旋转、视角及切镜，模型与镜头统一使用导入尺度。未指定 `-Stage` 时默认恢复为纯白影棚（`Studio`），保留原有浅色渐层与简洁圆台。

`-Stage` 提供八套角色展示：默认 `Studio`（纯白影棚）；新增 `RoseStudio`（樱雾柔光：柔粉背景与细边圆台）、`SlateGallery`（深蓝展厅：深蓝哑光空间与两侧柔光板）、`PearlAtrium`（珍珠回廊：珍珠白空间与偏侧弧墙）、`LilacStudio`（薰紫摄影棚：浅紫渐层、宽背板与低矮侧台）。早期 `SkyGlass`、`VioletStage`、`WarmAtelier` 三套云空仍接受显式参数，面板中收进默认关闭的「早期云空场景」；该版外观已被用户否决，不再作为默认。

八套展示与面板共用几何、材质及对应灯光配方，全部固定 EV0；兼容 LightDataRT 与原生场景灯使用同一环境 ID。新增影棚不使用新的生成图片或 Fab 资产；早期天空 PNG 仍仅为艺术背景，来源保留在[天空资源说明](../Resources/DisplayScenes/README.md)。`report.json` 记录实际 `stage_preset`、`lighting`、`exposure`。原有完整舞台 ID 仍可用，但电影中保留既有的「舞台几何＋Studio 灯光覆盖」行为，不能据此视为完整舞台原配光照复现。

`-CameraMode Vmd` 为默认值，使用配套原相机。`-CameraMode Showcase` 根据当前头、躯干、手臂和脚部骨骼动态构图，每 6 秒在全身、半身与小角度侧视之间过渡。`-CameraMode Fixed` 使用指定的 UE 世界坐标、旋转和水平视角，需同时提供 `-CameraLocation`、`-CameraRotation` 和 `-CameraFOV`。

`-Duration 10` 可先做十秒试片。省略时优先按所给音频长度制作，并受源动作长度限制。输出支持 15、30、60 fps，启用物理时以 60 Hz 推进。`-Physics 0` 关闭物理，`-PlaybackRate 1.5` 以 1.5 倍速采样动作和表情。`-SkipBuild` 用于已经编译了当前代码的宿主。脚本隐藏后台 UE 窗口，完成渲染后合成 H.264 MP4，提供音频时写入 AAC 音轨。中途失败保留已有帧与日志。

脚本调用 `-run=MMDCreateMovie`，原生启动参数仅作用于该进程。直接调用插件内脚本时传入 `-ProjectFile`，工程已编译时可用 `-SkipBuild`。单独调用 commandlet 还需 `-AllowCommandletRendering -RenderOffscreen -d3d12 -sm6`，以及目标风格所需的 Substrate 和原生后端配置。

## 产物与继续编辑

作品目录包含 `frames/frame_000000.png` 起的 PNG 序列、`mmd_movie.mp4`、`request.json`、`report.json`、`production.json`、`video_probe.json` 和渲染／编码日志。`production.json` 只在脚本核对视频帧数与音轨后生成成功结果；画面观感仍需实际播放、检查切镜与脚底接触。

`Assets/` 另存 `A_MMDProduction` 和 `LS_MMDProduction`。动画保存逐帧骨骼姿态与源表情曲线，启用物理时包含 Bullet 的模拟结果。序列保存角色、动画片段、相机和 Camera Cut，引用源工程网格或源模型缓存。

在同一工程编辑器的控制台执行：

```text
MMD.OpenProduction "D:/projectGithub/MMDUETools/MMD/Saved/MMDProduction/MyMovie"
```

命令读取 `report.json`，校验源模型、作品目录和序列路径后打开 Sequence。重开时保留原工程资产或原缓存，以及整个作品目录。

重开结果可编辑已保存的物理姿态、表情及透视相机轨道，使用模型原材质；它不包含渲染时的瞬态暖暖 MID、展示舞台关卡或音乐 Sequence 轨道。MP4 音轨由外部编码加入。重开 Sequence 不能据此宣称完整复现最终影片的舞台和风格；正交相机也不在当前保存镜头的重开保证内。

## 默认画面与阴影

面板默认预览和后台未指定 `-Stage` 时均恢复为纯白影棚（`Studio`），保留原版浅色渐层背景、哑光地面与简洁圆形展示盘；Studio 远地面按相机水平距离在600–1500 cm内融入背景。各展示的近地面和展示盘继续承接角色投影。展示几何与材质为内存实例，不改用户材质资产。

默认 Studio 保留暖主光3.2、补光0.55、轮廓光1.25、天光0.45，固定手动曝光 EV0；其他影棚切换各自配套灯组，不改变默认影棚原有配方。2026-10-23 暖暖皮肤表面配方调整为 Tint `(1, 0.84, 0.75)`、TintStrength `0.35`、AlbedoScale `1`、Saturation `1.08`、Roughness `0.48`、Specular `0.35`；共享原生协议与逐灯入口保持 v15。当日重生十三个自有暖暖资产，六个父材质 SM6 编译通过；该历史结果不代表本轮重测或完整原作画质验收。

当前宿主 `DefaultEngine.ini` 使用 `r.Shadow.Virtual.Enable=0`，沿用 2026-10-23 在 YYB16th 模型上对照得到的常规阴影／CSM 回退，避开当时实测的 VSM 自阴影三角块并保留正常投影。本轮没有重测 VSM，这个历史结果不能推广成所有 UE 工程或所有 VSM 都有问题。后台脚本的 `-ConventionalShadows` 可在制作进程中显式选择常规阴影；不修改安装引擎文件。

持久化 `LE_Studio3Point` 已修复 World Partition／外部灯光包的保存遗漏，定向重建并独立重载检查通过；最终维护日志为 `Saved/MMDProduction/2026-10-23-studio-map-fixed/engine.log`。共用浅色展示几何由预览／制作 helper 创建，不应把关卡的灯光配方同步误写成已经保存了全部瞬态展示场景。

## 2026-10-25 影棚集合

第二批恢复纯白默认，并新增樱雾柔光、深蓝展厅、珍珠回廊、薰紫摄影棚。build2 成功，run2 六项 UI 测试全部 Success、0 Error、380 条 Warning。五套后台短片也分别输出 1 秒、1280×720／30 fps，共 150 帧；每套实际推进 118 次物理步进（含 1 秒预热），源资产 dirty 标记保持，动画与 Sequence 保存成功。Studio 调用省略 `-Stage`，报告确认默认 `Studio`／`Studio3Point`。

电影采用捕获组件局部 TAA，面板仍为项目 SMAA；每套保留 73 条未匹配骨骼／表情轨道警告。本批未另做新进程重开检查。五套代表帧未见缺面，但部分薄边仍有约 1–2 像素的断续暗采样，原因尚未隔离定位。五秒原速拼接、五套实拍及来源见[影棚集合归档](Screenshots/2026-10-25-studio-collection/README.md)；这些检查不代表用户审美验收或快速舞蹈抗拖影验证，下方云空版结果仅作历史保留。

## 2026-10-25 三套云空短片（历史，已被否决）

晴空露台、暮色星台、暖光画廊各完成 1 秒、1280×720／30 fps 的真实引擎制作，源目录为 `Saved/UIExperience/2026-10-25/movie-<ID>-final/`。每套 30 帧，实际报告的光照环境分别为 `DisplaySkyGlass`、`DisplayVioletStage`、`DisplayWarmAtelier`，固定 EV0；118 次物理步进包含 1 秒预热。源资产 dirty 标记保持，动画与 Sequence 保存成功；本轮未另做新进程重开检查。

电影捕获采用组件局部 TAA，预热 12 个捕获帧并保留首帧历史；面板预览仍使用项目 SMAA。每套报告成功，同时保留 73 条未匹配 VMD 骨骼／表情轨道提示。已检查代表帧，未据 1 秒待机短片声称快速舞蹈抗拖影或所有角色验证完成。实际画面、原速三段拼接与来源摘要见[2026-10-25 展示场景实拍](Screenshots/2026-10-25-display-scenes/README.md)；同日 run8 的六项 UI 测试结果见[交互验证](Screenshots/2026-10-25-ui-experience/README.md)。这一批云空版在生成上述实拍后也被用户否决；保留文件与通过记录仅用于追溯，不再作为当前默认或新增影棚效果。

## 2026-10-23 素材与验证范围

本地《红马》样片使用 YYB16th NoWing 缓存：498骨骼、314刚体、379关节。模型署名 SANMUYYB／霁行Ralker／Delta幻黑；动作与面捕「-不灵-」、编舞白小白、相机秋燚QY。随动作包附带的音乐仅用于本地预览，未据此取得或声称音乐商业／再分发授权。模型、舞蹈、表情、相机与音频均为本机素材，不随插件分发。

最终展示版目录为 `Saved/MMDProduction/2026-10-23-red-horse-showcase`，已实际输出95.5秒、2865帧、1280×720／30 fps，H.264＋44.1 kHz双声道 AAC，MP4为56,547,860字节。后台制作与编码共371.14秒；这是本次离线制作耗时，不是游戏帧率测试。已按5秒间隔检查全片构图，原始相机版保留在 `2026-10-23-red-horse-final`，部分近景会被该角色长发遮挡。

最终展示版在新进程中读回和评估 Sequence 通过，0 Error／Warning；时间基、资产物理数据烘焙、面板动作和预设检查均通过。独立重载默认 Studio 地图并做真实关灯／恢复验证也通过。[本批实际画面和验证来源](Screenshots/2026-10-23-mmd-production/README.md)记录准确捕获时间和本机产物；第三方动作与角色仍需逐模型检查接触、穿插和相机适配。

`Ue5MMDTools.Creation.ProductionReload` 使用 `-MMDProductionDirectory=<作品目录>` 在新进程读回动画、Sequence 与原缓存引用，并实际评估首帧、中段和末帧的角色、镜头及绑定；无参数时警告跳过。该检查可用 NullRHI 运行，用于验证保存结果，不能替代影片目检。面板操作与内置动作说明见[工具面板指南](TOOL_PANEL_GUIDE.md)。
