# 终末地插件着色入门与维护

更新日期：2026-10-14。适用版本：UE 5.8.3 / CL58210709。

**UE 负责准备真实灯光、阴影和环境光，插件负责决定角色被照亮后是什么样子。** 这就是目前的方案。材质仍使用原生 Substrate Toon 的数据格式，光照计算入口已经接到插件自己的 `.ush`。

shader 是计算像素颜色的程序，`.ush` 是保存这类代码的文件；材质负责提供底色、法线等输入。

先看下面两张示意图，再看实拍。需要改代码时，直接跳到[维护入口](#维护入口)。

## 为什么从旧方案换过来

![三版方案的职责对比](Images/EndfieldNative/scheme-evolution.svg)

最早的 **Unlit** 是“自己画出受光的样子”：插件把灯光写进 RT，再在材质里手算明暗。RT 就是一张由程序实时写入的纹理。Unlit 不自动接收原生阴影，因此还要额外渲染一张阴影深度 RT，自己比较谁挡住了光。

后来改成 **原生 Toon**，角色开始使用 UE 的灯光、阴影和环境光，但我们主要只能调整底色、法线和 ramp。ramp 可以理解成“输入一个明暗程度，查出一个颜色”的色带；它不能表达所有想要的光照方程。

现在的 **插件后端** 保留原生数据和渲染流程，将光照计算接到自己的函数。过去主要是调方程的输入，现在可以改方程本身。这样做是为了同时拥有 UE 的场景一致性和终末地着色所需的控制权。

## 同一个材质分阶段处理光照

![一个材质在直射和间接阶段的数据流](Images/EndfieldNative/native-pipeline.svg)

**这里不是两个材质，也不是把角色渲染两遍再叠加。** UE 本来就分阶段处理直射光与间接光，我们在对应阶段各接一个入口。

直射光是灯直接照到角色。每处理一盏灯，UE 提供方向、衰减、面积光和原生阴影；插件计算这一盏灯产生的漫反射与高光，UE 再按原流程加权、累加。

```text
最终直射颜色 = 各盏灯的［UE 灯光权重 × 插件着色响应］之和
```

引擎外层仍负责乘灯色、强度和灯光 mask；在当前入口里再乘一次会把能量重复计算。灯光 mask 是引擎算出的该灯作用范围等权重。

间接光是墙面反弹过来的光、环境照明和反射。**GI** 就是全局光照。Lumen 继续计算这些结果，插件在合成入口处理当前阶段的贡献。现在只实现了间接漫反射／镜面贡献的倍率控制，没有重写 Lumen 的多次反弹算法。

### 数据会不会慢一帧

直射入口接的是**本次光照 pass 正在使用的参数和阴影结果**。这里的“复制”是 shader 函数内整理参数，没有新增一张 RT 再等下一帧采样，也没有额外的整帧延迟。

Lumen 的 GI／反射会使用历史累积，VSM 会复用仍然有效的阴影缓存。插件读取的是 UE 给本次合成准备的结果，保留这些原生机制；“本帧使用”不等于“所有结果都是本帧从零重算”。游戏线程与渲染线程原有的流水线延迟也仍然存在。

## 我们的入口在渲染管线哪里

下面标的是 UE 5.8.3 桌面延迟渲染中不透明／遮罩材质的入口位置。**B、C、D 都嵌在 UE 已有 pass 里，不是额外新建三套完整光照 pass。** pass 可以理解成渲染管线中的一项工作。

![材质直射间接与反射入口在管线中的位置](Images/EndfieldNative/pipeline-hooks.svg)

- **A 材质准备，在 BasePass 内。** 材质 Custom 调用 `TMMDEndfieldNative::PrepareSurface()`，计算独立原生法线、脸部漫反射方向和头部旋转。专用 CustomOutput 将后两项交给 BasePass 的打包入口，再供逐灯读取。这一步还没有当前逐灯光的最终阴影。
- **B 直射，在 RenderLights 内。** `DeferredLightPixelShaders.usf` 调用 `SubstrateDeferredLighting()`；该调用链准备原生阴影、接触阴影与 AreaLight，再在 `SubstrateEvaluation.ush` 的 Toon 分支调用我们的 `EvaluatePerLight()`。返回后，UE 外层乘灯色／LightMask 等并累加。额外灯光参数的复制在同一调用链中完成。
- **C 间接，在 DiffuseIndirectComposite 内。** UE 已取得 GI／反射输入，算好原生材质权重和 AO，插件再调用 `Process()`；adaptive 路径处理 `OutBSDF.Diffuse/Specular`，传统路径处理 `IndirectLighting.Diffuse/Specular`，随后才累加到 SceneColor。位置在原生分离输出及后续合成之前，不在 Lumen 射线追踪或历史更新内部。
- **D 独立环境／反射，在 ReflectionEnvironment 内。** `SubstrateEnvLightingCommon()` 及适用的独立 Lumen 反射合成先计算贡献，再调用插件 `Process()`，之后进入原生反射量化、分离输出和颜色合成。如果 Lumen 的组合路径已经在 C 合成了对应贡献，这个独立 pass 会按配置跳过，不能重复处理同一份结果。

**C 的位置按调度选择，不能固定画成直射之后。** 本机引擎在 `RenderLights()` 前后各调用一次 `RenderDiffuseIndirectAndAmbientOcclusion()`：SSGI 和未走异步的 Lumen 在前一次生成并合成；异步 Lumen 只剩合成步骤时，在直射后的第二次合成。对同一份贡献，不会两次都调用 C 去重复乘倍率。RDG（UE 的渲染任务图）维护资源依赖，Lumen 计算能与其他 GPU 工作重叠。

当 GI 使用 Lumen 时，对应反射已由 C 合成，独立 D 会跳过；GI 不使用 Lumen 的独立反射／天空路径才可能执行 D。这里也没有接管 D 循环外的非零独立 SSR 合成。

不包括透明前向、移动端、MegaLights 或路径追踪的完整覆盖；这些不能从当前桌面延迟验证推导出来。后续 SSS、透明与 TAA／TSR、tonemap 等后处理仍沿用 UE 流程。

维护时可对照安装引擎 `Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp`：BasePass 调用约在 3060 行，前一次 GI 在 3430 行，`RenderLights()` 在 3468 行，后一次 GI 在 3519 行，独立反射在 3529 行，后处理入口约在 4312 行。行号针对上述固定版本；只读对照，实际修改仍在插件适配器和 ush。

## GBuffer 和 ShadingModel 到底变了什么

可以把 **GBuffer／Substrate 材质容器**理解为每个像素的“材质记录”：保存颜色、法线、粗糙度等，供后面的光照读取。没有新增独立的 GBuffer render target；新版在自己的 Skin 材质里复用已有字段传递脸部参数。

**ShadingModel／BSDF** 是使用这些记录计算受光效果的规则。当前仍使用原生 Toon 类型，没有新增一个 ShadingModel ID；替换的是指定 Toon 材质分支中的光照计算函数。

插件通过 Profile 标记识别自己的材质。Profile 是一份可共享的着色设置：其色带的 alpha 保存角色标记、模式和 GI 倍率，RGB 仍保存颜色。普通 Toon 和 Default Lit 材质继续走原生响应。

GBuffer 里保存的是 Profile ID，用它查找对应色带；这些标记和倍率不占用 GBuffer 的 alpha 通道。

shader 中的 `mmd_light_data` 只是“这一次光照计算的参数包”，不写入 GBuffer。新版另有正式打包的 `mmd_payload`：24 bit 保存头空间漫反射方向及权重，32 bit 保存头部旋转。它复用自有 Toon 的 PatternUV 字段和材质编译器已预留的 anisotropy word；Profile ID、底色和粗糙度保持。原生 Normal 独立供高光与 GI 使用。

目前这条通道只用于 **Mode2、adaptive GBuffer=1、不透明／遮罩、单 Toon Skin**，并检查实际预算。真实各向异性、透明、fmt0、多层和简化变体会回退；不能把这套字段分配当成所有材质都可使用的空闲存储。普通材质不使用这些标记。没有新增一次角色绘制或离屏光照 pass。

这也不是“完全没有额外写入”：原生零 anisotropy 的像素可能跳过该32 bit word，新通道会写它，只是没有超出编译器预留预算。打包、读回和解码都有成本，目前未测净性能收益。

## 本轮脸部与 ramp 改了什么

![脸部方向与独立原生法线的数据流](Images/EndfieldNative/face-payload.svg)

旧版把固定俯角下的脸部阈值转换成一条共享法线，同时影响明暗、高光和 GI。改变灯光俯角时，这条法线无法重建烘焙时的方向；原型还会先做三档分类、再叠加偏移。

新版自动生成 **V4 几何方向场**：保存完整的头空间方向，逐灯用真实光方向求明暗，不再依赖固定俯角。镜像共 UV 的四个采样点先恢复各自左右语义再插值；复杂冲突退回几何计算。保留 UV0，未增加 UV2，也不需要用户手绘贴图。这个方向场没有计算遮挡距离，不能称为真正的阴影 SDF；真实遮挡仍来自 UE。

V4 的烘焙方向目前跟随头骨刚性旋转，尚未重建嘴／脸颊 Morph 或独立下颌骨造成的局部方向变化；无派生数据时的几何回退使用实时位置和顶点法线。这里仍需加入变形修正，高光与 GI 的原生法线则继续跟随真实几何。payload 的权重表示准备后方向有效，贴图的覆盖权重已在材质阶段混合，不能再次相乘。

```text
BasePass：几何／自动方向场 → 漫反射方向 + 头部旋转 → 打包
逐盏灯：读回方向 → 与本盏灯求 N·L → RD 明暗混合 + 颜色 tint
高光／GI：独立原生 Normal → 原生数据 + 对应着色响应
```

Skin、Hair、Cloth 的 RD 使用已有客户端完整 256 点数据。RD 的 alpha 决定明暗混合量，RGB 负责 tint；影色由底色亮度与饱和度派生，再作亮度补偿。RD alpha 存在 Profile 的 specular 行 alpha，diffuse 行 alpha 继续保存模式标记。**真实 RD 不能直接当作一个 RGB 乘数。**

衣料高光保留 GGX 波瓣幅度与原作 `.04 × specular` 的 F0 约定；代表 Skin 未启用 RS，使用白色乘数。Hair／Cloth 的二维 RS 暂取固定纵坐标切片，头发用几何竖向切线计算主高光环；没有原作 P 图／flow 图时属于明确的近似。完整二维 RS、Skin 颜色 LUT、头发第二波瓣与独立高光法线数据、眼睛、深度 rim／描边及最终调色仍在复刻清单里。

Mode2 要求 `r.Substrate.Experimental.ToonUnifiedDiffuse=0`，由每盏灯处理 RD 语义；统一漫反射后段尚未适配，会拒绝启用此模式，避免二次套用色带。验证使用 Profile atlas 256 点；引擎默认 64 点会重采样，插件没有改工程的默认配置。

本轮同镜头侧光、头发／衣料及packed读回原图见[2026-10-14画质迭代归档](Screenshots/2026-10-14-endfield-fidelity/README.md)。旧版鼻部、脸颊硬条块已明显减少；其余复刻项继续按清单推进，不以编译或图片数量判定终末地画质达标。

## 看实拍理解 GI 接口

以下两张均为 **2026-10-14 捕获的 UE 打包游戏原图**。这是基础几何诊断场景，中央人形来自工程默认 GameMode，颜色和曝光用于测试，不是终末地成品效果。

第一张：自定义响应原型，间接漫反射／镜面倍率均为 1。

![间接光正常参与的打包实拍](Screenshots/2026-10-14-endfield-native-backend/packaged-mode2.png)

第二张：保持直接灯光，将插件入口的间接漫反射倍率设为 0。注意肤色球体下方及盒子背光面明显变暗；这说明插件确实能处理原生间接光贡献。

![插件关闭间接漫反射后的打包实拍](Screenshots/2026-10-14-endfield-native-backend/packaged-diffuse-off.png)

更多原生阴影、高光对照以及原始来源见[实机归档](Screenshots/2026-10-14-endfield-native-backend/README.md)。本次编写文档没有重新运行 UE，沿用上述日期的实拍和验证结果。

## 阴影 RT 和性能

新后端直接取得 UE 原生阴影，自己的着色函数不需要旧 `MMDShadowMapRT` 或 `LightDataRT`。原生阴影结果告诉我们“这个像素被挡了多少”，插件可以把它并入明暗分段或高光门控。

**当前旧 RT 子系统仍在后台运行。** 旧阴影启用且有有效主光时，仍提交四级阴影深度 pass；灯光 RT 也仍在更新。因此不能把“新 shader 不再采它”写成“已经省掉这些开销”。

后续应按场景里实际使用的材质启停：全部使用新后端时停掉重复工作；仍有旧 Unlit 材质需要它时保留。停掉重复渲染有望节省这部分耗时，净收益需要实测，原生阴影和自定义 shader 本身也有成本。

此外，原生阴影值不保存遮挡物身份，不能直接区分鼻子、刘海和墙。因此接通原生阴影不等于已经解决所有脸部自阴影问题。

## 安装引擎文件为什么可以保持不变

```text
安装引擎的 Private shader
        ↓ 只读复制
工程 Intermediate 中的副本
        ↓ 适配光照、合成及材质打包入口并 include 插件 ush
Editor／Cook 编译 shader
        ↓
打包程序使用编译好的 shader 库
```

这叫 shader overlay，可以理解成“编译器改用工程里的适配副本”。安装目录的源文件不写入，但实际编译的 shader 实现确实经过适配。

它依赖固定版本的私有接口。升级 UE 时需要重新检查入口和源文件 hash，不能直接删除版本保护强行启用。工程 `Intermediate` 是自动缓存，维护应改插件源码；直接改缓存既难复现，也会破坏 hash 校验。

## 维护入口

按“想改什么”选择入口，通常不用同时打开所有文件。

### 改明暗分段和高光

打开 [TMMDEndfieldNativeBSDF.ush](../Shaders/Core/TMMDEndfieldNativeBSDF.ush)，看 `EvaluateToonResponse()`。

这里接收材质、当前灯光、阴影和面积光。Mode2 的 Skin／Hair／Cloth 用 `ReadDiffuseRamp()`、`ApplyDiffuseRamp()` 和 `EvaluateSpecular()` 处理真实 RD 语义及高光。Skin 的漫反射方向来自 `BSDF.mmd_payload`，高光法线来自独立原生 Normal；矩形灯保留原生 LTC／Toon 积分，使用当前 RS 切片。

### 改 GI 或反射的表现

打开 [TMMDEndfieldNativeIndirect.ush](../Shaders/Core/TMMDEndfieldNativeIndirect.ush)，看 `Process()`。

当前只有最终贡献倍率。扩展时按 `path` 区分合成阶段，并检查 `available_flags`：没有标为可用的字段只是零占位。逐灯光阴影与完整 Lumen 结果不在同一个入口里。

### 改材质输入或脸部数据

打开 [TMMDEndfieldNative.ush](../Shaders/Core/TMMDEndfieldNative.ush) 的 `PrepareSurface()`；材质图由 [MMDEndfieldToonPresetsCommandlet.cpp](../Source/Ue5MMDToolsEditor/Private/Editor/MMDEndfieldToonPresetsCommandlet.cpp) 构建。

这层负责底色、几何和脸部方向场。`ReadDirection()` 负责逐 texel 解镜像，`FrameQuaternion()` 负责头部旋转。旧 V3 阈值桥保留在 `PrepareLegacySurface()`，与 V4 分开读取。派生数据由 [MMDEndfieldFaceSdfBaker.cpp](../Source/Ue5MMDToolsEditor/Private/Editor/MMDEndfieldFaceSdfBaker.cpp) 自动生成。

跨阶段打包维护 [TMMDEndfieldNativePayload.ush](../Shaders/Core/TMMDEndfieldNativePayload.ush)；正式材质输出是 [MaterialExpressionMMDEndfieldNativePayload.h](../Source/Ue5MMDTools/Public/Materials/MaterialExpressionMMDEndfieldNativePayload.h)。新增数据时必须一起检查 CustomOutput、BasePass 注入、packed export/read、预算与简化回退，不能只给局部 struct 加成员。

### 改材质识别和引擎接入

- [MMDEndfieldNativeProfile.cpp](../Source/Ue5MMDTools/Private/Rendering/MMDEndfieldNativeProfile.cpp)：`MarkTransient()` 和 `GetOrCreate()`，负责 Profile 标记、真实 RD／RS 曲线、控制值和瞬态副本；同目录 `MMDEndfieldNativeRampData.inl` 保存样本数据及来源。
- [MMDStageLibrary.cpp](../Source/Ue5MMDTools/Private/Stage/MMDStageLibrary.cpp)：把对应 Profile 应用到角色材质。
- [MMDEndfieldNativeBackend.cpp](../Source/Ue5MMDTools/Private/Rendering/MMDEndfieldNativeBackend.cpp)：版本保护、源码校验、缓存、光照入口及新增的 BasePass／Substrate packed export/read 适配。
- [MMDEndfieldNativeIndirectPatch.inl](../Source/Ue5MMDTools/Private/Rendering/MMDEndfieldNativeIndirectPatch.inl)：GI／反射合成位置。
- [TMMDEndfieldNativeLightCapture.ush](../Shaders/Core/TMMDEndfieldNativeLightCapture.ush)：复制原生灯光输入；只有 `mmd_light_data.valid=true` 时这些额外字段才有效。
- [TMMDEndfieldNativeLegacy.ush](../Shaders/Core/TMMDEndfieldNativeLegacy.ush)：传统 GBuffer 的聚簇灯光兼容路径。

### 处理旧 RT 的重复开销

看 [FMMDAnimeLightViewExtension.cpp](../Source/Ue5MMDTools/Private/Rendering/FMMDAnimeLightViewExtension.cpp) 的 `SetupView()` 和 `PreRenderViewFamily_RenderThread()`：这里仍调用旧阴影更新、灯光收集与 RT 写入。

对应实现是 [UMMDShadowMapSubsystem.cpp](../Source/Ue5MMDTools/Private/Rendering/UMMDShadowMapSubsystem.cpp) 和 [UMMDAnimeLightDataSubsystem.cpp](../Source/Ue5MMDTools/Private/Rendering/UMMDAnimeLightDataSubsystem.cpp)。未来停用应依据旧材质是否需要它们；仅看“新后端已启用”就全局关闭，会影响混用旧材质的场景。

## 改完怎样确认没有弄坏

先开启 Substrate，并显式启用后端；项目默认配置仍未更改。

```text
-MMDEndfieldNativeBackend
-MMDEndfieldNativeBackendMode=2
```

- 模式 0：原生透传，用作基线。
- 模式 1：把阴影通道显示成 RGB，用于确认收到的是原生阴影。
- 模式 2：当前 RD／RS 与脸部方向响应迭代，不代表最终终末地画质。

先用 [MMDEndfieldNativeBackendProbe](../Source/Ue5MMDToolsEditor/Private/Editor/MMDEndfieldNativeBackendProbeCommandlet.cpp) 比较原生／自定义、无遮挡／有遮挡、间接光开／关，并确认普通材质保持原生。改材质图时同步更新生成器中的图版本／签名。

之后用 [MMDEndfieldNativeBackendFixture](../Source/Ue5MMDToolsEditor/Private/Editor/MMDEndfieldNativeBackendFixtureCommandlet.cpp) 生成专用夹具，再做 Cook 和打包游戏对照。**Cook 时必须启用后端；仅在打包程序启动时加开关，不能补上没有编译进去的内核。** 改 ush 后需重新编译，发行时重新 Cook；只切换 Profile 模式／倍率不需要重编同一份内核。

2026-10-14 的既有验证已覆盖 Editor 原生阴影／GI 对照和五组打包游戏运行。移动端、CSM、Cook 后的 SDF／骨骼／透明发片，以及最终终末地画质仍未完成验收。详细配置、验证记录和剩余边界见[技术记录](ENDFIELD_NATIVE_BACKEND.md)，完整复刻范围见[交接清单](ENDFIELD_V2_FOR_GPT.md)。
