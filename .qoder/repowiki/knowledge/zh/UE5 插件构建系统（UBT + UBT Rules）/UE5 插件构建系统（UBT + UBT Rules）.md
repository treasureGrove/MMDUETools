---
kind: build_system
name: UE5 插件构建系统（UBT + UBT Rules）
category: build_system
scope:
    - '**'
source_files:
    - MMD/MMD.uproject
    - MMD/Source/MMD/MMD.Build.cs
    - MMD/Source/MMD.Target.cs
    - MMD/Source/MMDEditor.Target.cs
    - MMD/Scripts/build_plugin.bat
---

本仓库是一个 Unreal Engine 5.5 MMD 导入与播放插件，其构建完全依赖 UE 官方工具链，未引入外部构建框架。核心构建体系如下：

1. **项目定义** — `MMD/MMD.uproject` 声明模块名 `MMD`、类型 `Runtime`、加载阶段 `Default`，并依赖 `Blutility`、`UnrealEd`、`CoreUObject`；同时启用编辑器专属插件 `ModelingToolsEditorMode`。
2. **模块规则** — `MMD/Source/MMD/MMD.Build.cs` 通过 `ModuleRules` 扩展 UBT，声明公共依赖 `Core`、`CoreUObject`、`Engine`、`InputCore`、`EnhancedInput`，并使用显式 PCH。
3. **目标文件** — `MMD/Source/MMD.Target.cs` 与 `MMDEditor.Target.cs` 分别定义运行时与编辑器目标的编译选项。
4. **本地自动化脚本** — `MMD/Scripts/build_plugin.bat` 串联三步流程：
   - 调用引擎的 `GenerateProjectFiles.bat` 生成 VS 工程；
   - 优先用 `dotnet build UE5Rules.csproj` 编译 UE5Rules，失败则回退到 MSBuild；
   - 最终执行 `Build.bat UE5Editor Win64 Development <uproject> -WaitMutex -FromMsBuild -NoHotReload -NoUBTMakefiles -NoPCH -Verbose` 完成完整构建。
5. **IDE 集成** — `.vscode/tasks.json` / `compileCommands_*.json` 提供 VS Code 任务与 IntelliSense 索引；`.vs/` 下为 Visual Studio 快照。

**约定与约束**
- 引擎路径与 uproject 路径硬编码在 `build_plugin.bat` 顶部，需按本机环境修改 `ENGINE` 与 `UPROJECT` 变量。
- 构建产物位于 `MMD/Binaries`、`Intermediate`、`DerivedDataCache`，均被 `.gitignore` 忽略。
- 无 Dockerfile、CI 流水线或跨平台 Makefile，仅支持 Windows + VS 2022 环境。
- 版本锁定由 `MMD.uproject` 中的 `EngineAssociation: "5.5"` 指定，不采用语义化版本号管理。

该方案是标准的 UE 插件开发模式，适合本地迭代，但缺乏远程 CI 与多平台构建能力。