# 面板本地化

记录面板文字的国际化和当前进度。最近更新：2026-11-19。

## 现状

- 面板里的 79 处硬编码中文已改成 `LOCTEXT`，命名空间 `Ue5MMDToolsUI`，源语言简体中文。
- 本地化目标配置在 [`Config/Localization/Ue5MMDTools.ini`](../Config/Localization/Ue5MMDTools.ini)，目标语言 `en`、`ja`。
- 英文与日文资源已生成并随插件分发：`Content/Localization/Ue5MMDTools/<Culture>/Ue5MMDTools.locres`，翻译源是对应的 `Ue5MMDTools.po`。
- 文件名里的 `{0}` 占位符在两种译文里都已保留。

编辑器语言是英文或日文时，面板走对应资源；中文环境显示中文原文。

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

## 生成记录（2026-11-19）

用最小宿主工程 + 源文件副本跑通，命令与产物：

```text
GatherTextFromSource: Submit = 89        （共 89 条文本，其中 84 条含中文）
生成：Ue5MMDTools.manifest / .archive / .po / .locres
英文 .po：84 条 msgid 全部填写 msgstr，0 条遗漏
日文 .po：84 条 msgid 全部填写 msgstr，0 条遗漏
```

首次尝试直接把插件放进宿主工程的 `Plugins/` 并启用，引擎会尝试加载插件模块（没有二进制）并中止；改成「把 `Source/` 复制到宿主工程的普通目录，配置里指向该目录」即可，收集阶段本来就不需要编译。

**坑：`CulturesToGenerate` 不能重复。** 同一个语言写两次时，`GenerateGatherArchive` 会在 `Localization/<Culture>/Ue5MMDTools.archive.tmp` 上保存失败并让整轮 `exit code 0` 但其实没更新 `.locres`。检查方法：看 `.locres` 的修改时间是否更新、以及日志里有没有 `Failed to save archive`。

## 验证

已做：英文 `.locres` 随包分发（`BuildPlugin` 产物里确认存在）；把发布包放进空白工程、`-culture=en` 无窗口启动，插件挂载两个模块、`MMD.OpenPanel` 被识别执行、`MMDMatEnsure` 正常完成，0 fatal error。

还要做：

1. 前台或离屏打开面板，逐页确认文字确实是英文（`.locres` 能被解析不等于面板已按英文渲染）。
2. 重点看带参数的两处：状态栏的 `动画：{0}` / `模型：{0}`，以及风格页的 `LOOKDEV / {0} · …`，确认占位符顺序在翻译后仍正确。
3. 窄窗口下检查英文是否溢出按钮（英文普遍比中文长 40% 左右）。
4. 面板截图目检通过之前，README 里不要写「英文界面已完成」。

### 自动截图的尝试（2026-11-19，未成功）

先写了 `-run=` 形式的 commandlet，在无窗口编辑器里构建面板并截图，结果 Slate 未初始化（commandlet 阶段没有 Slate）；改成控制台命令 `MMD.CapturePanel` + `-ExecCmds` 后，编辑器能正常启动、插件挂载、`MMD.OpenPanel` 路径可用，但截图命令在合理时间内没有跑到截图步骤，未取得面板图。

结论：面板的英文／日文渲染目前**仍需在前台或离屏渲染环境中人工目检**，上面 1–4 项检查继续有效。若要重试自动截图，建议走上层的编辑器控制台命令并给足加载等待，或直接用现有的 `-RenderOffscreen` 自动化测试框架（该框架里 Slate 是初始化好的）。

## 已知限制

- 自动化 UI 测试（`MMDPresetClickAuditTests` 等）按中文标签查找控件。文化切到英文后这些测试会找不到按钮，跑测试时需要固定中文文化，或把查找改成按控件类型 / 名字定位。
- 只有面板文字进了本地化；Commandlet 日志、构建工具输出仍是英文原文。
- `.locres` 是二进制，改动后在 diff 里只能看到二进制差异，评审时请附上生成命令和 PO 变更。
