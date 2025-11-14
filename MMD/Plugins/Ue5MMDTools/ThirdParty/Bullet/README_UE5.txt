Bullet Physics Library for Unreal Engine 5
==========================================

✅ 此包已使用 /MD 运行库编译，完全兼容 UE5！

目录结构
--------
MMD_Bullet_Libs_UE5/
├── lib/
│   └── x64/
│       ├── Release/                 ⭐ 使用 /MD (Multi-threaded DLL)
│       │   ├── BulletCollision_vs2010_x64_release.lib     (3.20 MB)
│       │   ├── BulletDynamics_vs2010_x64_release.lib      (2.21 MB)
│       │   ├── BulletSoftBody_vs2010_x64_release.lib      (2.50 MB)
│       │   ├── LinearMath_vs2010_x64_release.lib          (0.39 MB)
│       │   ├── Bullet3Common_vs2010_x64_release.lib       (0.02 MB)
│       │   ├── Bullet3Collision_vs2010_x64_release.lib    (0.28 MB)
│       │   ├── Bullet3Dynamics_vs2010_x64_release.lib     (0.24 MB)
│       │   └── Bullet3Geometry_vs2010_x64_release.lib     (0.10 MB)
│       │
│       └── Debug/                   ⭐ 使用 /MDd (Multi-threaded Debug DLL)
│           ├── BulletCollision_vs2010_x64_debug.lib       (14.38 MB)
│           ├── BulletDynamics_vs2010_x64_debug.lib        (9.56 MB)
│           ├── BulletSoftBody_vs2010_x64_debug.lib        (10.60 MB)
│           ├── LinearMath_vs2010_x64_debug.lib            (1.66 MB)
│           ├── Bullet3Common_vs2010_x64_debug.lib         (0.14 MB)
│           ├── Bullet3Collision_vs2010_x64_debug.lib      (1.34 MB)
│           ├── Bullet3Dynamics_vs2010_x64_debug.lib       (1.13 MB)
│           └── Bullet3Geometry_vs2010_x64_debug.lib       (0.48 MB)
│
└── include/
    ├── BulletCollision/
    ├── BulletDynamics/
    ├── BulletSoftBody/
    ├── LinearMath/
    ├── Bullet3Common/
    ├── Bullet3Collision/
    ├── Bullet3Dynamics/
    └── Bullet3Geometry/

构建配置
--------
✅ 运行库: /MD (Release) 和 /MDd (Debug) - 与 UE5 完全匹配！
✅ 平台: x64
✅ 双精度: 已启用 (BT_USE_DOUBLE_PRECISION)
✅ 多线程: 已启用
✅ Stable PD: 已启用
✅ 构建日期: 2025-11-13

UE5 插件集成指南
===============

方法 1: 在 Build.cs 中配置（推荐）
--------------------------------

1. 将 MMD_Bullet_Libs_UE5 文件夹复制到:
   YourPlugin/Source/ThirdParty/Bullet/

2. 创建或编辑 YourPlugin/Source/ThirdParty/Bullet/Bullet.Build.cs:

```csharp
using UnrealBuildTool;
using System.IO;

public class Bullet : ModuleRules
{
    public Bullet(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;
        
        string BulletPath = ModuleDirectory;
        string IncludePath = Path.Combine(BulletPath, "include");
        string LibPath = Path.Combine(BulletPath, "lib", "x64");
        
        // 添加头文件路径
        PublicSystemIncludePaths.Add(IncludePath);
        
        // 根据配置选择库文件
        string ConfigPath = Target.Configuration == UnrealTargetConfiguration.Debug && 
                           Target.bDebugBuildsActuallyUseDebugCRT
            ? Path.Combine(LibPath, "Debug")
            : Path.Combine(LibPath, "Release");
        
        string LibSuffix = Target.Configuration == UnrealTargetConfiguration.Debug && 
                          Target.bDebugBuildsActuallyUseDebugCRT
            ? "_debug.lib"
            : "_release.lib";
        
        // 链接核心库（MMD 最常用的 4 个库）
        PublicAdditionalLibraries.Add(Path.Combine(ConfigPath, 
            $"BulletCollision_vs2010_x64{LibSuffix}"));
        PublicAdditionalLibraries.Add(Path.Combine(ConfigPath, 
            $"BulletDynamics_vs2010_x64{LibSuffix}"));
        PublicAdditionalLibraries.Add(Path.Combine(ConfigPath, 
            $"BulletSoftBody_vs2010_x64{LibSuffix}"));
        PublicAdditionalLibraries.Add(Path.Combine(ConfigPath, 
            $"LinearMath_vs2010_x64{LibSuffix}"));
        
        // 如果需要 Bullet3 库，取消注释以下行:
        // PublicAdditionalLibraries.Add(Path.Combine(ConfigPath, 
        //     $"Bullet3Common_vs2010_x64{LibSuffix}"));
        // PublicAdditionalLibraries.Add(Path.Combine(ConfigPath, 
        //     $"Bullet3Collision_vs2010_x64{LibSuffix}"));
        // PublicAdditionalLibraries.Add(Path.Combine(ConfigPath, 
        //     $"Bullet3Dynamics_vs2010_x64{LibSuffix}"));
        // PublicAdditionalLibraries.Add(Path.Combine(ConfigPath, 
        //     $"Bullet3Geometry_vs2010_x64{LibSuffix}"));
        
        // 定义宏
        PublicDefinitions.Add("BT_USE_DOUBLE_PRECISION");
        
        // 禁用 Bullet 警告
        bEnableUndefinedIdentifierWarnings = false;
    }
}
```

3. 在您的主模块 Build.cs 中添加依赖:

```csharp
public class YourModule : ModuleRules
{
    public YourModule(ReadOnlyTargetRules Target) : base(Target)
    {
        // ... 其他配置 ...
        
        PublicDependencyModuleNames.AddRange(new string[] 
        { 
            "Core", 
            "CoreUObject", 
            "Engine",
            // ... 其他模块 ...
        });
        
        // 添加 Bullet 依赖
        PrivateDependencyModuleNames.Add("Bullet");
    }
}
```

方法 2: 直接在主模块 Build.cs 中链接
-----------------------------------

如果不想创建独立的 Bullet 模块，可以直接在主模块中配置:

```csharp
public class YourModule : ModuleRules
{
    public YourModule(ReadOnlyTargetRules Target) : base(Target)
    {
        // ... 其他配置 ...
        
        // Bullet Physics 配置
        string BulletPath = Path.Combine(ModuleDirectory, "..", "ThirdParty", "Bullet");
        string IncludePath = Path.Combine(BulletPath, "include");
        string LibPath = Path.Combine(BulletPath, "lib", "x64");
        
        PublicSystemIncludePaths.Add(IncludePath);
        
        string ConfigPath = Target.Configuration == UnrealTargetConfiguration.Debug && 
                           Target.bDebugBuildsActuallyUseDebugCRT
            ? Path.Combine(LibPath, "Debug")
            : Path.Combine(LibPath, "Release");
        
        string LibSuffix = Target.Configuration == UnrealTargetConfiguration.Debug && 
                          Target.bDebugBuildsActuallyUseDebugCRT
            ? "_debug.lib"
            : "_release.lib";
        
        PublicAdditionalLibraries.Add(Path.Combine(ConfigPath, 
            $"BulletCollision_vs2010_x64{LibSuffix}"));
        PublicAdditionalLibraries.Add(Path.Combine(ConfigPath, 
            $"BulletDynamics_vs2010_x64{LibSuffix}"));
        PublicAdditionalLibraries.Add(Path.Combine(ConfigPath, 
            $"BulletSoftBody_vs2010_x64{LibSuffix}"));
        PublicAdditionalLibraries.Add(Path.Combine(ConfigPath, 
            $"LinearMath_vs2010_x64{LibSuffix}"));
        
        PublicDefinitions.Add("BT_USE_DOUBLE_PRECISION");
    }
}
```

使用示例
--------

在您的 C++ 代码中:

```cpp
// 包含 Bullet 头文件
#include "btBulletDynamicsCommon.h"
#include "BulletSoftBody/btSoftBody.h"
#include "BulletSoftBody/btSoftRigidDynamicsWorld.h"

// 使用 Bullet Physics
void YourClass::InitPhysics()
{
    // 创建碰撞配置
    btDefaultCollisionConfiguration* collisionConfiguration = 
        new btDefaultCollisionConfiguration();
    
    // 创建调度器
    btCollisionDispatcher* dispatcher = 
        new btCollisionDispatcher(collisionConfiguration);
    
    // 创建宽相碰撞检测
    btBroadphaseInterface* overlappingPairCache = 
        new btDbvtBroadphase();
    
    // 创建约束求解器
    btSequentialImpulseConstraintSolver* solver = 
        new btSequentialImpulseConstraintSolver();
    
    // 创建动力学世界
    btDiscreteDynamicsWorld* dynamicsWorld = 
        new btDiscreteDynamicsWorld(dispatcher, overlappingPairCache, 
                                   solver, collisionConfiguration);
    
    dynamicsWorld->setGravity(btVector3(0, 0, -980));
    
    // ... 添加刚体和软体 ...
}
```

常见问题排查
-----------

### LNK2038: 检测到"RuntimeLibrary"的不匹配

✅ **已解决！** 此包使用 /MD 运行库编译，与 UE5 默认配置匹配。

### LNK2019: 无法解析的外部符号

确认以下事项:
1. 所有必需的 .lib 文件都已链接
2. 包含路径正确设置
3. BT_USE_DOUBLE_PRECISION 宏已定义

### 编译警告: 找不到头文件

检查:
- PublicSystemIncludePaths 是否正确添加
- include 目录路径是否正确

### Debug 模式链接错误

确认 Target.bDebugBuildsActuallyUseDebugCRT 的处理逻辑:
- Debug Editor 构建通常使用 Release 库
- 只有 Debug Game 才使用 Debug 库

技术支持
--------
Bullet Physics 官方文档: https://pybullet.org/
GitHub 仓库: https://github.com/bulletphysics/bullet3

构建信息
--------
编译器: Visual Studio 2022
工具集: v143
平台: Windows x64
Bullet 版本: 3.x
构建时间: 2025-11-13

---

祝您开发顺利！🎉
