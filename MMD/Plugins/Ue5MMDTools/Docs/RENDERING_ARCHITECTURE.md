# 当前渲染架构

源码核对日期：2026-10-19。本文是渲染路径的总入口；历史截图与测试结果保留各自实际日期，不代表本日重测。

## 终末地 V2：原生材质缓冲与逐灯 BSDF

终末地 V2 已接入 UE 5.8.3 / CL58210709 的 Substrate Toon。有效配置下，不透明与 Masked 材质在 BasePass 写入 Substrate packed 材质缓冲，延迟光照读取数据，调用插件 BSDF 计算当前灯的响应；原生阴影、间接光和反射参与各自阶段的合成。这是已有功能，不能把整个插件描述为 Unlit／LightDataRT 手算光照。

这里的“走 GBuffer”指原生延迟材质缓冲路径，实际使用 Substrate 的 packed 布局，不是增加一套插件 GBuffer RT。符合门控的 Skin／Face 数据利用现有字段及额外 word 跨 pass 传递。透明 Skin／附脸层使用 Surface Forward 内的 inline payload，不把透明路径误称为延迟 GBuffer 读回。

当前共享后端：`mmd-native-bsdf-v15-style-profiles`／Cooked协议15，Graph7、终末地Profile布局42、payload5、FaceData5、Frame1保持。v15新增暖暖独立风格路由，终末地原有响应分支保留；v14的实测记录仍按原日期引用，不能当作v15回归结果。

核对入口：

- [原生材质生成器](../Source/Ue5MMDToolsEditor/Private/Editor/MMDEndfieldToonPresetsCommandlet.cpp)：`MSM_DefaultLit`、`UMaterialExpressionSubstrateToonBSDF`、`FrontMaterial` 和专用 CustomOutput。
- [后端适配](../Source/Ue5MMDTools/Private/Rendering/MMDEndfieldNativeBackend.cpp)：BasePass／packed export/read、延迟与透明灯光、间接光／反射的工程副本适配。
- [逐灯 BSDF](../Shaders/Core/TMMDEndfieldNativeBSDF.ush)：消费当前灯和原生 `FShadowTerms`。
- [间接光合成](../Shaders/Core/TMMDEndfieldNativeIndirect.ush)：在原生合成阶段处理 diffuse/specular 贡献。
- [运行时选材质](../Source/Ue5MMDTools/Private/Stage/MMDStageLibrary.cpp)：依据 Substrate 能力、预设原生父材质与角色选择实际路径，不能只凭预设名字推断。

使用原生 Toon 需要开启 Substrate，并配置对应父材质；插件自定义 BSDF 还需要原生后端成功启用及匹配的 Profile。固定引擎版本、源 hash、映射和 Cooked 协议不符时拒绝自定义适配或保留原生回退。安装引擎文件不写入，私有 shader 的适配副本在工程 Intermediate 内；这依然是固定版本的私有接口适配。

完整用法与限制见[原生后端](ENDFIELD_NATIVE_BACKEND.md)、[维护指南](ENDFIELD_NATIVE_RENDERING_GUIDE.md)、[分发说明](ENDFIELD_PLUGIN_DISTRIBUTION.md)。原生接入不等于终末地完整画质已验收。

## Reference 与旧材质：Unlit 兼容路径

`M_MMD_Reference_*`、旧 Endfield 及部分通用材质仍使用 Unlit Custom，读取 `LightDataRT`、插件阴影 RT、天空 SH 和环境镜面。它们是仍存在的兼容路径，不代表终末地 V2 的原生架构。原生 V2 不依赖旧灯光／阴影 RT 取光，但旧子系统仍可能提交重复工作，不能因此声称已消除其成本。

暖暖最初实验误用了 SilverCity Reference Unlit 模板，视觉被用户否决；[旧实验记录](Screenshots/2026-10-19-infinity-nikki/README.md)保留证据。当前 `DA_Render_InfinityNikki` 已改为配置独立原生父材质和六类 Profile，共用已有材质缓冲、逐灯与 GI／反射入口；旧三份 Unlit 材质仅作非 Substrate 回退。暖暖不写入终末地 RD／RS，使用独立响应，见[原生暖暖说明](INFINITY_NIKKI_NATIVE_RENDERING.md)。迁移和画面验收分别记录。

后续暖暖等新风格应先复用或扩展已存在的原生材质、载荷和逐灯入口；风格公式与 Profile 可以独立维护，不能因名字不同就重新建立一套旧 Unlit 光照链并当作原生接入。

## 已移除或只供历史查阅的说明

旧 `ANIME_RENDERING_GUIDE.md` 描述的全屏 Compute 卡通重映射已没有对应源码，现已删除。它与 Substrate 延迟 BSDF 不是同一方案。

`PHASE_TWO_*`、旧路线图、旧交接和日期截图描述各自批次；其中的“当前”“下一步”不能用作今日架构或执行指令。最新状态从本文及具体功能指南读取，旧验证仅作为历史证据。

`TMMDAnimeFace.usf` 仍由用户维护。所有新风格保持仅底色可用与通用几何兜底，不要求模型自带固定 UV 的脸部阈值图；已有几何派生 FaceData 缓存的机制以[Face V5](ENDFIELD_FACE_V5_GUIDE.md)为准。
