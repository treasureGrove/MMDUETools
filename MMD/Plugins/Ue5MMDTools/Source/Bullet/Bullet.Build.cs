using System;
using System.IO;
using UnrealBuildTool;

public class Bullet : ModuleRules
{
    public Bullet(ReadOnlyTargetRules Target) : base(Target)
    {
        // 改为普通模块，这样 UBT 会自动编译源文件
        Type = ModuleType.CPlusPlus;

        string BulletSourcePath = Path.Combine(ModuleDirectory, "src");

        // 添加头文件路径
        PublicIncludePaths.AddRange(new string[] {
            BulletSourcePath
        });

        // 递归添加所有子目录为包含路径
        AddIncludePathsRecursively(Path.Combine(BulletSourcePath, "BulletCollision"));
        AddIncludePathsRecursively(Path.Combine(BulletSourcePath, "BulletDynamics"));
        AddIncludePathsRecursively(Path.Combine(BulletSourcePath, "LinearMath"));

        // Bullet 编译定义
        PublicDefinitions.Add("BT_NO_PROFILE=1");
        PublicDefinitions.Add("BT_USE_DOUBLE_PRECISION=0");

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDefinitions.Add("WIN32");
        }

        // 编译选项
        bEnableExceptions = false;
        bUseRTTI = false;

        // 禁用警告
        bEnableUndefinedIdentifierWarnings = false;
    }

    private void AddIncludePathsRecursively(string Directory)
    {
        if (System.IO.Directory.Exists(Directory))
        {
            PublicIncludePaths.Add(Directory);

            foreach (string SubDir in System.IO.Directory.GetDirectories(Directory))
            {
                AddIncludePathsRecursively(SubDir);
            }
        }
    }
}