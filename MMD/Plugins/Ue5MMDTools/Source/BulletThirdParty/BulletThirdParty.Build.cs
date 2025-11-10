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

        // Link prebuilt libs if available under lib/<platform>
        string LibDir = Path.Combine(BulletRoot, "lib", Target.Platform.ToString());
        if (Directory.Exists(LibDir))
        {
            if (Target.Platform == UnrealTargetPlatform.Win64)
            {
                string dyn = Path.Combine(LibDir, "BulletDynamics.lib");
                string col = Path.Combine(LibDir, "BulletCollision.lib");
                string lin = Path.Combine(LibDir, "LinearMath.lib");
                if (File.Exists(dyn)) PublicAdditionalLibraries.Add(dyn);
                if (File.Exists(col)) PublicAdditionalLibraries.Add(col);
                if (File.Exists(lin)) PublicAdditionalLibraries.Add(lin);
            }
            else
            {
                // Unix-like static libs
                string dyn = Path.Combine(LibDir, "libBulletDynamics.a");
                string col = Path.Combine(LibDir, "libBulletCollision.a");
                string lin = Path.Combine(LibDir, "libLinearMath.a");
                if (File.Exists(dyn)) PublicAdditionalLibraries.Add(dyn);
                if (File.Exists(col)) PublicAdditionalLibraries.Add(col);
                if (File.Exists(lin)) PublicAdditionalLibraries.Add(lin);
            }
        }

        // If no prebuilt libs found, recommend building Bullet with CMake into the lib folder.
        // The build will still attempt to compile but may fail if headers reference missing symbols.

        // Expose include path to consumers
        PublicSystemIncludePaths.Add(BulletInclude);
    }
}
