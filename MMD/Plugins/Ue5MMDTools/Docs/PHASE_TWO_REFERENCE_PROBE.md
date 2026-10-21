# 二期参考：无账号离线 shader 检查

**历史快照（2026-10-19 标记）**：本文保留原批次的研究、状态与验证，内部“当前／下一步”和历史聊天指令不作为今日架构或执行约束。现状先读[当前架构](RENDERING_ARCHITECTURE.md)和[接手状态](ENDFIELD_V2_FOR_GPT.md)，原日期与证据保持。

前置检查日期：**2026-10-11**。本文件记录尚未取得账号时的只读客户端检查；这一批没有运行游戏、采集角色画面或修改插件渲染代码。随后用户反馈工具检测，研究转向网络资料，新增结果见 [终末地公开资料与离线方法](PHASE_TWO_PUBLIC_REFERENCES.md)。

## 本次结论

离线读取编译 shader 不要求登录账号。已验证《无限暖暖》的一个未加密 shader 容器可以列目录、提取库头、解压代码组并反汇编样本，其中一份成功转换为重建 HLSL 并重新编译。**尚未定位人物材质绑定，尚未取得可确认属于人物的 shader，也没有取得原始 HLSL。**

编译程序、重建的 HLSL 和作者的原始源码需要区分。Unity 的构建会编译平台相关 shader；Microsoft 的 `D3DDisassemble` 接收编译代码并输出汇编。这些输出可以用于研究数学运算、资源访问和分支，但不能据此声称恢复了原始变量名、注释或完整材质工程。[Unity 2021.3 编译说明](https://docs.unity3d.com/2021.3/Documentation/Manual/shader-compilation.html)、[Microsoft D3DDisassemble](https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3ddisassemble)。

## 已验证入口

| 客户端 | 本次实测结果 | 当前限制 |
|---|---|---|
| 无限暖暖 | `Default_Shader_2816_P.utoc/.ucas` 未设置加密标记，UE 5.8 UnrealPak 列表与过滤提取成功；两个库头解析完整，三个内容库代码组解压成功，五个 DXBC 样本反汇编成功，一份重建 HLSL 编译成功 | shader map 以哈希保存；人物、服装、头发与材质实例的对应关系没有取得 |
| 终末地 | 五个裸 Unity SerializedFile v22 可列对象；确认 `2021.3.34f5`；`resources.assets` 有 39 个 Shader 对象，`sharedassets1.assets` 另有一个 | 标准 UnityPy 1.25.2 未成功完整解析这些 Shader/Material 对象；VFS 不呈裸 UnityFS，需要进一步核对客户端资源格式 |
| 鸣潮 | 核实 UE Pak v12 容器；最小三个游戏 Pak 均未成功列目录 | optional Pak 明确缺少加密 key，另外两份出现索引断言；本次没有取得 shader 库 |

终末地裸资源中可辨认 `HGRP/Lit`、`HGRP/UI/Default`、`HGRP/GPUParticle/FallenLeaves`，以及视频、字体和工具 shader 的字符串；`HGRP/Lit` 是待研究的通用光照入口，不能直接认定为人物材质。`TextMeshPro/Distance Field` 属于文字渲染，不是人物脸部 SDF 证据。

## 暖暖的实测细节

来源容器：

```text
E:\Games\Launchers\InfinityNikki\InfinityNikki Launcher\InfinityNikki\X6Game\Content\Paks\Default_Shader_2816_P.utoc
```

- TOC v6，flags `12 = Signed | Indexed`；没有 `Encrypted`。同名 `.pak` 的索引加密，不影响这组 IoStore 的独立读取。
- 目录列表有 **1,231 个条目**：1,228 个 ShaderCode chunks、两个 ShaderCodeLibrary、一个 ContainerHeader。代码块数量不是单个 shader 数量。
- `ShaderArchive-Global-PCD3D_SM5-PCD3D_SM5.ushaderbytecode`：271,184 B；6,733 个 shader 条目、468 个 shader maps、592 个代码组。
- `ShaderArchive-X6Game_Chunk0-PCD3D_SM5-PCD3D_SM5.ushaderbytecode`：299,984 B；7,245 个 shader 条目、242 个 shader maps、636 个代码组。全部七个序列化数组解析至文件末尾，没有剩余字节。
- 两个库各抽检前三组，**六组压缩代码的 BLAKE3-160 哈希均与容器列表一致**。这是 IO 容器哈希，不是 SHA1。
- 内容库前三组按库头大小解压为 4,887 / 98,556 / 18,876 B，返回长度均吻合。第一组两个、第三组三个 DXBC 成功 `D3DDisassemble`，包含四个 `ps_5_0` 和一个 `vs_5_0`。第二组未成功识别 DXBC，不计入反汇编成功数。
- `group_0002_shader_001.dxbc` 经本机 HLSLDecompiler 转换为 14,751 B 的重建 HLSL；本机 x64 DXC 以 `ps_6_0 / main` 重新编译成功。该检查只证明重建代码可编译，不证明与原始程序逐像素等价。输出使用 `CB1_m0`、`T0` 等无语义名字，含 Texture3D/通用场景数据访问，仍不能确认属于人物表面材质。
- IoStore 外层 compression block 均为 None，代码组内部仍有压缩。通用跨块读取必须按 TOC block offsets 拼接，不能假设磁盘连续。

样本只能证明离线研究路线可用；像素 shader 中出现纹理采样、法线或高光运算，仍不足以证明它属于人物皮肤或发丝。

## 本地证据与后续制作约束

原始游戏数据、反汇编样本、临时工具与完整日志仅保存在被 Git 忽略的 `Saved/Phase2Reference/`，不归档为插件分发资源。仓库记录检查方法、结论和限制。

本地主要报告：

```text
Saved/Phase2Reference/StaticProbe/nikki-shader-iostore.csv
Saved/Phase2Reference/StaticProbe/nikki-library-layout.json
Saved/Phase2Reference/StaticProbe/nikki-disassembly-samples.json
Saved/Phase2Reference/StaticProbe/NikkiSamples/
Saved/Phase2Reference/StaticProbe/NikkiSamples/group_0002_shader_001.reconstructed.hlsl
Saved/Phase2Reference/StaticProbe/endfield-unity-assets.json
Saved/Phase2Reference/StaticProbe/endfield-object-layout.json
```

解析工具为隔离 Python 3.12 环境中的 UnityPy 1.25.2、blake3 1.0.11，以及本机 UE 5.8 UnrealPak、UE 附带的 Oodle 解压库和 Windows `d3dcompiler_47.dll`；HLSL 重建与编译使用已有 `E:/RE4_Shader_Extract/tools/` 下的 HLSLDecompiler 和 x64 DXC。没有向游戏目录写入文件或尝试加密密钥。

二期后续先核对 shader map 与人物材质的关联，再判断哪些光照和材质响应值得实现。自有实现继续保留 BaseColor 最小输入、几何通用着色、可选通用细节贴图和零强度回退；不把专有模型 UV 或配套人物贴图变成插件必需输入，不改用户维护的 `TMMDAnimeFace.usf`。

本批没有新的引擎截图。既有一期截图捕获于 **2026-10-09**，不得作为本次离线研究的渲染验证。
