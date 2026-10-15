# 终末地原生着色后端

当前版本：`endfield-native-bsdf-v8-cooked-global-protocol`，2026-10-15。引擎范围仍为 UE 5.8.3 / CL58210709。**先读本节；下方折叠内容是 v4/v6 历史，不能当作本版行为或测试结果。**

## 当前架构与维护入口

逐灯 BSDF、GI／反射和 BasePass／packed payload 的位置沿用[图解指南](ENDFIELD_NATIVE_RENDERING_GUIDE.md)。本批没有把光照移到 BasePass 后的新前向 pass，新增的协议 shader 也不 dispatch。

- **9 个生成副本适配点**：SubstrateEvaluation、SubstrateDeferredLighting、DiffuseIndirectComposite、ReflectionEnvironment、ClusteredDeferredShading、Substrate、SubstrateExport、BasePassPixelShader、ToonProfileCommon。最后一个只附加 Cook 协议标记。安装引擎文件不改，副本只写工程 Intermediate。
- **材质数据**：v6 的单 Toon Skin / fmt1 方向与 quaternion 通道继续使用；Pattern24 + 预留 anisotropy32，24-byte 预算、真实各向异性、共享 basis、简化、透明与 fmt0 门控保持。没有新增独立 GBuffer RT，实际多写/读的 word 成本仍存在。方向场不是真正遮挡 SDF，局部 Morph 修正仍未完成。
- **直射响应**：Skin/Hair/Cloth 使用 RD RGB tint + RD alpha 明暗权重及现有 RS 近似；Mode0 原生透传，Mode1 阴影诊断，Mode2 自定义响应。v4 的“三档原型”不再描述当前响应。
- **分发协议**：CustomOutput 的名称、输出次序、默认值和 packed 约定由模块编译常量进入签名，不再要求用户磁盘上存在插件私有 `.cpp/.h`。USH 与引擎 hash 仍检查。[协议 shader](../Shaders/Private/MMDEndfieldNativeBackendProtocol.usf)的真实编译反射由[对应 FGlobalShader](../Source/Ue5MMDTools/Private/Rendering/MMDEndfieldNativeBackendProtocol.cpp)序列化。Cooked 启动初始禁止 Profile 路由，OnPostEngineInit 读取协议8才放行；未开后端 Cook 的记录为0。
- **证据边界**：全局 shader 协议只证明它见过适配头，`all_material_kernel_verified=false` 保持；材质和实际 GPU 行为另测。新增全局 shader 后，完全没有该类型的旧包须重新 Cook，UE 可能在插件验证前拒绝旧库。
- **专用描边**：自有 EndfieldV2 Outline 父材质，从当前 BaseColor 派生亮度/饱和度，复用原 PMX overlay、骨骼、宽度和恢复链。Opaque 忽略贴图 alpha；Masked 跟随当前实例 cutoff；透明以 .01 阈值近似。用户原 `M_MMD_Outline` 资源不改。独立光照、平滑描边法线、材质 Morph 实时同步尚未实现；动态改色可调用 Refresh。

调方程看 `Shaders/Core/TMMDEndfieldNativeBSDF.ush`；数据编解码看 `TMMDEndfieldNativePayload.ush`；改材质图/协议时同时维护 `MaterialExpressionMMDEndfieldNativePayload`、生成器图版本、模块协议与协议 shader binding。描边入口是 `MMDOutlineLibrary`、`TMMDEndfieldOutline.ush` 和生成器的专用父材质。详细启用与发行检查见[插件分发说明](ENDFIELD_PLUGIN_DISTRIBUTION.md)，公开实现对照见[参考源码审计](ENDFIELD_REPRODUCTION_REFERENCE_AUDIT.md)。

2026-10-15实际验证：Editor build4/Game game-build2、12包生成、旧模型描边与新YYB16th同机位对照通过。独立BuildPlugin三配置及文件检查通过；空白SM6宿主移出私有CustomOutput cpp后协议8、四父材质30/54/48/40 shaders且0错误。正/普通两次完整Cook均过；同一程序在正Cook读8，在普通Cook读0并保持原生回退。正Cook的Skin载荷开/关绿红实拍确认指定packed逐灯路径，不代表全部内核、角色动画/Morph或画质验收。见[角色实拍](Screenshots/2026-10-15-endfield-distribution/README.md)、[Cooked证据](Screenshots/2026-10-15-endfield-distribution/COOKED_VALIDATION.md)。本批实际运行范围为Win64/D3D12 SM6；首次帧RenderDoc注入异常及CLI绕行保留记录，未改用户Config。

<details>
<summary>2026-10-14 v4 / v6 历史开发与验证记录（非当前说明）</summary>

2026-10-14 开发记录。本后端用于实现用户指定的终末地复刻；下方 v4 记录是接入验证，当前新增 v6 画质迭代，不代表最终终末地画质验收。

## v6：脸部数据与真实 ramp 迭代

材质图版本4新增专用 CustomOutput。仅自有单 Toon Skin 的 Mode2／adaptive fmt1 不透明与遮罩路径接通：Pattern24 保存头空间漫反射方向 oct16 和准备后方向权重8；编译器预留 anisotropy32 保存头部旋转。最低请求／运行时预算24 bytes，不新增 render target 或绘制 pass。Toon 未消费的 F90 标记识别编码；读取后恢复中性 PatternUV、physical anisotropy=0 和原生标记，局部 `mmd_payload.valid` 保留。真实 anisotropy、非中性 UV、多层、低预算、简化和 fmt0 不写该通道。

与原生零anisotropy跳过extra word相比，自有Skin实际多写／读32 bit并解码，只是使用已有请求预算；不是零写入成本，未测性能。

自动 V4 保存完整头空间几何方向，四个 texel 分别解离散镜像再插值；V3 固定俯角阈值与 V4 字段／标记／缓存严格隔离。它不是遮挡 SDF，保留 UV0；没有数据时使用实时球面／几何回退。烘焙方向当前只随头骨刚性旋转，局部 Morph／下颌变形修正尚待实现。独立原生 Normal 供高光和 GI，方向场开关不改变该法线。

Mode2 的 Skin／Hair／Cloth 使用完整256点客户端 RD RGB，以及保存在 specular row alpha 的 RD 明暗权重。恢复无 LUT 的影色亮度／饱和、chroma tint、亮度补偿、`.96` diffuse 系数、`.04 × specular` F0 与保留波瓣幅度的 GGX 高光。Skin 代表样本未启用 RS；Hair09／Cloth04 RS 暂取 V=.5／.696 的真实固定切片。头发主波瓣使用几何竖向切线，缺少原作 P／flow／split normal 图时是近似；二维纵轴、Skin LUT、第二波瓣仍未恢复。

Mode0/1 恢复源 RGB；Cook Fixture 用 `MarkTransient(..., false)` 保存源曲线载体，再由运行时按模式创建瞬态曲线，避免 Mode2 数据固化到基线。Mode2 明确拒绝 `ToonUnifiedDiffuse=1`，避免后段重复将 RD RGB 当反射率。验证进程使用 Profile atlas256；项目默认配置未改。

详细数据流、维护入口与后续范围见[入门指南](ENDFIELD_NATIVE_RENDERING_GUIDE.md)。以下五入口／三档响应及打包结果属于本批之前的 v4 历史，不能当作 v6 已重新发行验证。

v6本轮实测：Editor build4/Game game-build2、11包generate2通过；fidelity2/3、Mode0/关闭后端、fmt0普通/聚簇合计109图/630槽0失败。实际packed绿/红、V4覆盖和多角度原图见[本轮归档](Screenshots/2026-10-14-endfield-fidelity/README.md)。侧光硬条块已减少，高光/GI对照脸区均差.113/255；未重Cook或验证发行包，完整复刻与变形支持仍按清单推进。

第一次接触这套方案，先看[图解入门与维护指南](ENDFIELD_NATIVE_RENDERING_GUIDE.md)。本文保留具体接口、配置与验证记录。

## 实施方案

插件在 `PostConfigInit` 核验 UE 5.8.3 / CL58210709 和固定私有 shader 接口，将安装引擎的 `Shaders/Private` 完整复制到工程 `Intermediate/MMDEndfieldNativeBackend/<签名>/Private`，只修改这个工程缓存中的入口。通过公开的 shader 目录映射 API 注册 `/Engine/Private`，安装引擎的源文件不写入。完整目录镜像是必要的：更深的映射不会对缺少的文件自动回退。

这提供自定义逐灯光 BSDF 和 GI／反射合成入口，着色逻辑保存在插件 `.ush` 的 struct 成员函数中。现阶段使用原生 Toon 的材质存储格式，响应方程可以在插件中编写；没有新增引擎 ShadingModel ID，也没有改原生材质缓冲格式。它依赖固定版本的私有 shader 接口，不能当作跨 UE 版本稳定的官方自定义 ShadingModel API。

当前镜像包含 1,146 个 Private 文件，约 12.6 MiB；只适配其中五个入口，缓存不入库。源文件和最终适配文件共同进入签名，缓存按签名隔离并原子发布。版本、精确锚点、源 hash 或已存在缓存不匹配时停用；首次开启会产生新的全局／材质 shader 编译开销，尚未进行性能基准测试。

只有插件标记的终末地 Profile 启用新响应。Profile 漫反射 ramp 的 alpha 两端保存角色签名，中间平台保存模式及 GI 倍率，RGB 保持原样；运行时使用瞬态副本，源资产不写入。未标记 Toon、Default Lit 和关闭后端的工程沿用原生路径。签名不匹配、目录映射冲突或固定源 hash 不符时拒绝启用。

## 数据入口

- `TMMDEndfieldNativeBSDF.ush`：逐灯光收到完整 BSDFContext、原生 FShadowTerms、FAreaLight，以及 Context 中的插件灯光数据。`TMMDEndfieldNativeLightCapture.ush` 在桌面延迟和补充的传统聚簇入口保存原生灯色、方向、位置、半径、衰减、聚光参数、光源尺寸、矩形灯及 atlas 参数、灯函数索引、接触阴影设置、预计算阴影与当前 attenuation 等 50 项数据。只有捕获完成时 `mmd_light_data.valid=true`；其他 Context 构造路径为零占位，透明前向的额外 LightData 捕获尚未实现。引擎外层已经乘灯颜色、LightMask 和 diffuse/specular scale，BSDF 内不能重复乘。
- `TMMDEndfieldNativeLegacy.ush`：补上桌面传统 GBuffer 的聚簇灯光路径；原生透传模式完整委托原函数。自定义路径使用真实 LightData 和原生阴影，避免外层再次乘表面阴影。移动端仍委托原生，未实现自定义移动后端。
- `TMMDEndfieldNativeIndirect.ush`：在 adaptive GI、传统 GBuffer GI 和独立天空／反射合成阶段收到原生辐射、AO、曝光、材质权重及该 pass 的最终 diffuse/specular 贡献。倍率在最终贡献上处理，不改 Lumen bounce albedo。`available_flags` 明确哪些字段实际可用。

这不是所有阶段共享的一次函数调用。逐灯光入口没有最终 Lumen 颜色，GI 合成入口也没有某一盏灯的阴影。当前没有接出天空／capture 在内部乘 BRDF 前的原始辐射；原生 Toon 的独立 SSR 权重为零，非零自定义 SSR 重写尚未实现。取得的是当前 pass 的贡献，不能冒称完整最终 framebuffer。

引擎的灯光剔除、完全遮挡早退、矩形灯可见性和 fast path 的 NoL 早退仍生效。原始材质纹理、全部 UV、顶点色与骨骼数据不会自动进入延迟入口。这里可以替换已进入入口的灯光响应，不能据此宣称整个 Renderer 的处理规则都已自由重写。

## 启用与验证

默认关闭。测试使用 `-MMDEndfieldNativeBackend`，并在启动时开启 Substrate；不修改工程默认渲染配置。

- `-MMDEndfieldNativeBackendMode=0`：原生保真透传。
- `-MMDEndfieldNativeBackendMode=1`：阴影诊断，RGB 分别显示表面阴影、透射阴影和 Toon 透光率。外层灯色、曝光和其他 pass 仍会参与合成。
- `-MMDEndfieldNativeBackendMode=2`：自定义三档响应原型。
- `-MMDEndfieldNativeIndirectDiffuseScale=<0..8>`、`-MMDEndfieldNativeIndirectSpecularScale=<0..8>`：写入 Profile alpha；保真模式不应用这些倍率。

也支持 `[Ue5MMDTools.EndfieldNativeBackend]` 中的 `Enabled`、`ProbeMode`、`IndirectDiffuseScale` 和 `IndirectSpecularScale`。这些是新建瞬态 Profile 的配置，修改启动参数需重新应用 Profile；模式变化不需要重编 shader。

`MMDEndfieldNativeBackendProbe` 核验实际目录解析、所有 patched 文件及插件源 hash、真实材质绘制 shader 和截图。`MMDEndfieldNativeBackendFixture` 只生成专属验证目录的 Profile、材质和地图。地图中的验证 Actor 仅在显式 `-MMDEndfieldBackendValidation` 游戏运行中重绑专属测试材质、截取游戏视口并退出，允许同一个 Cook 产物切换模式作对照。

Cook 时必须启用后端，确保 shader 库中包含插件内核。打包运行没有 shader 源映射；启动参数和 CPU Profile 元数据本身不能证明内核存在，需要 Cook 来源记录和实际图像对照。

## 实际验证（2026-10-14）

- UE5.8.3 / CL58210709：Editor `build8`、Game `game-build3` 实际构建通过。Editor r3–r8 分别验证模式 0／1／2、关闭后端、关闭两路间接光，每轮 11 图与 70 次材质槽检查通过，三种绘制材质 shader 为 21／40／29，无默认材质或代理 fallback。模式 0 对关闭后端的间接脸视图平均 RGB 差为 0.0429（0–255）；结果并非逐像素相同。
- 传统 GBuffer=0、聚簇灯光开启的 r11 另完成 11 图／70 槽检查，四类灯光的 clear／blocked 原生阴影诊断通过。此前 r9 和关闭后端的 r10 都在相同 VS／PS hash 处触发 PSO 创建失败；原因是 Substrate tile VS 输出与传统聚簇 PS 输入不匹配。v4 只在工程缓存中适配这处接口，安装文件不改。Editor 累计 77 图／490 槽检查；本批使用 VSM，没有验证 CSM 或移动端。
- UAT `package3` 完整 Cook、Stage、Pak、Archive 通过；Cook 已处理 699 个包。最新 Game 二进制通过 `package4 -skipcook` 重新 Stage／Archive，打包 exe 与本地构建 SHA256 一致。Cook 使用 v4 签名 `E3E3466769C5FCEEDE7D0860A85BF3829288DA93`；r3–r8 为 v3，仅聚簇接口修复不同。
- 同一 Cook 产物的 `final0/1/2/6/7` 五次后台 D3D12 SM6 游戏运行均 exit0：12／12 专属槽重绑，CPU Profile 控制读回匹配，第 120 帧实际游戏视口截图 1280×720，第 160 帧报告并退出。阴影诊断、漫反射间接光归零、镜面间接光归零对模式 2 的全图 RGB 平均差分别为 8.1818、7.5538、5.1399；同模式重复运行差为 0.4027。Cook 来源与大于重复波动的实际图像变化共同证明本批内核消费了运行时控制；CPU 报告单独仍标记 `compiled_kernel_verified_by_this_report=false`。
- 全部安装引擎 shader、工程／插件原 Content、Config 与用户 Face 的 2,378 项 SHA256 保持，新增的 13 个测试包及 manifest 已移入 Saved 备份，不随插件提交。未改工程默认 Substrate、未提交／推送。少量实际截图和来源见[本日归档](Screenshots/2026-10-14-endfield-native-backend/README.md)。

失败记录保留在 `Saved/Phase2Reference/Endfield2/native-backend-2026-10-14/`：最初构建虚拟内存不足；首轮 shader 的移动端宏条件、探针实验白名单已修；r2 被中断。首次 Cook 因 `SkipCookingEditorContent` 排除了 Landmass 所需资源失败，第二轮 Cook 已过但 Zen staging 记录 404，最终用本次命令行 `SkipZenStore` 完成。首次打包 `game0` 在第 1 帧返回 `0xc0000374`，无 UE fatal／调用栈，原因未定位；相同命令的 `game0r` 和最终 `final0` 均成功，不能将一次异常归因于模式或宣称已修复。测试未启用移动端自定义后端，也未证明所有渲染路径。

## 尚未完成

旧阴影 RT 子系统和灯光 RT 写入目前仍在后台运行，新后端不依赖这些纹理。后续需要按实际旧材质需求启停；停止重复渲染之前不能宣称已经节省这部分性能。维护调用位置见[旧 RT 的重复开销](ENDFIELD_NATIVE_RENDERING_GUIDE.md#处理旧-rt-的重复开销)。

头部方向、自动 SDF、独立高光法线及更多逐像素材质数据仍需正式传递到延迟 BSDF。当前独立法线函数接口仍传入相同的原生法线；不能据此宣称已经接通独立高光法线。

原生阴影标量不保留 caster 身份，不能直接分离鼻子、刘海和场景遮挡。旧阴影相机 RT 是给 Unlit 手算光照补遮挡的辅助深度，不等于原生多灯阴影，也不能用一张阴影图替代合理的自阴影控制。SDF Version3 的侧向阴影错误仍待修，专用 UV／几何烘焙方案需在本后端数据传递稳定后继续对齐。

继续实现时，逐灯光方程写在 `TMMDEndfieldNativeBSDF::EvaluateToonResponse`，灯光输入从 `bsdf_context.mmd_light_data` 取得，最终阴影从传入的 `shadow_terms` 取得；间接光和反射在 `TMMDEndfieldNativeIndirect::Process` 中处理。正式的脸部／高光 payload 必须另做存储与解码，不能靠这个 Context 的临时字段跨 pass 传递。未调用的旧 `ApplyEnvironment` helper 不是当前合成入口，未来接用需补模式 0 的保真门控。

真实客户端 ramp、头发高光环、眼部分层、深度 rim、描边和后处理仍按 `ENDFIELD_V2_FOR_GPT.md` 的十项范围推进。本批完成原生后端与发行验证，三档原型和诊断图均不代表最终终末地画质。

</details>
