---
kind: error_handling
name: 错误处理：基于 UE_LOG 的轻量日志输出
category: error_handling
scope:
    - '**'
source_files:
    - MMD/Source/MMD/MMDCharacter.cpp
---

本仓库是一个 Unreal Engine 5 MMD 导入与播放插件，代码量较小且主要围绕角色、工厂类与函数库展开。在错误处理方面，仓库未定义任何自定义错误类型、错误码枚举或统一的异常/返回值约定，而是直接依赖 Unreal 引擎内置的 `UE_LOG` 宏进行问题诊断。

具体表现：
- 仅有一处使用 `UE_LOG(LogTemplateCharacter, Error, ...)` 输出错误信息（MMDCharacter.cpp 第 91 行），用于检测 Enhanced Input 组件缺失的情况。
- 未发现 `check()` / `ensure()` 断言、`throw` / `catch`、`panic` / `recover` 等机制的使用。
- 没有集中式的错误处理中间件、错误包装器或统一返回码体系。

结论：该仓库对“错误处理”这一横切关注点几乎未做专门设计，属于低复杂度插件中常见的“以日志代替结构化错误处理”的做法。若后续扩展为更复杂的 MMD 解析管线，建议引入自定义错误类型（如 `EMMDParseError`）并配合 `ensureMsgf` 或可传播的 `FError` 结构体，以便上层能区分可恢复与不可恢复错误。