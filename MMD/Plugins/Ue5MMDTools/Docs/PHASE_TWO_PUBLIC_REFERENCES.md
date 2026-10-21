# 二期参考：终末地公开资料与离线方法

**历史快照（2026-10-19 标记）**：本文保留原批次的研究、状态与验证，内部“当前／下一步”和历史聊天指令不作为今日架构或执行约束。现状先读[当前架构](RENDERING_ARCHITECTURE.md)和[接手状态](ENDFIELD_V2_FOR_GPT.md)，原日期与证据保持。

核对日期：**2026-10-11**。用户反馈游戏检测到工具后，本轮停止 RenderDoc 挂接和抓帧，优先查公开解包方法、已有分析和材质复现源码。下面的网络资料不是本次游戏实拍；本轮插件引擎验证另行记录。

## 可以用到的资料

| 入口 | 可研究内容 | 本次核对边界 |
| --- | --- | --- |
| [EndfieldStudio](https://github.com/EIHRTeam/EndfieldStudio) | VFS 容器读取、资源解码与定向导出 | 本机 CLI 构建、VFS 列表和小范围 bundle 读取成功；另用实际 TypeTree 核对人物材质引用 |
| [Endfield Research Kit](https://github.com/Variante/endfield_research_kit) | AnimeStudio 的离线资源、材质与 shader 恢复流程 | 已读源码；没有运行完整资源导出。其公开说明也要求复核恢复结果 |
| [RuriRipperImporter](https://github.com/ShiyumeMeguri/RuriRipperImporter) | 终末地 Blender 材质库、Substance GLSL 与输入映射清单 | 2026-10-11 已核对公开生成文件；没有运行导入器，生成材质仍缺部分游戏管线功能 |
| [Better-Endfield 材质配方](https://github.com/Dr-hydra/Better-Endfield/blob/40560f4c1b601f13273afc710e3315a47fd7b75a/tools/CustomModel/profiles/zhuangfangyi-061fe0.materials.json) | 庄方宜身体、头发与衣物的绑定记录；定向离线读取工具 | 是作者旧样本报告。本次没有取得所引原始证据文件、DXBC 或公开实拍，也没有复现其运行时结论 |
| [RenoDX 终末地源码目录](https://github.com/clshortfuse/renodx/tree/c20d568f8f01c6f1cb7377632799e03ce1c61520/src/games/endfield) | 公开反编译后改写的 UI、后处理、阴影和天空 shader 样本 | 只读源码；没有安装或运行 mod。没有定位人物皮肤、头发、布料主 BRDF 与材质身份的对应关系 |
| [Endfield MME Shader](https://github.com/chris0214/Arknights-Endfield-MME-Shader) | 可读的独立 MMD 材质实现、材质生成器和使用流程 | 作者明确为仿终末地渲染；不是商业游戏原始源码。模型和角色贴图不随仓库分发 |
| [Gacha Setup](https://github.com/PaoloESAN/gacha-setup) | Blender 角色设置、Endfield PBRToon 与鸣潮重建入口 | 社区重建工具；不是游戏原资产库或原始 shader。仅研究结构，不移植其 GPL 实现到插件 |

查到的 [EndFieldPS2](https://github.com/UDSaUDHASASDIHADS/EndFieldPS2) 面向 CBT2 服务端与协议，并不能直接提供人物 shader。本轮没有下载或运行私服客户端，也没有配置其代理、证书或登录环境。

## 2026-10-11 GitHub 解包方案复核

本次重新读取 GitHub README、代码、提交和部分 Issues；以下是**代码审查结果**，没有运行新的解包器、游戏或 UE。仓库的“支持终末地”不等于本机资源版本 `24577065` 的完整导出已通过。

| 路线 | 固定审查版本 | 对本机的意义 |
| --- | --- | --- |
| EndfieldStudio | main / [ccf769b1](https://github.com/EIHRTeam/EndfieldStudio/commit/ccf769b1a3bedd9e300e0b1ba9c87ec1b87b2695)，2026-08-04 | 外层 VFS 在此前本机实测成功；Material 有 HG 字段适配，但原版 Shader 尾部解析与本机实际 TypeTree 不同 |
| Research Kit + Variante/AnimeStudio | Kit 默认分支 v0.0.2 / [717c6a56](https://github.com/Variante/endfield_research_kit/commit/717c6a56cb3198d5bb7ed0588dd34ff53293bd55)，2026-10-04；固定子模块 5bb936ca | 正式版 LOD/Blob 与参数元数据适配更匹配，适合下一步定向核对；本次没有重新执行其导出 |
| Ruri.RipperHook + ShaderDecompiler | [RipperHook ed62f85a](https://github.com/FractalTools/Ruri.RipperHook/commit/ed62f85af4e5da83dabd514b29ee93ef7d34e86b) / [Decompiler af13b5db](https://github.com/ShiyumeMeguri/Ruri.ShaderDecompiler/commit/af13b5dbbc1b59ac8cb4528f1af113d0c2e36c84)，2026-10-03 | 扩展 AssetRipper/FModel 的读取与导出，结合引擎元数据恢复编译程序符号；这条离线工具链不需要挂接游戏进程 |
| RuriRipperImporter | [fb77f982](https://github.com/ShiyumeMeguri/RuriRipperImporter/commit/fb77f982c97c6e3d72a998782c76c18cae39fabf)，2026-10-09 | 实际公开终末地生成材质，可研究输入和重建结构；不是原始 HLSL 或完整渲染管线 |

### 优先核对的读取器

EndfieldStudio 的 [Shader.cs](https://github.com/EIHRTeam/EndfieldStudio/blob/ccf769b1a3bedd9e300e0b1ba9c87ec1b87b2695/AnimeStudio/Classes/Shader.cs) 将正式版与 CB3 按旧的 `m_UseExternalBlobs` / `PPtr<SubShaderBinaryData>` 尾部读取。本机此前读取 Shader 时采用了文件内实际 TypeTree，不能把成功结果全部归为原版 EndfieldStudio 的 Shader 导出。

Variante 的 [Shader.cs](https://github.com/Variante/AnimeStudio/blob/5bb936ca1d3662fe9b956ec669ab7ebea08bfd07/AnimeStudio/Classes/Shader.cs) 单独适配正式版 `m_EnableShaderLODStreaming` / `subShaderBlobs`；[ShaderConverter.cs](https://github.com/Variante/AnimeStudio/blob/5bb936ca1d3662fe9b956ec669ab7ebea08bfd07/AnimeStudio.Utility/ShaderConverter.cs) 包含 HG type33/DXBC、LOD 分段、参数 Blob、资源 descriptor、constant-buffer 记录与字节码 sidecar。这是比“只导模型”更完整的路线，但未知 Blob 也可能仅导出 metadata。应分别验收字节码、参数与材质引用，不能以导出文件存在代替成功。

Kit 默认分支与 master 不能混用：默认版本固定 AnimeStudio `5bb936ca`，本次核对的 master 则使用 [22a860f7](https://github.com/Variante/AnimeStudio/commit/22a860f7d80982a140b475266d048dc43107ea26)，提交于 2026-10-09 UTC。作者说明恢复含 LLM 辅助且需原数据复核；IL2CPP 代码恢复与 Unity 资源导出是不同链路，普通资源导出不要求先完成 Cpp2IL。

### 公开重建材质的新增发现

RuriRipperImporter 的 [Endfield shader 目录](https://github.com/ShiyumeMeguri/RuriRipperImporter/tree/fb77f982c97c6e3d72a998782c76c18cae39fabf/Game/Endfield/shader) 实际包含角色、场景、特效、后处理、水体、阴影接收的 Blender 库，以及 `Ruri_Endfield_Uber.glsl` 和 [输入 manifest](https://github.com/ShiyumeMeguri/RuriRipperImporter/blob/fb77f982c97c6e3d72a998782c76c18cae39fabf/Game/Endfield/shader/Substance/Ruri_Endfield_Uber.manifest.json)。本次读取 GLSL 文件头与 manifest：明确为 `Ruri.RenderPipelines.Generator` 生成物；manifest 标记 Substance / Endfield / character stack，记录 BaseMap 等通道映射与法线重打包操作。

这是对先前“未取得公开重建结果”的补充：生成器真源仍未在已审查的 RipperHook tree 定位，**下游公开生成文件确实存在**。但作者的 [Blender 未支持功能清单](https://github.com/ShiyumeMeguri/RuriRipperImporter/blob/fb77f982c97c6e3d72a998782c76c18cae39fabf/Game/Endfield/shader/Blender/_BLENDER_UNSUPPORTED.md) 明确列出角色 screenDepth/screenSurfaceClass、部分 OverlayShadow，以及场景 contactShadow、屏幕采样、体积雾、SSR 和 GPU cloth 等缺失。没有运行或视觉验证这些库，不将其称为原作画面恢复。

另外找到 [endGuaGua/EndfieldUnpacker](https://github.com/endGuaGua/EndfieldUnpacker)：搜索索引中的作者 README 描述 VFS `.blc/.chk` 提取及集成 AnimeStudio，但本次直接访问 GitHub 返回 HTTP 451，无法核对当前提交与代码。该入口仅记录为线索，不以缓存说明宣称当前客户端兼容，也没有获取或运行其打包程序。

建议路线是**离线容器读取 → 单角色依赖闭包 → 材质/纹理通道 → 编译 shader 与参数对应 → 独立通用实现与实拍对照**。先用 Variante 读取器与现有 TypeTree 结果互证少量 Hair/Cloth 样本，再研究生成材质的结构；私服服务端不解决人物 shader 恢复。反编译能恢复部分可读程序，通常不能还原原始源码、作者命名或某个游戏帧的运行时状态。插件仍不依赖角色专有 UV、SDF 或配套贴图，也不直接复制第三方重建实现。

## 人物材质绑定线索

Better-Endfield 的公开 [庄方宜验证报告](https://github.com/Dr-hydra/Better-Endfield/blob/40560f4c1b601f13273afc710e3315a47fd7b75a/docs/custom_model/research/1.5.3/zhuangfangyi/CUSTOM_MODEL_ZHUANGFANGYI_VALIDATION.md) 记录的是 **2026-09-17** 的作者测试。报告及配方描述：头发保留 `hair/hairt` 双材质；身体使用 `_BaseMap/_BumpMap`；特定透明衣物变体使用 t19/t20/t21 对应 `_BaseMap/_MetallicGlossMap/_BumpMap`。

这些槽位只适用于作者记录的样本和变体。配方中的绘制哈希不能当成 shader hash，槽位也不能推广为所有角色统一布局。报告引用的原始 `artifacts/native-parser/*evidence.json` 未在公开 tree 中取得，当前证据等级是**作者报告与配方**，不是本机验证。

该仓库还提供按实际 TypeTree 读取 `m_ParsedForm` 前缀、保留 raw Shader 与 suffix schema 的 [NativeAssetReader](https://github.com/Dr-hydra/Better-Endfield/blob/40560f4c1b601f13273afc710e3315a47fd7b75a/tools/CustomModel/NativeAssetReader/Program.cs)。后续可研究这种离线方式，绕过固定版本 Shader parser 的不兼容；读取器与槽位解码仍需独立复核，不能把其输出直接标为已验证。

本轮源码审查还发现 [inspect_shader_bindings.py](https://github.com/Dr-hydra/Better-Endfield/blob/40560f4c1b601f13273afc710e3315a47fd7b75a/tools/CustomModel/inspect_shader_bindings.py) 的 RDEF 资源记录字段错位：把 ReturnType/Dimension 当作 slot/count，且未处理 SM5.1 的额外字段和步长。对照 [RenderDoc 的磁盘结构与读取实现](https://github.com/baldurk/renderdoc/blob/36b1c78ec7df44b2945c3c360ba8f4cc445eb060/renderdoc/driver/shaders/dxbc/dxbc_container.cpp#L341-L359)，SM5.0 的 BindPoint/BindCount 应从第六/第七个 DWORD 读取。因此不能照搬该 RDEF reader 的槽位输出；这也不自动否定作者另外使用 HG descriptor 与 D3DDisassemble 得出的旧配方。

## 已有画面分析与复现

- [官方庄方宜战斗演示](https://www.bilibili.com/video/BV1woQ3BvEq6/)与[官方角色页](https://endfield.gryphline.com/en-us/operator)：用于核对同一角色的公开外观目标。初次网页文本读取受限；2026-10-11 后台浏览器已打开官方视频，核对发布账号，查看约 8–9 秒人物近景及少量展示/战斗片段，未完成全片逐帧分析。播放器菜单实际显示自动 360P，较高档需要登录；压缩、动态特效和构图限制材质判断，不作微法线或定量像素对比。没有下载、改画或归档原视频；公开视频不能证明本机当前帧的 shader 变体或 GPU 绑定。
- [Floraison：终末地的角色描边](https://www.floraison.io/2026/04/14/)：2026-04-14 的角色描边 pass 分析，描述屏幕空间宽度、基础色派生颜色、环境贡献与角色自遮蔽。可用于描边方向核对，不证明皮肤、头发或布料的主 BRDF。
- [S7arry：终末地渲染还原记录（一）](https://www.s7arry.top/article/zmd1)：2026-06-02 发布、06-10 更新的独立复现，讨论衣物 GGX、ramp 与环境反射；作者明确仍有未实现的特殊材质。输入约定和参数属于该复现，不能直接认定为游戏真值。
- [Shuyuan Ji：Character Rendering Recreation](https://sku11zz.github.io/AnimTA_ShuyuanJi/project-endfield-character-rendering.html)：作者的 Blender/UE5 角色效果复现，可观察不同材质分支和构图目标；不是官方源码或本次实拍。
- [Muxi：终末地大气渲染解析](https://mu3657.github.io/posts/endfield-atmosphere-rendering-part-1/)：2026-09-03 的 Vulkan 截帧分析。作者明确重构代码含 AI 辅助命名与推断；仅作为天空与环境数据流线索，不扩展为人物材质证据。

[Ruri.RipperHook 的 FRAMEWORK](https://github.com/FractalTools/Ruri.RipperHook/blob/ed62f85af4e5da83dabd514b29ee93ef7d34e86b/FRAMEWORK.md) 描述了工具内部的导出、符号恢复与重建流程。其“13 张重建材质”是作者示例，不是本机结果；生成器真源未在该 tree 定位，但后续在 RuriRipperImporter 找到了公开生成库，详见上方新增复核。独立 [Ruri.ShaderDecompiler](https://github.com/ShiyumeMeguri/Ruri.ShaderDecompiler) 有 DXBC/DXIL/SPIR-V 离线 CLI，需要引擎元数据协助恢复名称和绑定；本次未运行。不能把“13 张重建材质”写成“拿到了游戏的 13 个原始 shader”。

## 本机离线检查的新增结果

EndfieldStudio 本次从源码构建成功，VFS 列表 CRC 检查通过，识别客户端资源版本 `24577065`、codeVersion `4`。列表中 InitBundle 有 1,052 个文件、Bundle 有 253,670 个文件、JsonData 有 95,245 个文件；这些数字是容器条目数，不是人物或 shader 数量。

早先裸资源副本只解析出 `Default-Skybox` 材质；它不是人物证据。后续定向读取已突破这一限制，结果如下。前置暖暖离线检查见 [PHASE_TWO_REFERENCE_PROBE.md](PHASE_TWO_REFERENCE_PROBE.md)。

### 本机人物资产引用已核对

以下是 **2026-10-11 本机客户端的离线检查**，与上方作者旧报告分开记录。没有游戏挂接、账号操作或 UE 工具调用。

- 提取 50,002,822 B 的 `manifest.hgmmap`，解码得到 147,664,256 B。按公开格式独立重放 347,942 个 asset 路径 hash、254,722 个 bundle 版本 hash 及字典关联，全部一致。容器索引 CRC 已检查；工具未提供逐 payload CRC，另记录读取副本 SHA256。不能把这些校验说成当前 `GameAssembly` 的格式认证。
- 定位并复制 `CharacterNPR`、`CharacterNPR_Skin`、`CharacterNPR_Hair`、`CharacterNPR_Eye` 四个 shader bundle，共 42,385,677 B。使用文件内实际序列化 TypeTree 解析 `m_ParsedForm`，没有套用默认 Unity Shader schema；普通 `m_Name` 为空，实际名称位于 `m_ParsedForm.m_Name`。这是资源元数据，还不是恢复出的原始 HLSL。
- 庄方宜 root `chr_0030_zhuangfy_postmodel.prefab` 的 manifest 依赖闭包有 71 项。只读取 root 加七个目标材质 bundle，共 158,431 B，未复制其余闭包、模型或贴图。四个 shader bundle 均在该闭包中。
- 限定副本内 1,197 个小元数据对象按实际 TypeTree 完整读取。Prefab 有 52 个 renderer、55 个材质槽；其中 10 个槽能解析到已复制的七个材质。七个 Material 的 Shader PPtr 均以 external CAB 与 pathID 精确对应，映射如下。其余 45 个槽未复制，不能称完整角色导出；当前帧 LOD、启用组和实际绘制仍未确认。

| 目标材质 | 实际 Shader 引用 |
| --- | --- |
| hair / hairt | `HGRP/CharacterNPR_Hair` |
| face / body | `HGRP/CharacterNPR_Skin` |
| cloth01 / cloth02 | `HGRP/CharacterNPR` |
| iris | `HGRP/CharacterNPR_Eye` |

头发实例启用双组各向异性控制、独立镜面法线和镜面 ramp，支持继续研究“漫反射法线与镜面法线分开、两瓣高光”的结构。两套 cloth 使用通用 Shader，实例没有启用布料各向异性；其中一套保存的 `_Specular` 为 0，但保存值不能直接当作带贴图变体的高光门控。Face 实例使用专有 SDF、表情和高光分支，按本项目通用性约束不移植这些资产依赖。属性与关键词只证明配置；具体 BRDF 方程、参数标度和运行时激活需继续沿实际字节码验证。

### 本机头发编译程序已反汇编

四个 Shader 的实际 TypeTree suffix 均读取至精确对象结束，含 D3D11 与 Vulkan 平台和 LOD 分段压缩表。随后只解压 Hair 的 D3D11 LOD600 目录段与首代码段，结果分别为 6,952 B、16,739,056 B，均与表中长度一致。目录有 579 个记录；已扫描段内检查 442 个 DXBC 容器边界，首次落盘两个 Pixel Shader 候选，之后定向追加两个常态配置候选：

| 目录记录 | DXBC 大小 | 离线结果 |
| --- | --- | --- |
| 184 | 68,804 B | D3DDisassemble 成功，2,246 行 |
| 242 | 67,600 B | D3DDisassemble 成功，2,210 行 |
| 126 | 67,488 B | D3DDisassemble 成功，常态 hair 配置候选 |
| 131 | 76,900 B | D3DDisassemble 成功，ALPHABLEND hairt 配置候选 |

首次 184/242 两个候选的原始程序记录均与目标 hair 材质有六个关键词重合，不含额外的材质关键词；但包含不同的全局变体开关。四个候选没有 RDEF，不能从 DXBC 反射恢复原始资源名称。

进一步读取完整实际 TypeTree 后，确认 HG 定制分组把这些记录放在 `progVertex.m_PlayerSubPrograms[3]`，`progFragment` 为空并不代表没有 Pixel 程序映射。该分组与 `m_ParameterBlobIndices[3]` 一一对应：记录 94 直接引用 Blob184/参数70，记录 152 引用 Blob242/参数25；类型33、BlobIndex 和完整关键词集合均与真实二进制程序一致，Pixel stage 另由 DXBC 指令头确认。因此可以把二者关联到文件内 **LOD600 ForwardLit**，尚未证明游戏某帧选用了该 LOD 或变体。184 含 dissolve、242 含 dither 全局条件，不把它们当成已确认的庄方宜常态绘制。此前仅按 `progFragment` 空表得出“不能绑定 pass”的判断已由完整记录推翻。

首次两个候选均离线重建出 HLSL。各用独立 DXC 编译一次：242 的 `ps_6_0/main` 成功，184 因 `TEXCOORD1` 语义重叠失败，保留原始日志与失败结果。编译通过只证明重建文本的语法和 DXIL 验证通过，不证明与原 DXBC 等价。追加的 126/131 只反汇编，没有重复重建或编译。

实际参数记录已读至精确边界，并与四份 ASM 的全部资源声明逐项比对，分别覆盖 34、32、32、33 项，无遗漏或冲突。寄存器 packing 的读法是本批样本互证结果，不称官方公开格式定义。126/184/242 的 `_SplitNormalMap` 对应 t17、`_SpecRampMap` 对应 t13、`_LineMap` 对应 t11；131 分别变为 t19/t15/t13，因此不跨变体硬编码固定槽位。

同一 `_SplitNormalMap` 的 **RG/BA 分别重建表面法线与镜面方向法线**；两个 Z 都在 XY 强度缩放前重建，再经过几何切线基转换。LineMap 则只采 R 参与条带衰减，不进入法线重建：126/131 为 `1-R`，242 为 `R`，不能把 dither 候选的遮罩方向推广到常态。所选程序没有 active BumpMap，实例中的 BumpMap 也为空。这修正了仅观察运算邻接时把第二套法线误归给 LineMap 的判断。

变体参数布局给出了两组各向异性偏移、强度、EdgeFade、第二瓣范围/颜色的准确字节偏移，可对应 cb6[13..15] 的高光带运算；cb6[16..17] 是另一组 Line 控制。`UnityPerMaterial` 布局为 448 B，common 表 400 B 标为 partial；184 实际声明 448 B，而 126/242 只声明用到的 304 B，不能把布局长度当成每个变体的编译声明长度。指令中双偏移轴相对 half-vector-like 向量产生正弦幂高光带，部分条带还经过 ramp。这与插件现有双瓣 GGX 的公式不同；只提取单段幂函数、丢掉法线和 ramp 会改变效果，因此本批没有直接替换成所谓“精确复刻”。

### 衣物纹理分支的关键限制

通用 CharacterNPR 的 cloth01/cloth02 已分别关联到 LOD600 ForwardLit Pixel 程序 572/577 及参数 167/170；DXBC 为 65,288 B / 75,280 B，全部资源声明分别覆盖 34/36 项。只读取必要目录、代码段和参数段，不导出全库。

这两个实例实际启用 `_METALLICSPECGLOSSMAP`。Shader 自带属性说明将该纹理 RGBA 定义为 Metal/Spec/Shadow/Smooth；对应的 common 与变体参数没有 `_Metallic/_Smoothness/_Specular` 标量。两材质引用同一张外部 MetallicGlossMap，未复制或读取其像素。因而不能据 cloth02 保存的 `_Specular=0` 推断它在当前纹理分支没有高光，也不能用存档 Smoothness 的差值解释两者当前粗糙度。

忽略此前雨雪修改的分支，实际指令给出 `F0 = lerp(0.04 * map.g, base_color, map.r)`、`p = 1 - map.a`、`alpha = max(p², 0.007812)`，分布函数使用 `alpha²`。主光局部镜面还经过 SpecRamp 着色；`_SpecRampIridescentMode=0` 不关闭这条局部 ramp，只让环境 F0 保持原值。附加灯重新计算角度项，但复用先前 ramp 着色后的高光颜色，没有逐灯重新采图。该主光的方向构造还有额外数据，不能简写成普通 `normalize(L+V)` 后声称等价。

环境反射保留 `F0*A+B` 形式及独立边缘贡献，所以 F0 为 0 也不保证所有环境亮边消失。雨雪代码已编入候选，运行时开度未知。上述公式是 ASM 代数关系和原始参数表互证，不是原始 HLSL 标识符恢复。插件继续保留无需配套图的默认响应和明确的镜面控制；本批不照搬这些资产相关分支。

源码副本、列表日志与研究样本仅保留在 Git 忽略的 `Saved/Phase2Reference/`。本轮不向插件仓库加入商业角色资产或反编译 shader。后续自有实现继续满足 BaseColor 最小输入、几何通用着色与可选通用细节贴图约束，不改用户维护的 `TMMDAnimeFace.usf`。

## 下一步

当前展示图未达到终末地目标，且 SakuraTheater 实际使用 Genshin 方向父材质。先在已有同灯光比较链重拍真正的 Reference_Endfield，再按无景深、固定曝光的近景分别评估头发、皮肤与衣物。编译通过与看起来相似分开验收；目前的检查数不能充当风格相似的证据。

2026-10-11 10:55 SGT 已完成这项验证对象纠正：[真实 Endfield 方向的近景基线](Screenshots/2026-10-11-phase-two-endfield-baseline/README.md)。实际 family=2 和三种父材质通过，景深/运动模糊启动真实读回为 0；画面仍未达到目标。此批没有新增着色实现，也没有覆盖主视图自阴影；当前四个名称仍是共享核心 shader 的参数方向，不能称四款游戏的完整人物着色模型。

当前可见的问题与原因需要分开记录：头发泛白和纵向亮条是画面观察，镜面占比、底图画出的亮条和发流方向则尚未分离；脸部明暗弱需要先排除填光与缺失主视图自阴影；衣料层次不足需要核对各槽的实际参数。旧 Sakura 报告确实将手套、裤袜、靴子和外套等共 16 个槽归为 Cloth，但只读回第 15 槽的标量，不能断言全部槽最终粗糙度相同。下一批应保留同镜头、同姿态和同曝光的对照，分别关闭镜面、改变填光、改变一块衣料的粗糙度；未做这些实验前不把某个公式当成已证实根因。

头发与布料的少量候选已完成参数、绑定和主要高光数据流核对，继续工作以通用材质设计与插件实拍为主。UE 安装修复后，已独立复查官方 manifest 对应的 16 个关键文件哈希，确认 5.8.3/CL58210709；共享门禁中的离屏复测于 2026-10-11 10:30 SGT 完成，47 项通过、23 个材质和12张 PNG通过。[新引擎实拍与验证范围](Screenshots/2026-10-11-phase-two-material-v1-ue583/README.md)单独归档，与历史 5.8.2 结果区分。人物材质身份、shader 变体与画面未对应之前，改进只标为独立实现，不声称精确复刻终末地。

只读设计审查给出的后续顺序如下，**均未在本批实施**：

1. 先补直接输入的 N/T 正交和缺省输出兜底。共享节点的正常路径已具备几何发流；但镜面法线为 Z、发流输入为零或同向时，Character 的 Z 兜底仍可能平行于法线。未连条带遮罩的 0 输入也只被夹到 0.5，而正常无细节路径应为 1。应对这些具体接口输入验证，不能把正常路径已有分离重复称为新功能。
2. 再增加默认 0 的独立正弦幂条带控制，保留当前双 GGX，通过实际灯光和通用几何法线/发流计算，分别做带宽与偏移的引擎 A/B；不要求角色 SplitNormalMap 或 SpecRampMap。
3. 最后评估默认 0 的几何镜面法线平滑，只在碎法线确实打断亮带的镜头启用。限定 Hair 和有效头骨范围，保持漫反射不变，长发尾和缺头骨输入完整回退。
