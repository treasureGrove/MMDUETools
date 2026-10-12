# 终末地角色渲染复刻

本文记录终末地角色渲染在本插件的落地实现、依据来源、与真实实现的差异，以及尚未验证的部分。
日期：2026-10-12，时区 Asia/Singapore。

## 依据来源（重要）

本轮**不采用网上逐步复刻教程**，依据是本机已有的逆向数据与完整实现：

| 来源 | 位置 | 用途 |
| --- | --- | --- |
| 真实 shader 属性表 | `MMD/Saved/Phase2Reference/StaticProbe/CharacterShaderMetadata/` | `HGRP/CharacterNPR`(256 项)、`_Skin`(131)、`_Hair`(115)、`_Eye`(78) 的真实参数名与默认值 |
| 反编译 HLSL | `StaticProbe/EndfieldBytecodeSamples/Hair|Cloth/` | DXBC → HLSL 的数学结构（发丝各向异性、布料的真实写法） |
| 完整 URP 复刻工程 | `Tooling/EndfieldCharacterRendering-public-code/` | `LightningSkin` / `LightningFaceSDF` / `LightningHair` 的完整光照库（980 行），提供 `sigmoidSharp`、`DepthRim`、球面法线、JGT 透散、等距柱面切线的可运行实现 |
| 完整 MME 复刻 | `Tooling/Endfield-MME-public-code/` | `.fx` 系列与材质控制器规格，交叉核对参数语义 |
| 真实材质引用 | `StaticProbe/CharacterMaterialMetadata/Zhuangfy/` | 庄方宜角色的 Material → CharacterNPR 引用链 |

**反编译 HLSL 的局限**（原始记录已声明）：DXC 重编译未通过（`dxc_exit_code: 1`），只是
"reconstructed HLSL"，**不能证明与原版等价**。因此本实现是"按真实参数表与完整参考实现重建的近似"，
不宣称是商业游戏的精确 shader 复刻。

## 实现结构

### 新增文件

| 文件 | 作用 |
| --- | --- |
| `Shaders/Core/TMMDEndfieldLighting.ush` | 终末地光照核心库（struct `TMMDEndfieldLighting`，21 个成员函数） |
| `Shaders/TMMDShader/TMMDEndfieldCharacter.usf` | 角色着色入口（struct `TMMDEndfieldCharacter`，64 个输出形参 / 57 个材质输入） |
| `Source/.../Public/Editor/MMDEndfieldFaceSdfBaker.h` | 脸部 SDF 烘焙接口与设置/结果结构 |
| `Source/.../Private/Editor/MMDEndfieldFaceSdfBaker.cpp` | 脸部 SDF 烘焙实现（头部基推导 + 角度位掩码 + 贴图保存） |
| `Source/.../Private/Editor/MMDEndfieldBakeSdfCommandlet.cpp/.h` | SDF 烘焙入口（`-run=MMDEndfieldBakeSdf`） |
| `Source/.../Private/Editor/MMDEndfieldPresetsCommandlet.cpp/.h` | 父材质与预设生成器（`-run=MMDEndfieldPresets`） |
| `Scripts/validate_endfield_shaders.py` | shader 结构静态检查 |
| `Scripts/validate_endfield_sources.py` | C++ 结构/接线静态检查 |
| `Scripts/Invoke-EndfieldPresets.ps1` | 端到端验证（编译 → 生成 → 校验） |

### 生成资产（正式入库，随插件分发）

| 资产 | 路径 |
| --- | --- |
| 不透明父材质 | `/Ue5MMDTools/Stages/Materials/Endfield/M_MMD_Endfield_Opaque` |
| 遮罩父材质 | `/Ue5MMDTools/Stages/Materials/Endfield/M_MMD_Endfield_Masked` |
| 半透明父材质 | `/Ue5MMDTools/Stages/Materials/Endfield/M_MMD_Endfield_Translucent` |
| 渲染预设 | `/Ue5MMDTools/Stages/Presets/Endfield/DA_Render_Endfield` |

这与旧 `MMDEndfieldPhase2` 有本质区别：旧资产只写在 `Saved` 私有目录、不入 Content、
拉源码不会启用；本代资产写入插件 `Content`，**打开工程即可在预设列表看到"终末地"**。

## 复刻的真实终末地做法

### 阴影：sigmoid 硬阈值，几乎没有半影

真实实现用 `sigmoidSharp(x, center, sharpness) = rcp(pow(100000, (x-center)*(-3*sharpness)) + 1)`
把阴影做成接近二值。这是终末地脸部阴影"无明显半影"的直接来源。
本实现的三段阴影取 min，与真实结构一致：

```
shadow = min(scene_shadow_hardened, lambert_shadow, sdf_shadow)
```

对应真实参数 `_SceneShadowCenter`/`_SceneShadowSharpness`（默认 0/0.1）、
`_HalfLambertShadowCenter`/`_HalfLambertShadowSharpness`（默认 0.5/0.1）。

### 脸部：SDF 方位角阈值图

真实 `LightningFaceSDF` 的关键是：SDF 图存的不是"阴影形状"，而是**逐 texel 从受光转为阴影的光照方位角阈值**：

```hlsl
sdfShadow = (sdf.r + sdf.g) / 2;
sdfShadow = sigmoidSharp(sdfShadow - _AngleThreshold, _SDFShadowCenter, _SDFSharpness);
```

光照绕头旋转时，`_AngleThreshold` 变化，阴影边界随之平滑移动，因此脸不会出现多边形硬边。
光照在另一侧时把 UV 镜像（`if(_FlipSDFThreshold < 0) sdfUV.x = 1 - sdfUV.x`）。

**本实现支持这一真实路径**（连 `SDFMap` 并把 `UseSdfMap` 设为 1），
同时提供**自动烘焙工具**（见下），因此 SDF 不是手绘配套贴图，而是可再生的派生资产。

### 头发：等距柱面切线 + 各向异性高光

真实实现用 `GetEquirectTangent`（复刻 Blender 的等距柱面切线节点）构造切线场，再做：

```hlsl
half3 T = GetEquirectTangent(normalWS);
half ToH = dot(T, H);
half sinTH = sqrt(1 - ToH * ToH);
half anisoHL = saturate(pow(sinTH, 5));
anisoHL = smoothstep(0.995, 1.01, anisoHL);
```

真实代码自注 "this is quite hard to control :("。本实现在**保留原公式的同时**，
用屏幕导数 `fwidth` 把阈值展宽到至少一个像素，避免远距离或斜视角下细亮线闪烁或整片消失——
这是对原实现的稳健性改进，几何形状不变。

真实材质的 `_AnisotropyValue` 0.35、`_AnisotropyIntensity` 1.0、第二层 `_AnisotropyValue2` 0.4
也已接入为 GDDR 双瓣。

### 轮廓光：屏幕空间深度壳

真实 `DepthRim` 沿视图空间法线偏移采样邻域深度，得到贴合剪影的稳定宽度轮廓光。
**注意**：参考工程写作 `clamp(0.0, 5.0, linearDepth2 - linearDepth)`，而 HLSL 的
`clamp(x, min, max)` 参数顺序是 `clamp(值, 下限, 上限)`，该写法结果恒为 0。
本实现按正确语义修正，并修正了采样器/深度来源（见"关键取舍"）。

### 皮肤透散与脸部球面法线

真实 `LightningSkin` 的 JGT 包裹透散在原版里被注释掉（`// directLightningDiffuse += sss_part`），
本实现接入但默认 `SSSValue = 0`，行为与原版一致，需要时可打开。
脸部用球面法线替代网格法线算高光（`ComputeAndBlendSphereNormal`），消除鼻翼/眼窝的法线撕裂。

## 关键取舍与差异

必须明确说明的三点：

**1. 场景深度改为显式输入，而不是 `CalcSceneDepth()`。**
引擎的 `SceneTexturesStruct` 只在 translucent 基 pass 被 `#define` 重定向
（`BasePassPixelShader.usf:24`），opaque/masked 下直接调 `CalcSceneDepth` **无法编译**。
因此轮廓光改为：材质侧传入 `scene_depth_tex` + `UseSceneDepthRim` 开关；
关闭时退回几何菲涅耳轮廓光（`N·V`），保证只连 `base_color` 也完整可用。

**2. SDF / Ramp / 场景深度全部是可选输入，默认走程序化兜底。**
现有 `AGENTS.md` 的通用性铁律要求"只连一张 base_color 就要能完整着色、不依赖配套贴图"。
本实现的处理是：

| 可选输入 | 开关默认 | 未连接时的行为 |
| --- | --- | --- |
| `SDFMap` | `UseSdfMap = 0` | 脸部退回半兰伯特硬化阴影（通用，不依赖 UV） |
| `DiffuseRampMap` | `UseRampMap = 0` | 用解析式两段 toon 曲线拟合 LUT |
| `SceneDepthTexture` | `UseSceneDepthRim = 0` | 用几何菲涅耳轮廓光 |

这既保留了真实做法（连上资源即为真路径），又满足通用性铁律（不连也能正常显示）。
**建议**：若要完全还原终末地，应连接这三张资源；它们是模型相关的，但都可自动生成。

**3. `_SDFMask` 的 R/G/B 语义已接入但非必需。**
真实 `_SDFMask` 是 `RimMask/SDFMask/FlatSHMask` 三通道合一。本实现按角色分路
分别消费对应语义（轮廓遮罩、SDF 有效区、平滑 SH），通过独立标量参数暴露，
避免强制要求一张打包贴图。

## 脸部 SDF 自动烘焙工具

`MMDEndfieldFaceSdfBaker.cpp` 把 SDF 从"手绘配套资产"变成"可从网格自动生成的派生资产"。

算法：取 LOD0 的顶点位置/法线/UV，用头部包围球建立头部局部基；判定脸部顶点
（法线朝前 + 在头部半径内 + 下颌以上 + 头顶以下）；对每个脸部 texel，
在水平面内绕头采样 64 个光照方位角，记录"从正前方起连续受光的最远角度"作为阈值
（即真实 SDF 的语义：数值越大越晚变暗）。

输出 RGBA8：`R/G` = 归一化阈值（乘 π 得弧度），`B` = 正面受光占比，`A` = 脸部遮罩。

内存优化：逐 texel 用 `uint64` 位掩码记录受光角度，512² 仅需 2 MB
（若按"逐 texel × 逐角度计数"实现则需 67 MB）。

**当前状态**：烘焙算法与命令入口均已实现（`-run=MMDEndfieldBakeSdf`），
但**尚未在本机编译或运行过**（沙箱是 Linux，无法执行 UE 的 Windows 二进制）。
头部基优先取 `Head` 骨的参考姿势；无骨骼时退回包围盒推理并在报告里标注
`head_frame_source`，识别不到任何脸部 texel 时拒绝写出空白图。

```
UnrealEditor-Cmd.exe MMD.uproject -run=MMDEndfieldBakeSdf \
  -Mesh=/Game/MMDModels/<模型>/Model/<资产>.<资产> \
  -Sdf=/Game/MMDModels/<模型>/Textures/T_<模型>_FaceSDF \
  -Report=D:\...\bake-report.json -Generate [-HeadBone=Head] [-Size=512] [-Tga=D:\...\debug.tga]
```

烘好之后，把父材质的 `SDFMap` 指向该贴图并把 `UseSdfMap` 设为 1，即启用真实终末地的脸部 SDF 路径。

## 静态检查

沙箱内没有 HLSL 编译器（UE 只随 Windows 版 DXC/DLL 分发，且无法在 Linux 执行 `.exe`），
因此提供两个静态检查器，它们已经捕获了两个**真实的编译级错误**：

1. 采样器命名：UE 自动生成的采样器名是「输入名 + `Sampler`」（`light_data_texSampler`），
   误写成 `light_data_tex_sampler` 会编译失败。
2. 纹理参数类型：`UMaterialExpressionTextureObjectParameter::Texture` 是 `UTexture*`，
   RenderTarget 不能按 `UTexture2D*` 传参。

检查器还会做输入名/顺序逐一对照、白名单一致性、签名覆盖完整性等核对。

```
python -I Scripts/validate_endfield_shaders.py <插件根目录>
python -I Scripts/validate_endfield_sources.py <插件根目录>
```

**静态检查不等于编译通过，更不等于画面达标。**

## 如何运行

沙箱（Linux）无法执行 UE，必须在 Windows 主机上运行：

```powershell
pwsh -File MMD\Plugins\Ue5MMDTools\Scripts\Invoke-EndfieldPresets.ps1
```

脚本会依次：静态检查 → 用项目既有 `BuildPlugin.ps1` 编译 Editor 模块 →
运行 `-run=MMDEndfieldPresets -Generate` 生成资产 → 校验 JSON 报告。

只想预检不写盘：

```powershell
pwsh -File MMD\Plugins\Ue5MMDTools\Scripts\Invoke-EndfieldPresets.ps1 -NoGenerate
```

生成器只允许写四个白名单包，其余一律拒绝；没有 `-Generate` 时不做任何写入。

## 尚未完成的部分

诚实列出，不把"验证链通过"当成画质成果：

1. **未在本机编译或运行过。** 沙箱是 Linux，UE 二进制是 Windows `.exe`（`Exec format error`，无 wine）。
   所有 C++ 与 HLSL 只经过静态检查，**可能存在静态检查无法发现的编译错误**。
2. **画面未经验收。** 没有截图，没有与终末地的视觉对比。能否达到目标必须人工看图判断。
3. **SDF 烘焙未实跑。** 算法与入口已实现，但未经真实模型运行，
   头部基推导与脸部 texel 判定是否覆盖插件测试夹具尚未验证；
   包围盒兜底路径的角度偏差也未实测。
4. **面部与头发的最终形状未经调参。** 发丝高光阈值、SDF 锐度、各向异性强度等参数
   目前取真实默认值，未针对插件测试角色（Miku 夹具）调优。
5. **未接入描边。** 真实终末地有 `_OutlineOffsetZ` 平滑法线外扩描边与眼部
   `EyeThrough`/`BrowOverlay`/`HairShadow` 分层；本轮只做角色主着色，
   描边走既有 `TMMDOutline.usf` 与预设的细描边参数。
6. **眼球分路为简化实现。** 真实 `CharacterNPR_Eye` 有独立的 `_EyeHighLight` /
   `_EyeScatteringColor` / 视差 `_ParallaxScale` 与眼部透明通道；本实现只按
   `MaterialRole = 5` 走了湿润高光与低粗糙度，未复刻眼部专用分层。
7. **模型资产差距。** 终末地角色有长外套、大褶皱、眼部几何与特定发片形状，
   插件测试夹具没有这些结构；shader 无法生成几何缺失带来的外观。

## 与旧二期实验的关系

旧的 `MMDEndfieldPhase2`（`Shaders/Core/TMMDEndfieldPhase2BRDF.ush`、
`Shaders/TMMDShader/TMMDEndfieldPhase2Character.usf`、`MMDEndfieldPhase2Commandlet`）
是**机械隔离旧 Revision4 算法并固定参数**，交接文档已明确承认"未落实用户思路、
长亮轨与灰脸仍在、画质未达目标"。

本代的区别是：按真实逆向数据与完整参考实现**重写光照结构**（sigmoid 硬阈值、
SDF 方位角、等距柱面切线各向异性、屏幕空间深度轮廓），并把资产**正式入库**。

旧文件保留未删（不擅自恢复或删除用户文件）。若确认新代可用，可另行清理。
