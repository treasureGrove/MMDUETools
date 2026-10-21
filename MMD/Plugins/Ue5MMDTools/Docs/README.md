# 插件文档入口

整理日期：2026-10-19。

渲染先读[当前架构](RENDERING_ARCHITECTURE.md)。终末地 V2 已使用 Substrate packed 材质缓冲与原生逐灯后端；Reference／旧材质的 Unlit 兼容路径单独存在。不要把其中一条路径泛化为全插件现状。

## 当前维护说明

- [原生后端与源码入口](ENDFIELD_NATIVE_BACKEND.md)
- [终末地使用与维护](ENDFIELD_NATIVE_RENDERING_GUIDE.md)
- [接手状态与未完成目标](ENDFIELD_V2_FOR_GPT.md)
- [脸部几何数据](ENDFIELD_FACE_V5_GUIDE.md)、[皮肤材质](ENDFIELD_SKIN_MATERIAL_GUIDE.md)、[脸部投影控制](ENDFIELD_FACE_SHADOW_GUIDE.md)
- [固定版本插件分发](ENDFIELD_PLUGIN_DISTRIBUTION.md)
- [暖暖原生渲染与独立 Profile](INFINITY_NIKKI_NATIVE_RENDERING.md)、[最初失败实验与配置证据](Screenshots/2026-10-19-infinity-nikki/README.md)
- [修改记录](CHANGELOG.md)

## 范围受限的说明与历史材料

[舞台 Reference 预设](STAGE_RENDER_PRESETS.md)仅说明旧 Unlit 材质和舞台功能；[旧阴影 RT](MMD_SHADOW_MAP_GUIDE.md)仅服务依赖它的兼容材质。

`PHASE_TWO_*`、[旧路线图](MMDTOOLS_ROADMAP.md)、[原生接入前的交接入口](ENDFIELD_V2_HANDOFF.md)、[公开复刻参考审计](ENDFIELD_REPRODUCTION_REFERENCE_AUDIT.md)及 `Screenshots/<日期>/` 为相应批次的记录。日期截图、研究依据和失败记录保留，历史指令不延续为当前执行约束。

导入与动画文档按各自主题查阅：[VMD/PMX 接入](MMD_VMD_Integration_Guide.md)、[动画烘焙](VMD_PMX_Animation_Bake.md)、[UE 5.8 迁移记录](UE58_MIGRATION_STATUS.md)。
