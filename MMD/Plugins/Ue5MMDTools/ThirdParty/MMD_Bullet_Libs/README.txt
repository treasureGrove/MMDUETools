Bullet Physics Library for MMD (MikuMikuDance)
==============================================

这个文件夹包含了用于 MMD 项目的 Bullet Physics SDK 编译库文件。

目录结构：
----------
MMD_Bullet_Libs/
├── lib/
│   └── x64/
│       ├── Debug/          # Debug 版本库文件
│       │   ├── BulletCollision_vs2010_x64_debug.lib
│       │   ├── BulletDynamics_vs2010_x64_debug.lib
│       │   ├── BulletSoftBody_vs2010_x64_debug.lib
│       │   └── LinearMath_vs2010_x64_debug.lib
│       └── Release/        # Release 版本库文件
│           ├── BulletCollision_vs2010_x64_release.lib
│           ├── BulletDynamics_vs2010_x64_release.lib
│           ├── BulletSoftBody_vs2010_x64_release.lib
│           └── LinearMath_vs2010_x64_release.lib
└── include/                # 头文件
    ├── BulletCollision/
    ├── BulletDynamics/
    ├── BulletSoftBody/
    ├── LinearMath/
    └── ...

库文件说明：
-----------
• BulletCollision - 碰撞检测库 (3.36 MB Release / 15.07 MB Debug)
• BulletDynamics - 刚体动力学库 (2.32 MB Release / 10.02 MB Debug)
• BulletSoftBody - 软体物理库 (4.08 MB Release / 15.74 MB Debug)
• LinearMath - 数学运算库 (408 KB Release / 1.74 MB Debug)

使用方法：
---------
1. 在项目中添加 include 目录到头文件搜索路径
2. 根据编译配置选择对应的库文件进行链接：
   - Debug 配置: 链接 lib/x64/Debug/ 中的库
   - Release 配置: 链接 lib/x64/Release/ 中的库

Visual Studio 配置示例：
-----------------------
项目属性 → C/C++ → 常规 → 附加包含目录：
  $(ProjectDir)MMD_Bullet_Libs\include

项目属性 → 链接器 → 常规 → 附加库目录：
  Debug: $(ProjectDir)MMD_Bullet_Libs\lib\x64\Debug
  Release: $(ProjectDir)MMD_Bullet_Libs\lib\x64\Release

项目属性 → 链接器 → 输入 → 附加依赖项：
  BulletCollision_vs2010_x64_release.lib
  BulletDynamics_vs2010_x64_release.lib
  BulletSoftBody_vs2010_x64_release.lib
  LinearMath_vs2010_x64_release.lib

编译信息：
---------
• Bullet 版本: 3.x
• 编译平台: Visual Studio 2010+ (x64)
• 配置选项: 
  - Double Precision (--double)
  - Stable PD (--enable_stable_pd)
  - Multithreading (--enable_multithreading)
• 编译日期: 2025-11-12

注意事项：
---------
• 确保您的项目编译架构为 x64
• Debug 和 Release 库不能混用
• 使用 double 精度浮点数 (对应编译选项 --double)

更多信息：
---------
Bullet Physics 官方网站: https://pybullet.org/
GitHub: https://github.com/bulletphysics/bullet3
