# 终末地角色渲染入口

整理日期：2026-10-19。终末地 V2 已走原生 Substrate 材质缓冲和延迟逐灯后端。旧 Unlit 实施笔记、Version3 脸图布局及早期“待接入”说明已从本入口移除，避免被当成当前架构。

使用 `/Ue5MMDTools/Stages/Presets/EndfieldV2/DA_Render_EndfieldV2`。实际原生选择需要 Substrate、对应父材质和有效配置；自定义 BSDF 还需要后端成功启用。旧 `DA_Render_Endfield`、Reference Endfield 与 V2 不能仅凭名称互相替代。

从下列说明继续：

- [当前渲染架构](RENDERING_ARCHITECTURE.md)：原生、透明 Forward 与旧 Unlit 的边界。
- [后端与源码入口](ENDFIELD_NATIVE_BACKEND.md)：v14、载荷、逐灯光照、GI 和启用条件。
- [使用与维护](ENDFIELD_NATIVE_RENDERING_GUIDE.md)。
- [Face V5](ENDFIELD_FACE_V5_GUIDE.md)、[皮肤材质](ENDFIELD_SKIN_MATERIAL_GUIDE.md)、[角色投影](ENDFIELD_FACE_SHADOW_GUIDE.md)。
- [接手目标与剩余验收](ENDFIELD_V2_FOR_GPT.md)、[分发](ENDFIELD_PLUGIN_DISTRIBUTION.md)。

历次截图、配置与失败记录保留在 `Screenshots/<日期>/` 和 [CHANGELOG](CHANGELOG.md)。它们只证明对应批次的结果，不是本日重测或完整画质验收。
