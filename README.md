# Ue5MMDTools

Ue5MMDTools 是一个用于在 Unreal Engine 5 中导入、预览和播放 MMD 资源的插件。当前主要目标是把 PMX 模型和 VMD 动作导入到 UE 的原生资产体系中，让 MMD 角色可以在 UE 场景里作为 `SkeletalMesh`、`AnimSequence`、`AnimBlueprint` 和 Actor 使用。

> 当前项目仍在开发中，建议先用于编辑器预览、流程验证和二次开发。

## 演示

### 视频演示

> 在这里放一个完整播放的 MMD 动作视频。建议录制 20-40 秒，画面里包含全身骨骼动作、面部表情、嘴型和眨眼，最好能看到 UE 编辑器视口或运行窗口。

```md
<!-- TODO: 替换为你的视频链接，例如 B站 / YouTube / GitHub Release asset -->
[演示视频：PMX + VMD 在 UE5 中正常播放](TODO_VIDEO_URL)
```

### 效果截图

> 在这里放最终播放效果截图。建议选一张能看到角色全身姿态和面部表情的视口截图。

```md
<!-- TODO: 替换为最终效果截图 -->
![MMD playback in Unreal Engine](TODO_FINAL_PLAYBACK_IMAGE)
```

## 已支持功能

### PMX 模型导入

- 解析 PMX 模型基础数据，包括顶点、索引、材质、骨骼、Morph、刚体和 Joint 等信息。
- 生成 UE `SkeletalMesh`。
- 生成 UE `Skeleton` 并建立 PMX 骨骼层级。
- 导入 PMX 材质槽，并创建基础 MMD 材质/材质实例。
- 支持顶点权重导入，包括 BDEF/SDEF/QDEF 等 PMX 权重数据的基础转换。
- 支持 PMX 顶点 Morph 导入为 UE `MorphTarget`。
- 修正 PMX 原始顶点到 UE 拆分后渲染顶点的 MorphTarget 映射，避免表情扭曲。

### VMD 动作导入

- 解析 VMD 文件。
- 支持骨骼关键帧导入。
- 生成 UE `AnimSequence`。
- 将 VMD 骨骼动作写入 `AnimSequence` 的 bone tracks。
- 支持 MMD 到 UE 坐标系、旋转和位置缩放转换。
- 支持将 VMD IK/追加骨骼效果烘焙到 FK 动画轨道中。

### 表情动画

- 解析 VMD Morph 关键帧。
- 将 VMD 表情轨道写入 `AnimSequence` float curves。
- UE 播放 `AnimSequence` 时自动驱动同名 `MorphTarget`。
- 支持 PMX vertex morph。
- 支持 VMD 表情名匹配 PMX/UE MorphTarget 名称。
- 支持 PMX Group Morph / Flip Morph 展开到最终 vertex MorphTarget，因此嘴型、眨眼、眉毛等组合表情可以正确播放。

### Actor 和编辑器工作流

- 提供 MMD 导入面板。
- 导入 PMX 后自动生成可放入场景的 MMD Actor。
- Actor 内包含 `SkeletalMeshComponent`，可直接在场景中预览模型。
- 支持选择已导入的角色后导入 VMD 动作，并在当前角色上播放。

### IK / Retarget 辅助资产

- 根据 PMX 骨骼生成 IK Rig。
- 生成 IK Retargeter 辅助资产。
- 自动尝试映射骨骼链。
- 对 Root 链设置适合重定向的基础参数。

### 动画蓝图和物理节点

- 自动生成 MMD 角色用 `AnimBlueprint`。
- 提供 `MMD Skeletal Control` 动画节点。
- 节点内保存 PMX 刚体和 Joint 数据。
- 基于 Bullet 初始化 MMD 风格物理模拟器。
- 当前物理功能仍属于实验性能力，适合继续调试和扩展。

## 快速使用

### 1. 安装插件

把插件目录放到 UE 项目的 `Plugins` 目录下：

```text
YourProject/
  Plugins/
    Ue5MMDTools/
```

然后打开 UE5，在插件面板中启用 `Ue5MMDTools`。

> 截图建议：插件面板中启用 Ue5MMDTools 的画面。

```md
<!-- TODO: 插件启用截图 -->
![Enable Ue5MMDTools plugin](TODO_ENABLE_PLUGIN_IMAGE)
```

### 2. 打开导入面板

在 UE 编辑器中打开 Ue5MMDTools 的插件面板。

> 截图建议：显示插件面板和导入按钮。

```md
<!-- TODO: 插件面板截图 -->
![Ue5MMDTools import panel](TODO_IMPORT_PANEL_IMAGE)
```

### 3. 导入 PMX 模型

点击导入模型按钮，选择 `.pmx` 文件。导入完成后，插件会生成：

- `SkeletalMesh`
- `Skeleton`
- 材质/材质实例
- MorphTarget
- MMD Actor
- AnimBlueprint
- IK Rig / IK Retargeter
- MMD ModelData 辅助资产

> 截图建议：Content Browser 中生成的模型资产、Skeleton、AnimBP、IKRig 等。

```md
<!-- TODO: PMX 导入后资产截图 -->
![Imported PMX assets](TODO_IMPORTED_ASSETS_IMAGE)
```

### 4. 导入 VMD 动作

选中已导入的 MMD 角色或对应 SkeletalMesh，然后点击导入 VMD 动画，选择 `.vmd` 文件。

导入后会生成 `AnimSequence`，其中包含：

- 骨骼动画轨道
- 表情 Morph float curves

如果当前场景里有对应角色，插件会尝试直接播放该动画。

> 截图建议：AnimSequence 曲线面板中能看到表情曲线，例如 `あ`、`まばたき`、`笑い` 等。

```md
<!-- TODO: AnimSequence 曲线截图 -->
![AnimSequence morph curves](TODO_ANIM_CURVES_IMAGE)
```

### 5. 播放效果

导入完成后，可以在 UE 视口中播放角色动画。正常情况下应能看到：

- 身体骨骼动作
- 头发/衣服等骨骼跟随
- 眨眼
- 嘴型
- 眉毛/表情变化

> 视频建议：你现在已经有一个正常播放的 MMD 视频，可以贴在最上方“视频演示”位置。最好保留几秒近景，展示嘴型和眼睛表情确实在动。

## 当前支持范围

当前重点支持：

- PMX 2.x 模型解析
- PMX SkeletalMesh 导入
- PMX vertex morph 导入
- PMX group/flip morph 在 VMD 表情导入时展开
- VMD 骨骼动作导入
- VMD 表情动画导入
- UE `AnimSequence` 播放
- 编辑器内快速预览

部分能力仍在完善：

- Material Morph 尚未完整驱动 UE 材质参数。
- Bone Morph / UV Morph 尚未作为完整运行时通道实现。
- 物理模拟仍处于实验性阶段。
- 不同模型的骨骼命名、IK 结构和特殊 morph 组合可能需要继续适配。

## 推荐补充的媒体素材

为了让 README 更直观，建议补以下素材：

1. **完整播放视频**
   - 内容：角色在 UE 中播放 VMD。
   - 必须包含：全身动作、眨眼、嘴型、表情。
   - 放置位置：README 顶部“视频演示”。

2. **插件启用截图**
   - 内容：UE 插件面板中启用 Ue5MMDTools。
   - 放置位置：“安装插件”。

3. **导入面板截图**
   - 内容：Ue5MMDTools 面板和导入按钮。
   - 放置位置：“打开导入面板”。

4. **PMX 导入结果截图**
   - 内容：Content Browser 中生成的 `SkeletalMesh`、`Skeleton`、`AnimBlueprint`、`IKRig`、`IKRetargeter`。
   - 放置位置：“导入 PMX 模型”。

5. **AnimSequence 曲线截图**
   - 内容：动画资产里能看到 Morph curves。
   - 推荐展示：`あ`、`い`、`う`、`まばたき`、`笑い` 等曲线。
   - 放置位置：“导入 VMD 动作”。

6. **最终播放截图**
   - 内容：UE 视口中角色带表情播放的画面。
   - 放置位置：“效果截图”。

## 开发状态

这个插件目前已经可以完成一条基础 MMD 工作流：

```text
PMX 模型
  -> UE SkeletalMesh / Skeleton / MorphTarget

VMD 动作
  -> UE AnimSequence bone tracks + morph curves

AnimSequence 播放
  -> 骨骼动作 + 表情动画
```

后续可以继续完善材质 morph、UV morph、物理稳定性、更多模型兼容性和打包运行时支持。
