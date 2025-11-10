using UnrealBuildTool;
using System.IO;

public class BulletVendor : ModuleRules
{
    public BulletVendor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

        // Expose Bullet includes from ThirdParty/Bullet/src
        string BulletRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "Bullet"));
        string BulletInclude = Path.Combine(BulletRoot, "src");
        if (Directory.Exists(BulletInclude))
        {
            PublicIncludePaths.Add(BulletInclude);
        }

        // Define BT_THREADSAFE so btThreads.h conditional compiles cleanly
        PublicDefinitions.Add("BT_THREADSAFE=1");

        // Try to link prebuilt libs in build/lib/Release or lib/Win64
        string BuildLibRelease = Path.Combine(BulletRoot, "build", "lib", "Release");
        if (Directory.Exists(BuildLibRelease) && Target.Platform == UnrealTargetPlatform.Win64)
        {
            string dyn = Path.Combine(BuildLibRelease, "BulletDynamics.lib");
            string col = Path.Combine(BuildLibRelease, "BulletCollision.lib");
            string lin = Path.Combine(BuildLibRelease, "LinearMath.lib");
            if (File.Exists(dyn)) PublicAdditionalLibraries.Add(dyn);
            if (File.Exists(col)) PublicAdditionalLibraries.Add(col);
            if (File.Exists(lin)) PublicAdditionalLibraries.Add(lin);
        }
        else
        {
            string LibDirPlatform = Path.Combine(BulletRoot, "lib", Target.Platform.ToString());
            if (Directory.Exists(LibDirPlatform) && Target.Platform == UnrealTargetPlatform.Win64)
            {
                string dyn = Path.Combine(LibDirPlatform, "BulletDynamics.lib");
                string col = Path.Combine(LibDirPlatform, "BulletCollision.lib");
                string lin = Path.Combine(LibDirPlatform, "LinearMath.lib");
                if (File.Exists(dyn)) PublicAdditionalLibraries.Add(dyn);
                if (File.Exists(col)) PublicAdditionalLibraries.Add(col);
                if (File.Exists(lin)) PublicAdditionalLibraries.Add(lin);
            }
        }

        // Allow exceptions if Bullet needs it
        bEnableExceptions = true;
    }
}
