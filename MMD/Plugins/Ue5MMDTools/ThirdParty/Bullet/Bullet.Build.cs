// Bullet.Build.cs
// 将此文件放在: YourPlugin/Source/ThirdParty/Bullet/Bullet.Build.cs

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
        
        // 根据配置选择库文件路径
        bool bUseDebugLibs = Target.Configuration == UnrealTargetConfiguration.Debug && 
                            Target.bDebugBuildsActuallyUseDebugCRT;
        
        string ConfigPath = bUseDebugLibs 
            ? Path.Combine(LibPath, "Debug") 
            : Path.Combine(LibPath, "Release");
        
        string LibSuffix = bUseDebugLibs ? "_debug.lib" : "_release.lib";
        
        // 链接核心库（MMD 最常用的 4 个库）
        string[] BulletLibs = new string[]
        {
            "BulletCollision",
            "BulletDynamics",
            "BulletSoftBody",
            "LinearMath"
        };
        
        foreach (string Lib in BulletLibs)
        {
            PublicAdditionalLibraries.Add(
                Path.Combine(ConfigPath, Lib + "_vs2010_x64" + LibSuffix));
        }
        
        // 如果需要 Bullet3 库，取消注释:
        /*
        string[] Bullet3Libs = new string[]
        {
            "Bullet3Common",
            "Bullet3Collision",
            "Bullet3Dynamics",
            "Bullet3Geometry"
        };
        
        foreach (string Lib in Bullet3Libs)
        {
            PublicAdditionalLibraries.Add(
                Path.Combine(ConfigPath, Lib + "_vs2010_x64" + LibSuffix));
        }
        */
        
        // 定义宏 - 启用双精度
        PublicDefinitions.Add("BT_USE_DOUBLE_PRECISION");
        
        // 禁用 Bullet 警告
        bEnableUndefinedIdentifierWarnings = false;
    }
}
