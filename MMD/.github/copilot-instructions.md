# MMD UE5 Tools - AI编程助手指导文档

## 🎯 项目概述

这是一个专业的Unreal Engine 5插件，用于导入和处理MMD（MikuMikuDance）模型、动画和物理系统。采用**混合架构设计**：
- **渲染系统**: UE5现代渲染管线（PBR、光线追踪、Nanite、Lumen）
- **物理系统**: 保留MMD原生物理特性（头发、衣物摆动）
- **动画系统**: UE5动画蓝图和Sequencer

## 🏗️ 核心架构理解

### 主要组件层次
```
AMMDActor (主要使用接口)
├── USkeletalMeshComponent (UE5原生渲染)
├── UMMDPhysicsComponent (MMD物理系统)
├── UAnimationBlueprint (UE5动画系统)
└── UMMDMaterialInstance (MMD材质适配)
```

### 数据流转换管线
```
PMX文件 → TPMXParser → PMXDatas → TMMDMeshBuilder → USkeletalMesh
VMD文件 → FVMDParser → FVMDAnimationData → UAnimSequence
物理数据 → UMMDPhysicsComponent → UE5 Physics World
```

## 🔧 关键技术模式

### 坐标系转换（MMD→UE5）
```cpp
// 右手坐标系 → 左手坐标系，注意Y/Z轴交换
const float PMXConvertScale = 100.0f; // MMD使用厘米，UE5使用厘米*100
Position = FVector3f(PMX.X * Scale, -PMX.Z * Scale, PMX.Y * Scale);
Normal = FVector4f(PMX.X, -PMX.Z, PMX.Y, 1.0f);
UV = FVector2f(PMX.U, 1.0f - PMX.V); // V坐标翻转
```

### 骨骼权重处理模式
MMD支持多种权重变形类型，必须正确处理：
```cpp
switch (Weight.WeightDeformType) {
    case 0: // BDEF1 - 单骨骼，权重255
    case 1: // BDEF2 - 双骨骼线性混合
    case 2: // BDEF4 - 四骨骼，总权重必须为255
    case 3: // SDEF - 球形混合（特殊处理）
}
```

### 错误处理和进度报告
```cpp
// 用户友好的进度显示
MMDImportSetting::ShowGlobalImportProgress(TEXT("正在解析PMX文件..."), EMMDMessageType::Info);

// 开发调试日志
UE_LOG(LogTemp, Warning, TEXT("顶点数: %d, 骨骼数: %d"), VertexCount, BoneCount);

// 数据验证模式
if (PMXInfo.ModelVertices.Num() == 0) {
    MMDImportSetting::ShowGlobalImportProgress(TEXT("错误: 没有顶点数据"), EMMDMessageType::Error);
    return nullptr;
}
```

## 📁 项目结构规范

### 核心模块
- **`TPMXParser`**: PMX文件格式解析器（1200+行复杂实现）
- **`TMMDMeshBuilder`**: UE5骨骼网格构建器
- **`MMDViewPanel`**: 3D预览窗口（560+行UI实现）
- **`MMDImportSetting`**: 导入设置和进度管理
- **`MMDPhysicsSystem`**: MMD物理系统（待实现）

### 文件命名约定
- 类名前缀: `TMMD` (TPMXParser), `MMD` (MMDImportSetting)
- 资源后缀: `_Skeleton`, `_Material`, `_Animation`
- 临时对象: 使用`GetTransientPackage()`

## 🚀 开发工作流程

### 构建系统依赖
```csharp
// Ue5MMDTools.Build.cs - 关键模块依赖
PublicDependencyModuleNames: "Core", "CoreUObject", "Engine", "Slate", "SlateCore"
PrivateDependencyModuleNames: "UnrealEd", "AdvancedPreviewScene", "AssetTools"
```

### 任务配置
项目包含预配置的UBT构建任务：
- `MMDEditor Win64 Development Build` (默认构建)
- `MMD Win64 Development Build` (运行时模块)
- `Generate Project Files` (重新生成项目文件)

### 开发环境
- UE5.5引擎路径: `E:\ae\UE_5.5\`
- 项目路径: `Z:\Project\UEProject\MMDUETools\MMD\`
- 使用VS Code + UE5集成开发

## 💡 特殊实现考虑

### PMX文件格式复杂性
PMX是二进制格式，包含：
- 可变长度字符串（UTF-8/UTF-16编码）
- 动态索引大小（1/2/4字节）
- 复杂的顶点权重类型
- 物理约束和软体数据

### 内存管理策略
```cpp
// 大文件处理
TArray<uint8> PMXData; // 整个文件加载到内存
PMXData.Reserve(ExpectedSize); // 预分配避免重复分配

// UE5对象创建
USkeletalMesh* Mesh = NewObject<USkeletalMesh>(GetTransientPackage(), *AssetName);
```

### UI组件通信模式
```cpp
// Slate组件间通信
class MMDImportSetting : public SCompoundWidget {
    TSharedPtr<MMDViewPanel> ViewPanel; // 弱引用视口
    static TWeakPtr<MMDImportSetting> CurrentInstance; // 全局实例管理
};
```

## 🎨 视觉和材质特性

### MMD材质特点
- 卡通着色渲染
- 边缘线描绘
- 球形环境贴图
- 多重纹理混合
- Alpha混合模式

### UE5适配策略
保持MMD视觉特色同时利用UE5现代渲染特性，通过`UMMDMaterialInstance`实现适配层。

## 🔬 调试和测试

### 日志级别
- `LogTemp` 用于开发调试
- `MMDImportSetting::ShowGlobalImportProgress` 用于用户反馈
- 区分中文用户消息和英文调试信息

### 测试数据验证
每个导入步骤都包含数据完整性检查，确保MMD文件格式兼容性。

## ⚡ 性能优化指南

### 大数据处理
MMD模型通常包含：
- 数万个顶点
- 复杂骨骼层级
- 大量物理约束
- 高分辨率纹理

优化策略：
- 流式加载大文件
- 批量处理顶点数据
- 异步资源创建
- 内存池管理

---

**开发提示**: 这是一个深度技术项目，涉及3D图形、文件格式解析、物理模拟等多个领域。始终考虑MMD社区的兼容性需求和UE5的现代化优势。