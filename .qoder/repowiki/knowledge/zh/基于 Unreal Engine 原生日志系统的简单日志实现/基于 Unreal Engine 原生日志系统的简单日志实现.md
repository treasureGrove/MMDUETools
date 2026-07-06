---
kind: logging_system
name: 基于 Unreal Engine 原生日志系统的简单日志实现
category: logging_system
scope:
    - '**'
source_files:
    - MMD/Source/MMD/MMDCharacter.h
    - MMD/Source/MMD/MMDCharacter.cpp
---

该仓库中的日志系统完全依赖 Unreal Engine 5 的原生日志框架，采用最基础的 DEFINE_LOG_CATEGORY/UE_LOG 模式，没有自定义的日志封装或中间层。

**核心实现：**
- 在 `MMDCharacter.h` 中通过 `DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All)` 声明日志类别
- 在 `MMDCharacter.cpp` 中使用 `DEFINE_LOG_CATEGORY(LogTemplateCharacter)` 定义日志类别
- 仅在输入组件初始化失败时使用 `UE_LOG(LogTemplateCharacter, Error, ...)` 输出错误信息

**日志级别使用：**
- 仅使用了 Error 级别进行关键错误提示
- 未使用 Warning、Log、Display、Verbose、Fatal 等其他级别
- 所有日志都集中在角色类的输入配置验证逻辑中

**架构特点：**
- 无集中式日志管理器或日志工厂类
- 无结构化日志字段或上下文信息
- 无日志文件输出配置或滚动策略
- 无日志过滤或动态级别控制
- 遵循 UE 模板项目的标准日志模式

**开发规范：**
- 日志类别命名遵循 `LogXxx` 格式（如 `LogTemplateCharacter`）
- 使用 TEXT() 宏包装字符串以支持 Unicode
- 通过 GetNameSafe(this) 获取对象名称用于调试信息
- 错误信息包含具体的失败原因和解决方案建议

由于这是基于 UE 模板项目构建的插件，日志系统保持极简风格，仅满足基本的调试需求。