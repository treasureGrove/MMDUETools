
# Ue5MMDTools — UE5 MMD 导入插件

一个用于将 MMD（MikuMikuDance）模型和动画导入 Unreal Engine 5 的编辑器插件。

---

## 使用教程

下载后将 Plugins 文件夹复制到你的 UE5 工程目录，在 UE5 中启用该插件：

<img width="2556" height="507" alt="image" src="https://github.com/user-attachments/assets/34db6af7-8de2-455e-8c66-3fe83762a915" />

导入后在 UE5 插件面板里使用：

<img width="798" height="792" alt="image" src="https://github.com/user-attachments/assets/1c5c4d43-480d-4a4e-91a9-e905e94f4fc4" />

点击"导入MMD模型"：

<img width="1626" height="648" alt="image" src="https://github.com/user-attachments/assets/078f6b85-bc5c-4959-ae0c-669c37bb0079" />

导入后可以看到 Actor 已在插件和蓝图面板中生成：

<img width="1914" height="1026" alt="image" src="https://github.com/user-attachments/assets/9ab3b650-4054-4ed4-8882-56c2c63d848c" />

对应的 Actor 和骨骼重定向器已经生成好了：

<img width="1611" height="621" alt="image" src="https://github.com/user-attachments/assets/a37aea3c-5c72-418b-9920-711de3a3d9f8" />

---

## 代码架构总结

### 目录结构

```
Plugins/Ue5MMDTools/Source/Ue5MMDTools/
├── Public/
│   ├── TPMXParser.h          # PMX 模型文件数据结构与解析器接口
│   ├── TVMDParser.h          # VMD 动画文件数据结构与解析器接口
│   ├── TMMDMeshBuilder.h     # 从 PMX 数据构建 UE5 资产的工具类
│   ├── AMMDActor.h           # 场景中 MMD 模型的 Actor 类
│   ├── MMDImportSetting.h    # 插件导入面板 UI 组件
│   ├── MMDViewPanel.h        # 插件内嵌3D预览视口组件
│   ├── MMDPhysicsSimulator.h # 基于 Bullet Physics 的 MMD 物理模拟器
│   ├── AGN_MMDSkeletalControl.h # 自定义 AnimNode（驱动 MMD 物理）
│   └── Ue5MMDTools.h         # 插件模块入口
└── Private/
    ├── TPMXParser.cpp        # PMX 文件二进制解析实现
    ├── TVMDParser.cpp        # VMD 文件二进制解析实现（含 SJIS 解码）
    ├── TMMDMeshBuilder.cpp   # 骨骼网格、材质、IK Rig、动画蓝图构建
    ├── AMMDActor.cpp         # Actor 初始化与 PMX 导入流程
    ├── MMDImportSetting.cpp  # UI 按钮事件、文件对话框、导入进度反馈
    ├── MMDViewPanel.cpp      # 编辑器视口客户端、模型预览、拖放支持
    ├── MMDPhysicsSimulator.cpp # Bullet 世界初始化、刚体/约束/模拟步进
    └── AGN_MMDSkeletalControl.cpp # AnimNode 评估、物理初始化、AnimGraph 节点辅助
```

### 各模块说明

#### 1. TPMXParser — PMX 模型解析器
- 完整支持 PMX 2.0/2.1 规范
- 解析内容：顶点、法线、UV、材质、纹理、骨骼（含 IK）、变形（Morph）、显示帧、刚体、关节、软体
- 数据保存在 `PMXDatas` 结构体中，供后续各模块使用

#### 2. TVMDParser — VMD 动画解析器
- 支持 VMD 全格式：骨骼帧、表情帧、镜头帧、光源帧、阴影帧、IK帧
- 使用 Windows API（`MultiByteToWideChar`）将 Shift-JIS 编码的骨骼/表情名称转为 Unicode
- 解析结果存于 `VMDData` 结构体

#### 3. TMMDMeshBuilder — UE5 资产构建器
- `BuildSkeletalMeshFromPMX`：创建带材质、骨骼的 `USkeletalMesh`；自动导入纹理并创建 `UMaterialInstanceConstant`
- `BuildIKRigFromPMX`：自动识别常见 MMD 骨骼名称（日文/英文），创建 `UIKRigDefinition`，配置重定向链与 IK Goal
- `BuildIKRetargeterFromPMX`：创建 `UIKRetargeter`，用于与 UE 标准角色的动画重定向
- `BuildAnimBlueprint`：创建动画蓝图，并通过 `FMMDAnimGraphHelper` 自动插入 MMD 物理控制节点

#### 4. AMMDActor — 场景 Actor
- 包含 `UCapsuleComponent`（碰撞）和 `USkeletalMeshComponent`（骨骼网格）
- `SetupComponents(FilePath)`：调用解析器和构建器完成全套资产生成，并在关卡中生成可用的带物理的角色
- `OnConstruction`（编辑器）：在编辑器预览中初始化动画实例

#### 5. MMDImportSetting — 导入设置面板
- 工具栏按钮：导入MMD模型 / 导入VMD动画 / 导入静态网格 / 选中 / 移动 / 缩放
- 状态栏 `StatusText` 显示彩色进度消息（绿色=成功/信息，黄色=警告，红色=错误）
- 通过静态全局实例（`CurrentInstance`）支持从任意位置更新状态消息

#### 6. MMDViewPanel — 预览视口
- 继承自 `SEditorViewport`，内嵌独立的 `FAdvancedPreviewScene`
- 支持鼠标轨道控制、光照模式、选中高亮、拖放资产等编辑器标准功能
- `LoadMMDModel`：在视口内预览已解析模型
- `CreatePreviewActor`：在预览场景中生成蓝图实例

#### 7. FMMDPhysicsSimulator — MMD 物理模拟器
- 基于 Bullet Physics（ThirdParty 内含源码）实现
- 坐标系转换：PMX → Bullet → UE5（Z↑ / 右手系互转）
- 每帧流程：`PreSyncKinematicFromBones`（骨骼驱动 Kinematic 刚体）→ `StepSimulationMMD`（物理步进）→ `PostSyncBonesFromPhysics`（物理结果写回骨骼）

#### 8. FAGN_MMDSkeletalControl — 自定义 AnimNode
- 继承自 `FAnimNode_SkeletalControlBase`，在动画蓝图中以节点形式存在
- `Initialize_AnyThread`：创建并初始化 `FMMDPhysicsSimulator`，加载刚体/关节数据
- `EvaluateSkeletalControl_AnyThread`：每帧驱动物理模拟，将结果骨骼变换写入 `OutBoneTransforms`
- `FMMDAnimGraphHelper::AddMMDNodeToAnimBP`：自动将节点插入动画蓝图图表并连线

---

## 已实现功能

| 功能 | 状态 |
|------|------|
| PMX 文件完整解析（顶点/骨骼/材质/刚体/关节/变形） | ✅ 完成 |
| VMD 文件解析（骨骼帧/表情帧/镜头帧/光源帧/IK帧） | ✅ 完成 |
| 从 PMX 构建 USkeletalMesh（含多材质、UV） | ✅ 完成 |
| 纹理自动导入与材质实例创建 | ✅ 完成 |
| 骨骼（含 IK 骨骼）导入与权重 | ✅ 完成 |
| IK Rig 自动创建与重定向链配置 | ✅ 完成 |
| IK Retargeter 创建 | ✅ 完成 |
| 动画蓝图自动生成 | ✅ 完成 |
| 自定义 MMD AnimNode 插入动画蓝图 | ✅ 完成 |
| Bullet Physics 物理模拟（刚体 + 关节） | ✅ 完成（待验证） |
| 导入时自动生成 Blueprint Asset 并保存 | ✅ 完成 |
| 编辑器内嵌3D预览视口 | ✅ 完成 |
| VMD → UAnimSequence 动画资产转换 | ❌ 未实现 |
| 顶点变形（Morph Target / Blend Shape）导入 | ❌ 未实现 |
| VMD 表情帧 → 动画曲线 | ❌ 未实现 |
| 时间轴 UI | ❌ 占位符（待实现） |
| 视口模式切换（选中/移动/缩放按钮） | ❌ 未连接 |
| 静态网格导入 | ❌ 未实现 |
| 动画资产（UAnimSequence）保存 | ❌ 未实现 |
| 跨平台支持（非 Windows） | ❌ 仅支持 Windows |

---

## 下一步完善方向

### 优先级 1（核心功能缺失）

#### 1.1 VMD → UAnimSequence 动画资产转换
这是目前最重要的缺失功能。VMD 数据已经能正确解析，但没有转换为 UE5 的动画资产。

**实现思路：**
```
TVMDParser::ParseVMDFile(FilePath)  →  VMDData
    │
    ├── BoneFrames (骨骼关键帧)
    │       ↓
    │   为每根骨骼创建 FRawAnimSequenceTrack
    │   写入 PosKeys / RotKeys（需 VMD→UE5 坐标系转换）
    │
    └── MorphFrames (表情关键帧)
            ↓
        为每个 Morph 创建 FAnimCurveBase 曲线
        写入 FloatCurve Keys
    
最终调用 UAnimSequence::PostProcessSequence() 并保存资产
```

需要实现 `SaveMMDAnimationAsset(UAnimSequence*, FolderPath, AssetName)`（函数签名已存在，本版本已完成保存逻辑实现，但调用方的 VMD→AnimSequence 转换流程仍需开发）。

#### 1.2 PMX 顶点变形（Morph Target）导入
PMX 已解析全部变形数据（`PMXDatas::ModelMorphs`），但构建骨骼网格时未生成 `UMorphTarget`。

**实现思路：**
- 遍历 `PMXDatas::ModelMorphs`，筛选 `MorphType == 1`（顶点变形）
- 对每个变形，创建 `UMorphTarget`，填充 `FMorphTargetDelta`（顶点位置偏移）
- 调用 `USkeletalMesh::RegisterMorphTarget()` 注册
- 在 VMD 导入时，将 `VMDMorphKeyframe` 写入 `UAnimSequence` 的 Float Curve，曲线名与 MorphTarget 名一致

### 优先级 2（功能完整性）

#### 2.1 完善视口模式切换
"选中 / 移动 / 缩放"按钮目前只打印日志，需要连接到 `FMMDViewportClient` 的 Widget 模式：
```cpp
// 在 MMDViewPanel 中暴露接口，例如：
void MMDViewPanel::SetWidgetMode(UE::Widget::EWidgetMode Mode);

// 在按钮回调中调用：
ViewPanel->SetWidgetMode(UE::Widget::WM_Translate); // 移动
ViewPanel->SetWidgetMode(UE::Widget::WM_Scale);     // 缩放
```

#### 2.2 时间轴 UI
当前左侧面板只显示"时间轴"文字占位符，需要实现实际的时间轴控件：
- 显示 VMD 关键帧轨迹（骨骼帧 / 表情帧 / 镜头帧）
- 支持播放控制（播放/暂停/跳帧）
- 推荐使用 Slate 的 `STrack` / `SCurveEditor` 或自定义 `SPanel`

#### 2.3 静态网格导入
当前"导入静态网格"按钮仅在屏幕上打印消息，没有实际导入逻辑。可以调用 UE5 的 `FbxImporter` 或 `UAssetImportTask` 完成 FBX/OBJ 导入。

### 优先级 3（质量与兼容性）

#### 3.1 跨平台 SJIS 解码
`TVMDParser.cpp` 使用了 Windows 专属 API：
```cpp
#include <windows.h>
MultiByteToWideChar(932, 0, ...);
```
需替换为跨平台方案，例如使用 ICU（UE5 内置）：
```cpp
#include "Internationalization/Text.h"
// 或手动实现 Shift-JIS → UTF-16 查表转换
```

#### 3.2 SDEF 骨骼变形支持
PMX 支持 SDEF（Spherical Deformation）权重类型，当前代码将其等同于 BDEF2 处理，导致部分模型的衣服/头发变形不正确。正确做法是实现球面插值计算。

#### 3.3 PMD 格式支持
文件过滤器中包含了 `*.pmd`，但解析器只支持 PMX。可以添加 `TPMDParser` 或在 `TPMXParser` 中兼容 PMD 格式。

#### 3.4 导入进度对话框
导入大型 PMX 模型（尤其是顶点/纹理多的模型）时，UE 主线程会卡顿。建议使用 `FScopedSlowTask` 或异步任务（`AsyncTask`）配合进度条对话框。

### 优先级 4（体验优化）

- **错误恢复**：导入失败时清理已创建的不完整资产，避免留下垃圾包
- **重复导入检测**：检测 `/Game/MMDModels/{ModelName}` 下是否已有同名资产，提供覆盖/跳过选项
- **骨骼名称映射表**：IK Rig 硬编码了部分常见 MMD 骨骼名称，对非标准模型可能失效，建议提供可编辑的映射配置
- **物理参数暴露**：`FAGN_MMDSkeletalControl` 的 `UnitScale` / `MaxSubSteps` / `FixedTimeStep` 已暴露给蓝图，但默认值的合理性需根据实际效果调整
- **单元测试**：为解析器（`TPMXParser` / `TVMDParser`）添加单元测试，使用已知格式正确的小型 PMX/VMD 文件验证解析结果

---

## 已知 Bug

| Bug | 位置 | 状态 |
|-----|------|------|
| `SaveMMDAnimationAsset()` 函数体为空 | `MMDImportSetting.cpp:111` | ✅ 已修复（本版本） |
| `VMDLightKeyframe` 结构体定义错误 | `TVMDParser.h` | ✅ 已修复（本版本）—光源帧原按镜头帧格式（62字节）解析，现已修正为正确的28字节格式 |
| VMD 动画导入后未应用到骨架 | `MMDImportSetting.cpp:359` | ❌ `ViewPanel->LoadVMDAnimation(VMDInfo)` 被注释掉 |
| SJIS 解码依赖 `windows.h` | `TVMDParser.cpp:6` | ❌ 非 Windows 平台无法编译 |
| 视口模式切换按钮无实际效果 | `MMDImportSetting.cpp:145-168` | ❌ 选中/移动/缩放按钮只调用 `ShowImportProgress`，未连接视口 |
| 静态网格导入未实现 | `MMDImportSetting.cpp:283-322` | ❌ 只显示屏幕消息，没有实际导入逻辑 |

