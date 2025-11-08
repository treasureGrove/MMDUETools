using System;
using System.IO;
using UnrealBuildTool;

public class Bullet : ModuleRules
{
    public Bullet(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        // Bullet 源码路径
        string BulletSourcePath = Path.Combine(ModuleDirectory, "src");

        // 添加所有头文件路径
        PublicIncludePaths.AddRange(new string[] {
            BulletSourcePath,
            Path.Combine(BulletSourcePath, "BulletCollision"),
            Path.Combine(BulletSourcePath, "BulletDynamics"),
            Path.Combine(BulletSourcePath, "LinearMath")
        });

        // 添加所有 .cpp 源文件
        string[] BulletDirs = new string[]
        {
            Path.Combine(BulletSourcePath, "BulletCollision"),
            Path.Combine(BulletSourcePath, "BulletDynamics"),
            Path.Combine(BulletSourcePath, "LinearMath")
        };

        // 递归添加所有 cpp 文件
        foreach (string Dir in BulletDirs)
        {
            if (Directory.Exists(Dir))
            {
                PublicIncludePaths.Add(Dir);
                AddSourceFilesRecursively(Dir);
            }
        }

        // 禁用 Bullet 的警告
        bEnableExceptions = true;
        bUseRTTI = false;

        PublicDefinitions.Add("BT_NO_PROFILE=1");
        PublicDefinitions.Add("BT_USE_DOUBLE_PRECISION=0");

        // 针对不同编译器的设置
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDefinitions.Add("WIN32");
        }
    }

    private void AddSourceFilesRecursively(string Directory)
    {
        if (System.IO.Directory.Exists(Directory))
        {
            foreach (string FilePath in System.IO.Directory.GetFiles(Directory, "*.cpp", SearchOption.AllDirectories))
            {
                // 这里不需要手动添加，UBT 会自动处理
            }

            foreach (string SubDir in System.IO.Directory.GetDirectories(Directory))
            {
                PublicIncludePaths.Add(SubDir);
            }
        }
    }
}