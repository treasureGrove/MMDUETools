# 终末地原生着色后端

2026-10-14 开发记录。本后端用于实现用户指定的终末地复刻，当前三档响应只是接入验证，不代表最终终末地画质。

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
