# 构建环境与打包（UE 5.8.3 / Win64）

记录本机从源码构建 Ue5MMDTools Win64 插件包的实际步骤、遇到的环境问题及处理方式。最近核对：2026-11-18。

适用范围：**UE 5.8.3 / CL58210709 / Win64**，Windows 10/11。不修改安装引擎文件，所有适配都在工程目录内完成。

## 快速开始

```powershell
# 构建、清理、打包并输出 SHA-256
& .\MMD\Scripts\BuildReleasePackage.ps1 -OutputRoot D:\PluginReleases\v1.1.2
```

脚本会做四件事：准备工程本地 AutoSDK、构建 BuildPlugin、剔除不该发布的文件、生成 ZIP 与校验值。下面两节是它自动处理的环境问题，手工构建时需自己做。

## 问题一：UBT 找不到已安装的 MSVC

现象：

```text
No valid Visual C++ toolchain was found (minimum version 14.38.33130, preferred version 14.50.35717).
No available x64 toolchains found
Result: Failed (OtherCompilationError)
```

即使 `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.50.35717` 存在且 `vcvars64.bat` 能正常运行，UBT 仍然报这个错。原因是 UBT 通过 SetupConfiguration / SxS 注册表枚举 Visual Studio 安装实例，本机的 VS 18 没有被它识别为可用实例。显式传 `-CompilerVersion=14.50.35717` 也不能绕过，因为问题在安装枚举，不在版本选择。

处理方式：让 UBT 走 AutoSDK 路径读取同一份 MSVC。UE 会在 `UE_SDKS_ROOT\HostWin64\Win64\VS2026\<版本>` 下查找工具链。

```powershell
$autoSdk = 'D:\YourProject\Saved\BuildAutoSdk'
$toolchain = Join-Path $autoSdk 'HostWin64\Win64\VS2026\14.50.35717'
New-Item -ItemType Directory -Path (Split-Path $toolchain) -Force | Out-Null
New-Item -ItemType Junction -Path $toolchain -Target 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.50.35717'
$env:UE_SDKS_ROOT = $autoSdk
```

成功后日志会出现：

```text
Using Visual Studio 14.50.35738 toolchain (<AutoSDK 路径>) and Windows 10.0.22621.0 SDK (D:\Windows Kits\10).
```

注意目录名用 `14.50.35717`（MSVC 安装目录名），日志里显示的 `14.50.35738` 是工具链内部版本，两者不一致属正常。

## 问题二：UBA 初始化空引用崩溃

现象：

```text
Unhandled exception: NullReferenceException: Object reference not set to an instance of an object.
   at EpicGames.Core.FileSystemReference.CombineStrings(...)
   at UnrealBuildTool.UBAExecutor.Init(...)
```

工具链识别已经在日志里成功之后才会出现，属于构建器自身问题，与本插件源码无关。原因是 UBA 默认把缓存根目录放到 `CommonApplicationData`，该路径在本机解析为空。UE 5.8 中 `-NoUBA` 只关闭 detour，仍然使用 UBA 执行器，所以无法用该开关绕开。

处理方式：把 UBA 根目录指到确定存在的目录。

```powershell
$env:UBA_ROOT = 'D:\Temp\MMD-UBA'
New-Item -ItemType Directory -Path $env:UBA_ROOT -Force | Out-Null
```

## 构建命令

编辑器模块编译：

```powershell
$env:UE_SDKS_ROOT = '<AutoSdk 根>'
$env:UBA_ROOT = 'D:\Temp\MMD-UBA'
& '<引擎>\Engine\Build\BatchFiles\Build.bat' UnrealEditor Win64 Development `
  -Project="<工程>\MMD.uproject" -WaitMutex -NoHotReloadFromIDE
```

发行包（三种配置）：

```powershell
& '<引擎>\Engine\Build\BatchFiles\RunUAT.bat' BuildPlugin `
  -Plugin="<插件>\Ue5MMDTools.uplugin" `
  -Package="<输出>\Ue5MMDTools" `
  -TargetPlatforms=Win64 -HostPlatforms=Win64 `
  -WaitForUATMutex -UTF8Output
```

`BuildPlugin` 会依次构建 UnrealEditor Development、UnrealGame Development、UnrealGame Shipping，任一失败即整体失败。日志末尾出现 `BUILD SUCCESSFUL` 才算通过。

## 发布包清理规则

`BuildPlugin` 输出会带中间产物，打包前删除：

- `Intermediate/`（编译中间文件）
- `HostProject/`（BuildPlugin 生成的临时宿主工程）
- `Binaries/Win64/*.pdb`（调试符号）

同时确认不包含：`*.pmx`、`*.pmd`、`*.wav`、`*.mp3`，以及 `Resources/PreviewMotions` 之外的动作文件。插件自带的 `Config/FilterPlugin.ini` 负责在 BuildPlugin 阶段排除本机测试模型，但打包脚本仍应独立复查一次。

## 验证记录（2026-11-18）

| 项目 | 结果 |
| --- | --- |
| UnrealEditor Win64 Development | 通过 |
| UnrealGame Win64 Development | 通过 |
| UnrealGame Win64 Shipping | 通过 |
| BuildPlugin | BUILD SUCCESSFUL |
| 发布包大小 | 75,550,910 字节 |
| 发布包 SHA-256 | `c3517b04de704b46371d2e67b3a57e26b3772b703feb4099e99665fcc873a532` |

另做空白宿主加载验证：把发布 ZIP 解压进全新 UE 5.8.3 工程，用 `UnrealEditor-Cmd.exe -nullrhi -unattended -ExecCmds="MMD.OpenPanel;Quit"` 无窗口启动，日志确认两个模块加载、命令被识别执行、`MMDMatEnsure` 正常完成、无致命错误。

该验证覆盖模块加载与启动逻辑，不覆盖面板实际渲染效果，也不等于 Cooked 游戏与画质验收。
