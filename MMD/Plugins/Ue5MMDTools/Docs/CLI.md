# 命令行导入（CLI）

不打开编辑器也能导入 PMX／VMD，并拿到 JSON 结果，便于批量处理和脚本编排。最近核对：2026-11-18。

## 入口

```powershell
<插件>/Scripts/ue5mmd.ps1 <子命令> [参数] -Project <你的工程.uproject>
```

脚本会调用 `UnrealEditor-Cmd.exe`，以无窗口方式运行对应 Commandlet，并转发参数。公共参数：

| 参数 | 说明 |
| --- | --- |
| `-Project` | 目标工程 `.uproject`；省略时在当前目录向上查找 |
| `-Engine` | 引擎目录；省略时按工程 `EngineAssociation` 从注册表读取 |
| `-Json` | 打印 Commandlet 写出的 JSON 报告 |
| `-Quiet` | 不打印命令行回显 |
| `-Report` | 报告输出路径，默认写到工程 `Saved/` |

## import-pmx

```powershell
.\ue5mmd.ps1 import-pmx -Pmx D:\models\yyb.pmx -Name YYB -Json
```

- 资产固定生成在 `/Game/MMDModels/<PMX 文件名规范化>/`，模型名为非字母数字转下划线。
- 网格位于 `Model/<名字>`，骨架在 `Skeleton/`，材质与贴图在同级目录。
- 目标已存在时返回失败并提示；加 `-Force` 才覆盖。
- `-StandardMaterials` 使用标准材质链导入。

输出示例（节选）：

```json
{
  "schema": "mmd.cli.import-pmx.v1",
  "mesh": "/Game/MMDModels/YYB/Model/YYB.YYB",
  "vertices": 351932,
  "triangles": 634970,
  "materials": 38,
  "bones": 498,
  "morph_targets": 71,
  "asset_count": 14,
  "success": true
}
```

## import-vmd

```powershell
.\ue5mmd.ps1 import-vmd -Vmd D:\motions\wait.vmd -Mesh /Game/MMDModels/YYB/Model/YYB -Pmx D:\models\yyb.pmx -Name Wait -Json
```

- `-Mesh` 必填，必须是已导入的骨骼网格。
- `-Pmx` 可选，用于补充骨骼映射上下文。
- 动画写入该模型目录下的 `Animation/`，同名已存在时需要 `-Force`。
- 未匹配的骨骼或表情轨道会作为 `warnings` 返回，不影响成功状态。

## render / validate

```powershell
.\ue5mmd.ps1 render -ModelAsset /Game/MMDModels/YYB/Model/YYB -Motion D:\motions\wait.vmd -Output D:\out\movie.mp4
.\ue5mmd.ps1 validate
```

`render` 转发到现有 `MMDCreateMovie`，参数与后台制作一致（`-Preset`、`-Stage`、`-Width`、`-Fps` 等）。`validate` 运行导入与物理自动化测试并写出报告。

## 退出码与报告

- 退出码 `0`：成功；非 `0`：失败，日志尾部会打印在控制台。
- 每个子命令都会写 JSON，包含 `success`、`errors`、`warnings` 和 `elapsed_seconds`。
- 日志默认在 `Saved/Logs/ue5mmd-<子命令>.log`。

## 已知限制

- 资产目录由导入器固定推导，不能改到自定义路径。
- PMX 文件名含撇号等字符时，材质包路径可能被判定为非法包名；建议先重命名为字母数字下划线组合。
- 报告里的 `asset_count` 只统计属于本次命名的文件，同目录此前导入的资产不计入。
