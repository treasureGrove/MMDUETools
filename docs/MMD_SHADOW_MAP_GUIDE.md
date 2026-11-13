# 旧 Unlit 材质的阴影 RT 兼容路径

源码核对日期：2026-10-19。本文只说明依赖插件阴影 RT 的兼容材质，不是终末地 V2 的原生阴影方案。当前架构见[总览](RENDERING_ARCHITECTURE.md)。

此前的单级相机基、固定四 texel 布局、旧输入名称及默认值清单已经过时，现已移除，避免直接照抄接线。

当前兼容实现使用四级主方向光阴影，通过主视图的 CustomRenderPass 提交深度，并把级联与角色遮挡等元数据交给 LightDataRT。具体布局、默认值和门控以这些源码为准：

- [UMMDShadowMapSubsystem.cpp](../Source/Ue5MMDTools/Private/Rendering/UMMDShadowMapSubsystem.cpp)和[公开接口](../Source/Ue5MMDTools/Public/Rendering/UMMDShadowMapSubsystem.h)。
- [UMMDAnimeLightDataSubsystem.cpp](../Source/Ue5MMDTools/Private/Rendering/UMMDAnimeLightDataSubsystem.cpp)。
- [MMDToonLighting.ush](../Shaders/Core/MMDToonLighting.ush)中的实际阴影采样入口。

SceneCapture／离屏验证缺少主视图时，兼容路径可能拿不到有效自定义阴影基。必须读取报告中的有效状态并验证像素，不能仅凭阴影开关或地面存在投影就判角色正确接收。

终末地 V2 的直射阴影来自原生逐灯的 `FShadowTerms`，不依赖这张辅助深度 RT。旧 RT 无效不表示原生 VSM 失效；同样，原生 VSM 开启也不会自动补齐旧 Unlit 材质的阴影采样。
