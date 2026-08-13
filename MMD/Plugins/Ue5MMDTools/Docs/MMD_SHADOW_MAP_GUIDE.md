# MMD 场景阴影（主平行光遮挡）接入指南

让 MMD 角色（toon 材质，自算光照）被**场景物体**（墙/地板/箱子等）投下的阴影遮挡。
只遮挡主平行光（太阳光）；MMD 模型默认**不**参与投射（无自阴影，避免与 NdotL toon 叠加双重变暗）。

## 原理

- `UMMDShadowMapSubsystem` 每帧（`SetupViewFamily`）找主平行光，算出光视角的正交投影矩阵，
  通过引擎公开扩展点 `FCustomRenderPassBase` + `FSceneInterface::AddCustomRenderPass`
  把一个深度 pass 注入**主渲染器内部**执行（共享 frustum culling / LOD），
  输出写到 `/Ue5MMDTools/Rendering/MMDShadowMapRT`（RGBA16F 2048×2048，R 通道 = 沿光轴的线性深度 cm）。
  **不创建任何 AActor / USceneCaptureComponent2D，不放 BP，不污染关卡。**
- 同时算出"阴影相机基"4 个 float4，交给 `UMMDAnimeLightDataSubsystem` 写进 `LightDataRT` **第 2 行**（y=1）：
  - texel0 = `(Origin.xyz, Valid)`
  - texel1 = `(Right/OrthoWidth, GlobalBias)`
  - texel2 = `(Up/OrthoWidth, 0)`
  - texel3 = `(Forward.xyz, 0)`
- 材质侧 `SampleMMDShadow()`（`Shaders/Core/MMDToonLighting.ush`）从 LightDataTex 第 2 行读基，
  把世界坐标投到阴影空间，采样 MMDShadowMapRT 深度做比较（4-tap PCF 软边），返回阴影因子
  乘到直接光漫反射/高光上。

## 材质接入（MMDBaseToon 及复用它的布料材质）

在材质 Custom 节点上新增 3 个输入（类型见下表），并把 `MMDShadowMap` 连到
`/Ue5MMDTools/Rendering/MMDShadowMapRT`：

| 输入 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `MMDShadowMap` | Texture2D | 无 | 场景阴影深度 RT |
| `MMDShadowEnabled` | float | 0 | 0=关（不遮挡） 1=开 |
| `MMDShadowBias` | float | 0 | 阴影深度偏移 cm，自遮挡/漏光时调 5~20 |

> 注意：Custom 节点代码引用了这些输入名，**必须加 pin**，否则材质编译报未定义变量。
> `MMDShadowMapSampler` 由 `MMDShadowMap` 自动生成，不需要手动加。

接入后效果：MMD 模型被场景投出的阴影遮挡，遮挡处直接光归零、只剩天空环境光，高光同步被门控。
`MMDShadowEnabled=0`（默认）完全回退，不影响原有 toon 光照。

## 子系统设置（编辑器控制台 / 蓝图）

`UMMDShadowMapSubsystem` 是引擎子系统，可通过 BP 节点 `Get MM D Shadow Map Subsystem` 调用：

| 函数 | 默认 | 说明 |
|---|---|---|
| `SetShadowEnabled(bool)` | true | 总开关 |
| `SetShadowDistance(float)` | 4000 | 阴影相机放在相机后多远（cm），同时决定 far |
| `SetOrthoWidth(float)` | 4000 | 正交投影宽度（cm），阴影覆盖范围 |
| `SetGlobalBias(float)` | 5 | 全局深度偏移（cm），材质 `MMDShadowBias` 在此基础上叠加 |
| `SetHideMMDActors(bool)` | true | 是否排除 MMD 模型（不做自阴影） |
| `SetShadowMapRenderTarget(RT)` | 自动 | 手动指定阴影深度 RT（默认自动创建） |

## 限制与提示

- 只支持场景直射光（第一盏可见平行光）。聚光/点光阴影暂不支持。
- 阴影相机跟随渲染相机，覆盖相机周围的 `OrthoWidth` 范围；角色跑出覆盖区即不受影。
- 深度 pass 注入主渲染器内部执行（共享 frustum culling / LOD），开销远低于独立 CaptureScene；
  不需要时 `SetShadowEnabled(false)` 关闭。
- 深度为 RGBA16F，远距离或极端场景可适当加大 `GlobalBias`/`MMDShadowBias` 避免自遮挡漏光。
