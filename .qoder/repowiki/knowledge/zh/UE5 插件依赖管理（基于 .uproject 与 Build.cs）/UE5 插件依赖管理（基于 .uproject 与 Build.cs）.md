---
kind: dependency_management
name: UE5 插件依赖管理（基于 .uproject 与 Build.cs）
category: dependency_management
scope:
    - '**'
source_files:
    - MMD/MMD.uproject
    - MMD/Source/MMD/MMD.Build.cs
---

本仓库是一个 Unreal Engine 5 MMD 导入与播放插件，其“第三方依赖”并非通过通用的包管理器（如 npm、pip、cargo 等）声明，而是遵循 UE 插件体系的标准方式：在 .uproject 中声明运行时模块依赖与编辑器插件依赖，在 *.Build.cs 中声明 C++ 模块链接关系。

1. 使用的系统/方法
- 项目级依赖：MMD/MMD.uproject 的 Modules[].AdditionalDependencies 列出运行时模块（Blutility、UnrealEd、CoreUObject），Plugins[] 启用编辑器插件 ModelingToolsEditorMode。
- 构建期依赖：MMD/Source/MMD/MMD.Build.cs 的 PublicDependencyModuleNames 指定编译时链接的 UE 模块（Core、CoreUObject、Engine、InputCore、EnhancedInput）。
- 引擎版本锁定：.uproject 中的 EngineAssociation: "5.5" 固定了所需 UE 主版本。
- 无 vendoring / lockfile / 私有注册表：仓库未包含任何通用语言包清单或锁文件，所有依赖均指向 UE 引擎内置模块。

2. 关键文件
- MMD/MMD.uproject：插件入口，声明模块与编辑器插件依赖。
- MMD/Source/MMD/MMD.Build.cs：C++ 模块构建规则，声明 PublicDependencyModuleNames。
- MMD/Source/MMD.Target.cs / MMDEditor.Target.cs：Target 层（如有额外依赖会在此扩展）。

3. 架构与约定
- 依赖分层：运行时模块依赖集中在 .uproject；编译期 C++ 依赖集中在 MMD.Build.cs，二者需保持一致。
- 仅依赖 UE 官方模块，不引入外部 C/C++ 库或二进制分发。
- 插件目录 MMD/Plugins/ 为空，表示当前未以子插件形式集成其他第三方 UE 插件。

4. 开发者应遵循的规则
- 新增 UE 模块依赖时，同步更新 .uproject 的 AdditionalDependencies 与 MMD.Build.cs 的 PublicDependencyModuleNames。
- 若需要引入非 UE 原生库（C/C++、第三方二进制），应在 MMD.Build.cs 中通过 PublicAdditionalLibraries / RuntimeDependencies 等方式声明，并考虑将二进制随插件分发或在 CI 中拉取。
- 保持 EngineAssociation 与团队使用的 UE 版本一致，避免跨引擎版本兼容问题。