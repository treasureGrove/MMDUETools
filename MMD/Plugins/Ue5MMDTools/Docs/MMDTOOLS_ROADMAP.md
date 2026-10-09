# MMDUETools 功能路线图（ROADMAP）

> 状态：**2026-09-23 制定的原始路线图**。后续导入导出、动画与编辑工具规划继续保留；阶段进展以 [STAGE_RENDER_PRESETS.md](STAGE_RENDER_PRESETS.md) 和当前源码为准。
>
> 原“现有 shader 全冻结、不得 include / 修改”的约束已被本轮用户明确授权的渲染迭代取代。后文相关冻结表述与渲染现状保留为历史方案，不再作为当前限制；通用性与插件分发要求仍遵守 AGENTS.md。
>
> `TMMDAnimeFace.usf` 仍由用户自行维护，AI 不修改。

---

## 0. 市面插件功能基线（调研结论）

| 能力 | Blender MMD Tools（事实标准） | 本插件现状 | 差距 |
|---|---|---|---|
| PMX/PMD 导入 | 粒度开关（网格/骨骼/物理/贴图/形态） | 全量导入，无粒度开关 | 缺开关 |
| toon 贴图 | 自定义 + 共享 toon01~10 | **解析层有数据，导入层丢弃** | Phase 1 补 |
| sphere 贴图 | 加算/乘算/副画面(matcap) 三模式 | 已导入，`sphere_mode` 参数已有 | 基本齐 |
| edge 边线 | 材质边线渲染 | 数据进材质参数，无 shader 消费 | Phase 1 先入参，渲染待定 |
| PMX 导出 | round-trip | 无 | Phase 2 |
| VMD 导出 | 有 | 无 | Phase 2 |
| 骨骼/IK | MMD IK 解算 + 编辑 | IKRig 构建 + DataAsset 存 IK 链 | Phase 3 面板化 |
| 物理 | 刚体/关节转换 + 编辑 | 导入时一次性 Bullet 构建，无编辑 API | Phase 4 |
| K帧 | VMD 关键帧 / 时间轴 | 仅 LevelSequence 合成 | Phase 3 |

## 1. 目录规范（新文件一律按此落位）

| 路径 | 用途 |
|---|---|
| `Shaders/MMDStandard/` | 新通用 shader 系列（自包含，不引用 Core/TMMDShader） |
| `Scripts/MMDStandard/` | Phase 1 配套 Python（生成贴图/材质/验证/截图） |
| `Scripts/`（平铺） | 现有 UniversalAnimeToon/ReferenceToon 脚本，保持不动 |
| `Content/Resources/ToonRamps/` | MMD 共享 toon01~10 等效 ramp 贴图（程序生成） |
| `Content/Resources/MaterialInstance/` | 父材质资产（M_MMDStandard_Opaque 等） |
| `Source/.../Import/` | 导入导出 C++（现有） |
| `Source/.../UI/` | 面板 C++（现有；新面板按 `MMD<域>Panel` 命名） |
| `Saved/MMDStandardScreenshots/` | 截图闭环输出（运行时生成，不进 git） |

命名：C++ 类型 PascalCase；shader struct `TMMD` 前缀 + PascalCase，变量 snake_case；Python 脚本 PascalCase 动词开头（`Create*/Validate*/Screenshot*`）。

## 2. Phase 1 — MMDStandard 通用 shader 系列 + toon 导入 + 截图闭环（本轮）

目标：参考 MMD 原生渲染管线，写几套**只依赖 PMX 自带素材**的通用 shader，并建立可重复的截图验证闭环。
所有 shader 遵守 AGENTS.md 铁律：最少输入 = 一张 base_color；贴图缺失自动兜底；不依赖 UV 布局。

### 2.1 Shader（`Shaders/MMDStandard/`，自包含）

| 文件 | struct | 定位 |
|---|---|---|
| `MMDStandardCore.ush` | `TMMDStandardLight` | 独立工具库：LightDataRT 采样（16 灯 P/C/D/T）、主光方向、场景阴影投影（复制自 Core 逻辑，**不 include Core**）、天空 SH、matcap。与冻结的 `Core/MMDToonLighting.ush` 完全解耦 |
| `MMDStandard.usf` | `TMMDStandard` | **全素材版（忠实 MMD 原生管线）**：`base × diffuse × toon_ramp(n_dot_l)` + ambient + sphere 三模式（mul/add/subtex matcap）+ blinn-phong 高光（MMD 风格：高光叠在 toon 之上，不参与 ramp 门控）+ 场景阴影。输入：base_color / toon_tex / sphere_tex / sphere_mode / specular_color / specular_power / ambient_color / diffuse_color / edge_color / edge_size（后两项先入参） |
| `MMDLiteToon.usf` | `TMMDLiteToon` | **极简版**：仅 base_color，程序化两段 ramp + 阴影色派生 + rim，验证"最少输入"铁律，作为无贴图模型/快速预览的兜底 |

MMD toon 采样约定：`toon_tex` 为 256×1 灰度 ramp，`u = saturate(n_dot_l * 0.5 + 0.5)`（MMD 原生 Half-Lambert 映射），`v = 0.5`；缺贴图时程序化 `pow(half_lambert, 0.6)` 近似 toon01。

### 2.2 toon 贴图导入（C++，默认关闭的开关）

- `TMMDMeshBuilder`：新增可选路径 `bUseStandardMaterials`（默认 false，走现有逻辑不变）。
  开启时：父材质用 `M_MMDStandard_Opaque / _Translucent`；导入自定义 toon 贴图（`UseSharedToon==0`）；共享 toon（编号 0~9）取 `/Ue5MMDTools/Resources/ToonRamps/T_MMDToon01~10`；材质实例设 `ToonTex` 等参数。
- `MMDImportSetting` 面板加一个 CheckBox「使用 MMD Standard 材质」。

### 2.3 Python 闭环（`Scripts/MMDStandard/`）

```
CreateToonRampTextures.py     生成共享 toon01~10 ramp 资产（一次性）
CreateMMDStandardMaterials.py 建父材质 M_MMDStandard_Opaque/_Translucent
ValidateMMDStandardMaterials.py  结构检查 + recompile（HLSL 编译验证）
ScreenshotMMDStandard.py      截图闭环（见 2.4）
```

### 2.4 截图闭环流程

```
[编辑器 -ExecutePythonScript]
1. Validate：材质 recompile 无错
2. Apply：对场景 MMD Actor 逐 slot 生成 MI（from PMX 原始数据：base/toon/sphere）
3. Shot：视角对准模型 → console `HighResShot 1920x1080 filename=<绝对路径>`
4. Wait：轮询文件落盘（超时 60s）
5. Check：读回像素，断言非全黑/非全白、亮度直方图有分布
6. Report：打印 PASS/FAIL + 截图路径
```

运行方式：`UnrealEditor-Cmd.exe MMD.uproject -ExecutePythonScript=.../ScreenshotMMDStandard.py -TestExit="Automation Test Queue Empty"`（或编辑器内直接跑）。

## 3. Phase 2 — 导入导出

- **VMD 导出**：Sequencer/AnimSequence → VMD（骨骼关键帧 + 表情曲线 + 相机）。复用 `TVMDParser` 的数据结构反向序列化。
- **PMX 导出（round-trip）**：`MMDModelDataAsset` + SkeletalMesh → PMX 2.0。顶点/面/材质/骨骼/形态；刚体关节从 AnimNode 数据回写。目标：MMD Tools 导回验证。
- 导入粒度开关（贴图/物理/形态/边线 可选导入）。

## 4. Phase 3 — 动画工具（K帧 / IK）

- **MMD K帧时间轴面板**（`MMDKeyframePanel`，Slate）：骨骼/表情关键帧列表 + 曲线预览，支持 VMD 风格插值（贝塞尔控制点），写入 LevelSequence 或导出 VMD。
- **MMD IK 控制面板**（`MMDIKControlPanel`）：列出 PMX IK 链（已有 DataAsset 数据），开关 IK、拖拽 IK 目标（脚/手柄），与 IKRig 联动。

## 5. Phase 4 — 物理编辑器

- `FMMDPhysicsSimulator` 增加**局部重建 API**（`UpdateRigidBody(index)` / `AddJoint` / `RemoveRigidBody`，避免全量 Shutdown）。
- **MMD 物理编辑面板**（`MMDPhysicsPanel`）：刚体/关节树形列表（按骨骼分组）+ 详情编辑（形状/质量/碰撞组/约束角）+ 视口联动高亮 + 热重载模拟。

## 6. 验证纪律

- 每个 Phase 交付物必须过对应闭环：shader → recompile + 截图像素检查；C++ → Win64 Development 编译通过；面板 → 至少一张操作截图。
- 截图/验证产物放 `Saved/MMDStandardScreenshots/`，不进 git。
