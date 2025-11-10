using UnrealBuildTool;
using System.IO;

public class BulletVendor : ModuleRules
{
    public BulletVendor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[] { });

        // Expose Bullet includes from ThirdParty/Bullet/src
        string BulletRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "Bullet"));
        string BulletInclude = Path.Combine(BulletRoot, "src");
        if (Directory.Exists(BulletInclude))
        {
            PublicIncludePaths.Add(BulletInclude);
        }

        // Allow exceptions in this module if needed
        bEnableExceptions = true;
    }
}
