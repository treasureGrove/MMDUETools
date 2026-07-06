---
kind: configuration_system
name: Unreal Engine 原生配置体系
category: configuration_system
scope:
    - '**'
source_files:
    - MMD/Source/MMD/MMDCharacter.h
---

本仓库为 Unreal Engine 5 MMD 插件，未实现自定义配置系统，完全依赖 UE 引擎内置的配置机制。核心要点如下：

1. **使用方式**：通过 `UCLASS(config=Game)` 宏将类声明为可持久化到 INI 文件（如 `DefaultGame.ini`），配合 `UPROPERTY(EditAnywhere, Config=true)` 暴露字段供编辑器或运行时读写。
2. **当前状态**：仅 `MMDCharacter.h` 中 `AMMDCharacter` 使用了 `config=Game`，其余模块未见显式配置类；项目根目录的 `MMD/Config/` 下仅有引擎默认模板 INI 文件（`DefaultEngine.ini`、`DefaultEditor.ini` 等），无插件专属配置文件。
3. **约定与约束**：新增可配置参数应遵循 UE 标准——在头文件中用 `UCLASS(config=<SectionName>)` 标记类，并用 `UPROPERTY(Config=true, EditAnywhere, BlueprintReadWrite)` 暴露字段，由引擎自动序列化至对应 INI。
4. **不适用说明**：仓库不包含独立于 UE 的 YAML/TOML/JSON 加载器、环境变量解析、Feature Flag 系统等自定义配置层，因此本类别在此仓库中属于“低适用度”。