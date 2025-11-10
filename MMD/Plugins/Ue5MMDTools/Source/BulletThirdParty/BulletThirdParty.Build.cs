using UnrealBuildTool;
using System.IO;

public class BulletThirdParty : ModuleRules
{
    public BulletThirdParty(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        // Expect vendorized Bullet placed at Plugins/Ue5MMDTools/ThirdParty/Bullet
        string BulletRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "Bullet"));
        string BulletInclude = Path.Combine(BulletRoot, "src");

        // Add includes if exist
        if (Directory.Exists(BulletInclude))
        {
            PublicIncludePaths.Add(BulletInclude);
        }

        // Look for prebuilt libs in build/lib/Release or lib/<platform>
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

        // Fallback search in lib/<platform>
        string LibDirPlatform = Path.Combine(BulletRoot, "lib", Target.Platform.ToString());
        if (Directory.Exists(LibDirPlatform))
        {
            if (Target.Platform == UnrealTargetPlatform.Win64)
            {
                string dyn = Path.Combine(LibDirPlatform, "BulletDynamics.lib");
                string col = Path.Combine(LibDirPlatform, "BulletCollision.lib");
                string lin = Path.Combine(LibDirPlatform, "LinearMath.lib");
                if (File.Exists(dyn)) PublicAdditionalLibraries.Add(dyn);
                if (File.Exists(col)) PublicAdditionalLibraries.Add(col);
                if (File.Exists(lin)) PublicAdditionalLibraries.Add(lin);
            }
            else
            {
                string dyn = Path.Combine(LibDirPlatform, "libBulletDynamics.a");
                string col = Path.Combine(LibDirPlatform, "libBulletCollision.a");
                string lin = Path.Combine(LibDirPlatform, "libLinearMath.a");
                if (File.Exists(dyn)) PublicAdditionalLibraries.Add(dyn);
                if (File.Exists(col)) PublicAdditionalLibraries.Add(col);
                if (File.Exists(lin)) PublicAdditionalLibraries.Add(lin);
            }
        }

        // Expose include path to consumers
        PublicSystemIncludePaths.Add(BulletInclude);
    }
}
