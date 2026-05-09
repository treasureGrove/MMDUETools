# Ue5MMDTools

Ue5MMDTools 是一个用于 Unreal Engine 5 的 MMD 导入和播放插件，用于把 PMX 模型、VMD 动作和表情数据转换到 UE 的原生资产流程中。

完整介绍、使用教程和 B 站内嵌演示视频请查看 GitHub Pages：

https://treasureGrove.github.io/MMDUETools/

当前基础流程：

```text
PMX 模型 -> UE SkeletalMesh / Skeleton / MorphTarget / Actor
VMD 动作 -> UE AnimSequence 骨骼动画 + 表情曲线
AnimSequence 播放 -> 骨骼动作 + 眨眼 + 嘴型 + 表情
```
