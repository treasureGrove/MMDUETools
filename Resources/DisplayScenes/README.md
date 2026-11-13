# 展示场景天空图片

生成日期：2026-10-25，Asia/Singapore。本目录三张图片由本项目通过内置 `image_gen.imagegen` 分别生成，用于二次元角色展示场景的可见天空与远景。没有使用参考图片，也没有从 Fab 下载或导入环境包；这些是生成式艺术背景，不能作为实际引擎截图。

| 文件 | 场景名称与 ID | 内容 |
|---|---|---|
| [sky_day.png](sky_day.png) | 晴空露台／`SkyGlass` | 开阔蓝天、低处白云与远方海岚 |
| [sky_twilight.png](sky_twilight.png) | 暮色星台／`VioletStage` | 蓝紫暮空、粉金薄云、稀疏星点与侧月 |
| [sky_warm.png](sky_warm.png) | 暖光画廊／`WarmAtelier` | 浅金杏色地平线、蓝紫上空与柔云 |

三张均为 **1774×887、2:1 PNG**，从工具生成目录逐字节复制，未缩放、调色或编辑像素。完整提示词、原始路径、生成工具、时间、尺寸、SHA-256 与初次目检记录见 [generation.json](generation.json)。原文件仍保留在生成目录中；使用和打包只依赖本目录副本。

这些 PNG 不包含 HDR 光照数据，也不作为天光 Cubemap 或物理辐照度来源。实际角色和近地面的受光由 `UMMDLightingEnvironmentLibrary` 中对应的三盏方向光与一盏天光配方提供；展示场景固定 EV0。可见天空由 `FMMDStudioDisplayScene` 的材质采样，远地面沿同一世界视线映射融入背景，实际 3D 地台承接角色投影。

生成请求采用完整等距柱状全景构图，但输出地平线未严格落在 V=0.5，源码分别使用约 0.13／0.06／0.07 的纵向偏移，并调整背景采样范围。它们是为展示构图处理的艺术贴图，未据平面目检认定为严格无缝、可用于任意方向测光的 HDRI。换镜头或展示比例后仍需检查接缝、天顶和地面衔接。2026-10-25 的 run8 已检查前／侧引擎画面，来源见[当日实拍归档](https://github.com/treasureGrove/MMDUETools/blob/dev/MMD/Plugins/Ue5MMDTools/Docs/Screenshots/2026-10-25-display-scenes/README.md)；这些机位的检查不构成任意方向无缝或用户审美验收的保证。
