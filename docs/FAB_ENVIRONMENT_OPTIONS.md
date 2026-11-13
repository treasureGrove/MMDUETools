# Fab 环境候选与本地可用资源

公开页面核对日期：2026-10-25。下列三项当日均显示 **Free**，仅查阅了 Fab／Epic 官方页面；尚未领取、下载、导入或验证 UE 5.8.3 兼容性。它们不是当前工具已经内置的预设。

| 候选 | 用途与范围 | 页面标记 |
|---|---|---|
| [MetaHuman Lighting](https://www.fab.com/listings/52f008f2-bfd2-4db1-b9f5-94c5b1512b8a)／Epic Games | 九套角色灯光，包括 Portrait、Split、Moonlight、Rooftop 等；适合作为影棚和城市 LookDev 的候选，不是完整屋顶几何场景。 | 免费，UE 格式；说明允许一般 UE 项目使用。 |
| [City Sample Buildings](https://www.fab.com/listings/008fe959-5511-428e-93bd-f99b1179f6d5)／Epic Games | 超过 2,000 个建筑模块，可择取少量作为屋顶远景；无需为此引入完整 City Sample 城市项目。 | 免费，UE 格式，明确 UE-Only。 |
| [Broadcast Studio](https://www.fab.com/listings/88009e85-88b6-4fc1-beac-da998cbb85a0)／Warren Marshall | 演播室环境与道具使用可调整的材质实例，适合改造成简洁角色展示台。 | 免费，UE 格式，明确仅用于 UE 项目。 |

这三页公开内容的 License terms 栏未展开具体协议，实际使用范围以领取时对应的 EULA 与产品限制为准。

## 下载与接入边界

当前没有核实到这三项的公开匿名压缩包。Fab 官方说明，一般免费产品可能支持匿名下载，但 UE 文件需要经 Fab 集成或 Epic Games Launcher 的库取得；加入库需要 Epic 账户，首次取得适用内容可能要求接受 EULA。UE 5.3 及以后支持 Fab 编辑器集成。已有本地、合法取得的资产可先在独立项目验证，再只迁移需要的模块与依赖。[官方获取与下载流程](https://dev.epicgames.com/documentation/en-us/fab/purchasing-and-downloading-assets-in-fab)

工具「预设场景」页提供「浏览 Fab 场景素材」，打开官方 UE 资源页面。资源加入库后，从 UE 的「窗口 → Fab」添加到项目；未启用 Fab 时先在编辑器插件列表启用。该入口不调用 Fab 私有接口，下载与导入由官方集成完成，不会把候选资源自动视为已导入预设。

Fab Standard License 允许在项目中使用、修改和商用，但禁止资产独立再分发；旧 Epic 内容许可对源格式分享另有限制。因此，项目环境可用不等于可以把源资源随公开插件打包。工具应保留用户自行取得资产后的可选接入路径；插件自带场景优先采用自有内容与来源明确、允许再分发的素材。[Fab 标准许可](https://www.fab.com/eula)、[Epic 内容许可](https://www.unrealengine.com/eula/content)

## 现有资源

2026-10-25 只读检查了本机 `C:/ProgramData/Epic/EpicGamesLauncher/VaultCache/FabLibrary`：仅有 53,248 字节的 `listings_v1.db`，其资源记录与下载记录均为 0，没有可直接复用的已下载资源包。此次仅检查该目录及资源元数据，未读取账户凭据，不能据此推断账户库或其他缓存目录为空。

`Resources/Garden/SOURCES.json` 记录了 Poly Haven 的 CC0 木地板、石地板与夜空 HDR，共五个源文件；本轮核对五个 SHA-256 均匹配，`Content/Stages/Textures/T_Garden_*` 对应导入资产存在，可继续复用。樱庭远景另有 `sakura_night_backdrop_v3.generation.json` 记录生成来源，不属于上述 CC0 清单，也不是引擎实拍。[本地来源清单](../Resources/Garden/SOURCES.json)

现有霓虹演唱会、月光镜庭、樱庭水镜的 Blueprint 与地图位于 `Content/Stages/`。新增展示场景的效果与可用性，以后续实际引擎验证和截图为准，不以 Fab 商品图替代验收。
