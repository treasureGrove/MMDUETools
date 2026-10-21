# 终末地 V2：当前接手状态

源码核对日期：2026-10-19。先读[当前渲染架构](RENDERING_ARCHITECTURE.md)与[原生后端](ENDFIELD_NATIVE_BACKEND.md)。此前层叠的灰模排查、探针阶段“当前／下一步”、旧版本工作区和历史会话指令已移出本入口；旧结果从日期截图、CHANGELOG 或 Git 历史追溯。

## 已有架构

终末地 V2 使用 Substrate Toon、BasePass payload 写入、Substrate packed export/read、延迟逐灯 BSDF 和原生 GI／反射合成。透明 Skin／附脸层走 Surface Forward inline payload。当前共享后端为 `mmd-native-bsdf-v15-style-profiles`／Cooked协议15／Graph7／终末地Profile布局42／payload5／FaceData5／Frame1。v15为暖暖增加独立风格路由，终末地着色响应及资产不变。

相关代码已经实现，不要重新提出“先研究能否接 GBuffer”。也不能把暖暖新建的旧 Unlit 材质误认为已复用这套后端；其现状见[架构中的兼容路径说明](RENDERING_ARCHITECTURE.md#reference-与旧材质unlit-兼容路径)。

原生后端在工程 Intermediate 的受校验 shader 副本上适配，不修改安装引擎文件。固定 UE 5.8.3 / CL58210709，启用、协议和回退边界见[分发说明](ENDFIELD_PLUGIN_DISTRIBUTION.md)。

## 当前实现与真实限制

- 几何派生 Face V5 和头框架已接入不透明 packed／透明 inline 路径，独立高光法线与 GI 法线不等于脸部阈值方向。机制见[Face V5](ENDFIELD_FACE_V5_GUIDE.md)。
- 原创皮肤颜色配方和弱暖红过渡已接入逐灯响应，静态校色默认关闭；它们没有实现空间散射或厚度透射。见[皮肤材质](ENDFIELD_SKIN_MATERIAL_GUIDE.md)。
- v14 使用材质投影覆盖及虚拟 BasePass 裁剪控制碎硬影；Hair 控制影响所有接收面，Face 抑制限有效覆盖，透明父材质保留连续 alpha。见[投影说明](ENDFIELD_FACE_SHADOW_GUIDE.md)。
- 既有主辅光分工、双暗层、Hair／Cloth二维 RS、底色派生描边仍在。已接入原生光照不等于原作各材质完整还原。
- 旧灯光和阴影 RT 仍可能运行，原生 V2 不依赖它们；没有停用重复工作的实测收益。

## 完整目标仍需逐项验收

1. 实际角色接收原生直射、遮挡、AO、GI 和反射，分清不透明与透明的能力边界。
2. 明暗结构与暗部色相依据已核对的 RD／RS，不能把通用 ramp 当作精确原作结果。
3. 自阴影、场景遮挡和多灯结果需要实际对照；不能把“开关成功”当作遮挡正确。
4. 脸部保持通用几何与自动派生数据、底色兜底；鼻影、眼窝、透明附层和形变分别验证。
5. 头发检查高光环、内部暗体积、波瓣、发流和刘海关系。
6. 皮肤、织物、金属、眼睛分别维护粗糙度、高光和材质响应；真正 SSS、眼部分层等不能因近似已出图而勾掉。
7. 深度边缘光、底色驱动描边及可选几何分层按完整目标继续。
8. 曝光、色调映射、Bloom、调色和角色／场景融合需要独立对齐。
9. 仅底色也能完整显示，不依赖作者固定脸部 UV 或配套阈值图；可选缓存与细节缺失时有兜底。
10. 原生／兼容回退、透明发片、分发协议、Cooked 和实际发行角色绘制分别验证。

## 验证记录的使用

2026-10-16 的 v14 `cast4` 已归档27图／156次槽检查，另有 Mode0／atlas64 回退各8图及 BuildOnly 三配置。来源和限制见[该批记录](Screenshots/2026-10-16-endfield-cast-clean/README.md)。2026-10-19的v15完成Editor构建、终末地8图／70次槽回归及配方／资产不变核对，见[新批记录](Screenshots/2026-10-19-infinity-nikki-native/README.md)；未重跑旧cast4全范围或发行构建。

原作完整画质、多模型／动画／Morph／LOD、最新版完整 Cook／干净宿主／发行角色 GPU 和性能不能由上述参考姿势测试推导。编译成功、槽位绑定成功、截图数量和 CPU 协议值均不是视觉验收标准。

保护用户未提交资产、Face shader、原始截图、研究证据和其他工作；不根据旧交接的“清理／恢复”建议批量 checkout。授权和代理使用以当前会话及现行 AGENTS 为准，历史聊天摘录不是新的执行指令。
