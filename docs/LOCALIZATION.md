# 面板本地化

记录面板文字的国际化和当前进度。最近更新：2026-11-19。

## 现状

- 面板里的 79 处硬编码中文已改成 `LOCTEXT`，命名空间 `Ue5MMDToolsUI`，源语言简体中文。
- 本地化目标配置在 [`Config/Localization/Ue5MMDTools.ini`](../Config/Localization/Ue5MMDTools.ini)，目标语言 `en`、`ja`。
- **英文 / 日文资源尚未生成**，所以面板目前仍显示中文。README 与落地页写的 "UI planned" 仍然成立，改这一行之前请先把 `.locres` 生成并验证。

## 涉及文件

| 文件 | 条数 |
| --- | --- |
| `MMDImportSetting.cpp` | 29 |
| `MMDImportSettingMotion.cpp` | 19 |
| `MMDImportSettingPresets.cpp` | 14 |
| `MMDImportSettingProduction.cpp` | 6 |
| `MMDViewPanelMotion.cpp` | 8 |
| `MMDViewPanel.cpp` | 9 |

命令行的 `Scripts/ue5mmd.ps1` 与各 Commandlet 的输出保持英文／原样，不参与这次面板本地化。

## 生成翻译资源

不需要打开前台编辑器，用无窗口的 Commandlet：

```powershell
& '<引擎>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  '<工程>\<工程>.uproject' `
  -run=GatherText `
  -config="Plugins/Ue5MMDTools/Config/Localization/Ue5MMDTools.ini" `
  -unattended -nosplash
```

第一次运行会在 `Content/Localization/Ue5MMDTools/` 下产出清单、归档和 `Ue5MMDTools.po`。把 PO 里的 `msgstr` 填好，再跑一次同一条命令，就会导入翻译并生成：

```text
Content/Localization/Ue5MMDTools/en/Ue5MMDTools.locres
Content/Localization/Ue5MMDTools/ja/Ue5MMDTools.locres
```

`.locres` 随插件包分发；运行时会按当前文化加载，找不到就回退到中文原文。

开发宿主里插件的相对路径是 `MMD/Plugins/Ue5MMDTools/`，与发布树不同，运行前改一下配置里的 `SearchDirectoryPaths`、`SourcePath`、`DestinationPath`。

## 分发说明

- `BuildPlugin` 只把 `Config/FilterPlugin.ini` 打进包，`Config/Localization/Ue5MMDTools.ini` **不随插件包分发**。这是工具链配置，留在开发仓库给维护者用即可。
- 真正要随包走的是 `Content/Localization/Ue5MMDTools/<Culture>/*.locres`，它在 `Content/` 下，会正常被打包。

## 验证

1. 生成 `.locres` 后，用 `-culture=en` 启动编辑器（或把编辑器语言切到英文），打开 **Window → Ue5MMDTools**，逐页检查按钮、标题、状态行。
2. 重点看带参数的两处：状态栏的 `动画：{0}` / `模型：{0}`，以及风格页的 `LOOKDEV / {0} · …`，确认占位符顺序在翻译后仍正确。
3. 窄窗口下检查英文是否溢出按钮（英文普遍比中文长 40% 左右）。

## 已知限制

- 自动化 UI 测试（`MMDPresetClickAuditTests` 等）按中文标签查找控件。文化切到英文后这些测试会找不到按钮，跑测试时需要固定中文文化，或把查找改成按控件类型 / 名字定位。
- 只有面板文字进了本地化；Commandlet 日志、构建工具输出仍是英文原文。
- `.locres` 是二进制，改动后在 diff 里只能看到二进制差异，评审时请附上生成命令和 PO 变更。
