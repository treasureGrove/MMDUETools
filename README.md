# Ue5MMDTools

Ue5MMDTools 是一个用于 Unreal Engine 5 的 MMD 导入和播放插件。插件可以把 PMX 模型、VMD 动作和表情数据转换到 UE 的原生资产流程中，方便在 UE 场景里预览、播放和继续制作。

完整展示页和演示视频：

https://treasureGrove.github.io/MMDUETools/

B 站演示视频：

https://www.bilibili.com/video/BV1M2RXBPExk/

当前基础流程：

```text
PMX 模型 -> UE SkeletalMesh / Skeleton / MorphTarget / Actor
VMD 动作 -> UE AnimSequence 骨骼动画 + 表情曲线
AnimSequence 播放 -> 骨骼动作 + 眨眼 + 嘴型 + 表情
```

## 使用教程

### 1. 安装插件

下载后，把插件目录放到 UE 项目的 `Plugins` 文件夹下。

```text
YourProject/
  Plugins/
    Ue5MMDTools/
```

然后打开 UE5，在插件面板中启用该插件。

<img width="2556" height="507" alt="Enable plugin" src="https://github.com/user-attachments/assets/34db6af7-8de2-455e-8c66-3fe83762a915" />

### 2. 打开插件面板

启用插件后，在 UE5 插件面板中打开 Ue5MMDTools 的导入界面。

<img width="798" height="792" alt="Ue5MMDTools panel" src="https://github.com/user-attachments/assets/1c5c4d43-480d-4a4e-91a9-e905e94f4fc4" />

### 3. 导入 PMX 模型

点击“导入模型”，选择需要导入的 `.pmx` 文件。

<img width="1626" height="648" alt="Import PMX model" src="https://github.com/user-attachments/assets/078f6b85-bc5c-4959-ae0c-669c37bb0079" />

导入完成后，插件会生成 MMD 角色相关资产，并在面板和蓝图中可以看到对应的 Actor。

<img width="1914" height="1026" alt="Imported MMD actor" src="https://github.com/user-attachments/assets/9ab3b650-4054-4ed4-8882-56c2c63d848c" />

### 4. 查看生成的资产

导入 PMX 后，会生成对应的角色 Actor、骨骼资产和 IK 重定向相关资产。

<img width="1611" height="621" alt="Generated actor and retarget assets" src="https://github.com/user-attachments/assets/a37aea3c-5c72-418b-9920-711de3a3d9f8" />

通常会包含：

- `SkeletalMesh` 和 `Skeleton`
- 材质和材质实例
- `MorphTarget`
- MMD Actor 和 `AnimBlueprint`
- IK Rig / IK Retargeter
- MMD ModelData 辅助资产

### 5. 导入 VMD 动作

选中已经导入的 MMD 角色或对应的 SkeletalMesh，然后点击“导入 VMD 动画”，选择 `.vmd` 文件。

导入成功后会生成 UE `AnimSequence`，其中包含：

- 骨骼动画轨道
- 表情 Morph 曲线

如果当前场景里有对应角色，插件会尝试直接播放该动画。

### 6. 播放 MMD 动画

导入 VMD 后，角色可以在 UE 中播放 MMD 动作。正常情况下应能看到：

- 身体骨骼动作
- root / center 位移
- 眨眼
- 嘴型
- 眉毛和表情变化

## 插件功能说明

### PMX 模型导入

- 解析 PMX 顶点、索引、法线、UV、材质和骨骼。
- 生成 UE `SkeletalMesh` 和 `Skeleton`。
- 创建基础 MMD 材质和材质实例。
- 保存 MMD ModelData 辅助数据。

### MorphTarget / 表情

- PMX vertex morph 导入为 UE `MorphTarget`。
- 修正 PMX 顶点到 UE 渲染顶点的映射，避免表情扭曲。
- VMD 表情写入 `AnimSequence` float curves。
- 支持 Group / Flip Morph 展开，嘴型和眨眼可以正常播放。

### VMD 动作导入

- 解析 VMD 骨骼关键帧和 Morph 关键帧。
- 骨骼轨道写入 `AnimSequence`。
- 表情轨道写入 Morph curves。
- 支持 MMD 到 UE 坐标系转换和 IK / 追加骨骼烘焙。

### Actor / AnimBlueprint / IK

- 自动创建 MMD Actor。
- 自动生成 `AnimBlueprint`。
- 提供 MMD Skeletal Control 动画节点。
- 生成 IK Rig 和 IK Retargeter。

### 物理模拟

- 读取 PMX 刚体和 Joint 数据。
- 基于 Bullet 初始化 MMD 风格物理模拟器。
- 物理功能仍在实验阶段，适合继续调试和扩展。

## 当前限制

- Material Morph 尚未完整驱动 UE 材质参数。
- Bone Morph / UV Morph 还没有作为完整动画通道实现。
- 不同模型的骨骼命名、IK 结构和特殊 Morph 组合可能需要继续适配。
