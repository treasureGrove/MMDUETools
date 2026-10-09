using UnrealBuildTool;
using System.IO;
using System.Linq;
public class Ue5MMDTools : ModuleRules
{
    public Ue5MMDTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicIncludePaths.AddRange(Directory.GetDirectories(Path.Combine(ModuleDirectory, "Public"), "*", SearchOption.AllDirectories).Prepend(Path.Combine(ModuleDirectory, "Public")));
        PrivateIncludePaths.AddRange(Directory.GetDirectories(Path.Combine(ModuleDirectory, "Private"), "*", SearchOption.AllDirectories).Prepend(Path.Combine(ModuleDirectory, "Private")));
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "AnimGraphRuntime", "LevelSequence", "MovieScene", "RenderCore", "RHI" });
        PrivateDependencyModuleNames.AddRange(new[] { "Projects", "Renderer", "AssetRegistry", "MovieSceneTracks" });
        // 运行文件中的编辑器视口兜底只在 Editor 目标编译，不进入游戏构建。
        if (Target.bBuildEditor) { PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "LevelEditor" }); }
        string BulletPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/Bullet"));
        PublicSystemIncludePaths.Add(Path.Combine(BulletPath, "include"));
        PublicAdditionalLibraries.AddRange(new[] { "BulletCollision", "BulletDynamics", "BulletSoftBody", "LinearMath" }.Select(Name => Path.Combine(BulletPath, "lib", Name + ".lib")));
        PublicDefinitions.AddRange(new[] { "BT_USE_DOUBLE_PRECISION", "BT_THREADSAFE=0", "BT_NO_PROFILE=1" });
    }
}
