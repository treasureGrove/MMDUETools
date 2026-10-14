# 终末地角色渲染预设 — 实际引擎截图

- 捕获日期：2026-10-13（本机 UE 5.8，`UnrealEditor-Cmd.exe -run=MMDStagePresets -EndfieldCapture -AllowCommandletRendering -RenderOffscreen -d3d12`）
- 来源：`MMDStagePresetsCommandlet.cpp` 新增的 `-EndfieldCapture` 只读截图路径，绑定新入库预设
  `/Ue5MMDTools/Stages/Presets/Endfield/DA_Render_Endfield` 与父材质
  `/Ue5MMDTools/Stages/Materials/Endfield/M_MMD_Endfield_{Opaque|Masked|Translucent}`。
- 角色夹具：`Saved/BackgroundValidation/ImportedAssets/` 中最新的 `vertices > 5000` 导入样本（Miku 型），VMD 8 秒预览动画。
- 命令报告：`MMD/Saved/EndfieldValidation/endfield-capture-report.json`；引擎日志：`endfield-capture.log`。
- 该组 12 张全部 `passed: true`，`masked_hair_depth_passed`、`environment_response_passed` 均为真；
  真实绑定核对到 `Hair01 → M_MMD_Endfield_Masked`、`Hair02 → M_MMD_Endfield_Opaque`。
- 这批是**引擎实拍**，不是目标效果图。

该 Unlit 方案随后被用户否决，现仅保留历史证据。本次发布只纳入 `endfield_hero.png` 和 `endfield_face.png` 两张代表图；下表是原始 12 图的输出清单，其余实拍留在本地原验证目录，不随本批上传。

## 图

| 文件 | 内容 | 大小 |
|---|---|---|
| `endfield_hero.png` | 全身（hero_full_body，庭院机位） | 1.40 MB |
| `endfield_face.png` | 脸部特写（正视，animation t=2.6s） | 1.05 MB |
| `endfield_face_turn.png` | 脸部特写（抬头，animation t=7.0s） | 1.05 MB |
| `endfield.png` | 舞台机位全身（t=0） | 1.40 MB |
| `endfield_time2.png` | 同一机位第二时间点（t=1.6s） | 1.39 MB |
| `endfield_detail.png` | 中景（当前机位被前景樱枝遮挡，参考价值有限） | 1.18 MB |
| `endfield_cloth.png` / `endfield_cloth_details_off.png` | 衣料细法线开/关 | 1.10 MB |
| `endfield_face_details_off.png` | 脸部细节参数关闭对照 | 1.05 MB |
| `endfield_geometry_fallback.png` | 头部几何兜底（清空头骨参数） | 1.05 MB |
| `endfield_environment_only.png` / `endfield_lights_off.png` | 仅环境光 / 全部直射灯关（脸部均值 22.99 vs 0.05） | 0.99 / 0.68 MB |

## 未归档

`Saved/EndfieldValidation/regress-seven-shots/`（旧七套预设回归，57/57 通过）体积较大，按项目约定不纳入仓库。
