# 终末地原生着色后端

源码核对日期：2026-10-19。适用引擎：UE 5.8.3 / CL58210709。跨路径总览见[当前渲染架构](RENDERING_ARCHITECTURE.md)。

当前共享后端为 `mmd-native-bsdf-v15-style-profiles`／Cooked协议15，Graph7、终末地Profile布局42、payload5、FaceData5、Frame1保持。暖暖使用独立Profile风格标记和响应，终末地现有分支保留。[v15本日Editor及终末地回归](Screenshots/2026-10-19-infinity-nikki-native/README.md)与下面的v14历史范围分开记录；不以旧结果宣称新版发行验收。

## 当前数据与光照路径

[材质生成器](../Source/Ue5MMDToolsEditor/Private/Editor/MMDEndfieldToonPresetsCommandlet.cpp)生成 Substrate Toon 材质，接到 `FrontMaterial`，并生成专用 `UMaterialExpressionMMDEndfieldNativePayload`。角色用的资产位于 `Stages/Materials/EndfieldV2/`，预设为 `Stages/Presets/EndfieldV2/DA_Render_EndfieldV2`。旧 `MMDEndfieldPresets` 生成器及旧 Endfield Unlit 资产不是这套 V2。

不透明／Masked 的数据流为：

1. 材质阶段准备底色、粗糙度、独立高光法线和几何派生的脸部数据。
2. BasePass 在符合条件的单 Toon／单 basis／adaptive fmt1 材质中写入 payload。
3. Substrate packed export/read 将载荷带到延迟光照，复用原格式中的可用存储位置；没有新增独立 GBuffer RT。
4. 当前灯的方向、颜色、衰减和原生阴影进入插件 BSDF。
5. 原生 GI／反射在各自合成入口处理；不会在材质里重复手算完整场景光照。

透明 Skin／附脸层在原有 Substrate Surface Forward 绘制内使用 inline payload，消费其支持的方向光和局部灯。它保留透明覆盖与混合，不等同于不透明的 packed 读回或完整 Lumen GI。其他变体按门控保留原生路径。

## 源码入口

- [MMDEndfieldNativeBackend.cpp](../Source/Ue5MMDTools/Private/Rendering/MMDEndfieldNativeBackend.cpp)：版本、源 hash、缓存、11个 shader 副本适配点及虚拟目录映射。
- [TMMDEndfieldNativePayload.ush](../Shaders/Core/TMMDEndfieldNativePayload.ush)：载荷编解码；[CustomOutput](../Source/Ue5MMDTools/Private/Materials/MaterialExpressionMMDEndfieldNativePayload.cpp)提供材质输出。
- [TMMDEndfieldNativeBSDF.ush](../Shaders/Core/TMMDEndfieldNativeBSDF.ush)：逐灯响应，接收 BSDFContext、当前灯、`FShadowTerms`、面积光等。
- [TMMDInfinityNikkiNative.ush](../Shaders/Core/TMMDInfinityNikkiNative.ush)：同一逐灯入口选择的暖暖响应，不读取终末地 RD／RS；[原生暖暖说明](INFINITY_NIKKI_NATIVE_RENDERING.md)记录具体范围。
- [TMMDEndfieldNativeIndirect.ush](../Shaders/Core/TMMDEndfieldNativeIndirect.ush)：原生间接光／反射合成入口。
- [TMMDEndfieldNativeForwardPayload.ush](../Shaders/Core/TMMDEndfieldNativeForwardPayload.ush)和[ForwardLight](../Shaders/Core/TMMDEndfieldNativeForwardLight.ush)：透明 Skin 的 inline 数据与灯光。
- [TMMDEndfieldNativeLegacy.ush](../Shaders/Core/TMMDEndfieldNativeLegacy.ush)：传统 GBuffer 的聚簇兼容入口，不能与旧 Unlit 回退混为一谈。
- [MMDEndfieldNativeProfile.cpp](../Source/Ue5MMDTools/Private/Rendering/MMDEndfieldNativeProfile.cpp)：角色 Profile、RD／二维 RS、原创皮肤配方与缓存。
- [MMDStageLibrary.cpp](../Source/Ue5MMDTools/Private/Stage/MMDStageLibrary.cpp)：实际父材质选择、Profile／Face 绑定、参数和源材质恢复。

11个适配点是 SubstrateEvaluation、SubstrateDeferredLighting、SubstrateForwardLighting、SubstrateLightingCommon、DiffuseIndirectComposite、ReflectionEnvironment、ClusteredDeferredShading、Substrate、SubstrateExport、BasePassPixelShader、ToonProfileCommon。不是11个新增渲染 pass。

## 启用与回退

通过[Invoke-EndfieldPresets.ps1](../Scripts/Invoke-EndfieldPresets.ps1)及[分发说明](ENDFIELD_PLUGIN_DISTRIBUTION.md)配置。原生 Toon 需要启动时开启 Substrate；自定义后端用 `-MMDEndfieldNativeBackend` 或对应配置请求启用。

模式0为原生透传，模式1为阴影诊断，模式2为插件响应。Mode2 需在应用预设前设置 `r.Substrate.ToonProfile.Resolution=256`；不足时按当前实现退回原曲线／Mode0，不支持已有 Profile atlas 运行中直接降分辨率。

后端校验固定引擎版本和私有 shader hash，将适配副本放在工程 `Intermediate/MMDEndfieldNativeBackend/`，通过 `/Engine/Private` 映射使用。安装引擎源文件不写入。这依赖私有接口，不是跨版本稳定的官方 ShadingModel API。Cooked 启动须验证序列化协议；请求开启、CPU Profile 标记或一份 GlobalShader 协议值，均不能单独证明全部 GPU 材质内核正确。

Substrate 不可用或缺少原生父材质时，运行时可选旧 Unlit 兼容材质。后端未启用时的原生 Toon 和非 Substrate 的 Unlit 回退是不同路径。

## 已有验证与限制

以下是历史实测，未在2026-10-19重新构建或拍摄：2026-10-16 的 v14 `cast4` 为27图／156次槽检查／0失败，另有 Mode0 和 atlas64 回退各8图；原图与来源见[投影清理归档](Screenshots/2026-10-16-endfield-cast-clean/README.md)。当日 BuildOnly 三配置、文件和源哈希结果见[分发说明](ENDFIELD_PLUGIN_DISTRIBUTION.md)。不能把 v8／v4 的 Cooked 验证记作 v14 的结果。

已具备原生阴影、GI 和延迟载荷接入；现有皮肤配方仍是局部外观响应，没有真正空间 SSS、邻域扩散或厚度透射。完整终末地画质、全部头发／眼睛／屏幕效果、动态形变与 LOD、最新版完整 Cook／发行角色 GPU 和性能仍按各功能记录继续验收。

旧 LightDataRT／阴影 RT 子系统仍可能运行，但原生 V2 不依赖它们取光。尚未按材质需求停掉重复工作，也没有本轮性能收益实测。旧阴影 RT 无效不能推导原生 V2 的 VSM／逐灯阴影无效。

详细使用见[维护指南](ENDFIELD_NATIVE_RENDERING_GUIDE.md)，剩余范围见[接手状态](ENDFIELD_V2_FOR_GPT.md)。
