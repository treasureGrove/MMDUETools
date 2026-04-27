using UnrealBuildTool;
using System.IO;
using System.Linq;

public class Ue5MMDTools : ModuleRules
{
    public Ue5MMDTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        string PublicPath = Path.Combine(ModuleDirectory, "Public");
        string PrivatePath = Path.Combine(ModuleDirectory, "Private");

        PublicIncludePaths.AddRange(
            Directory.GetDirectories(PublicPath, "*", SearchOption.AllDirectories)
                .Prepend(PublicPath)
                .ToArray());

        PrivateIncludePaths.AddRange(
            Directory.GetDirectories(PrivatePath, "*", SearchOption.AllDirectories)
                .Prepend(PrivatePath)
                .ToArray());

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "EditorStyle",
            "RenderCore",
            "RHI",
            "AnimGraph",
            "AnimGraphRuntime",
            "BlueprintGraph",
            "KismetCompiler"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Projects",
            "InputCore",
            "UnrealEd",
            "EditorFramework",
            "ToolMenus",
            "EditorWidgets",
            "AdvancedPreviewScene",
            "EditorSubsystem",
            "LevelEditor",
            "DesktopPlatform",
            "AssetTools",
            "AssetRegistry",
            "ApplicationCore",
            "SkeletalMeshUtilitiesCommon",
            "MeshUtilities",
            "MeshUtilitiesCommon",
            "StaticMeshDescription",
            "MeshDescription",
            "IKRig",
            "IKRigEditor",
            "IKRigDeveloper",
            "Persona",
            "SkeletonEditor",
            "Json",
            "JsonUtilities"
        });

        string PluginPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../"));
        string BulletPath = Path.Combine(PluginPath, "ThirdParty/Bullet");

        PublicSystemIncludePaths.Add(Path.Combine(BulletPath, "include"));
        string LibPath = Path.Combine(BulletPath, "lib");
        string LibSuffix = ".lib";

        PublicAdditionalLibraries.AddRange(new string[]
        {
            Path.Combine(LibPath, "BulletCollision" + LibSuffix),
            Path.Combine(LibPath, "BulletDynamics" + LibSuffix),
            Path.Combine(LibPath, "BulletSoftBody" + LibSuffix),
            Path.Combine(LibPath, "LinearMath" + LibSuffix)
        });

        PublicDefinitions.AddRange(new string[]
        {
            "BT_USE_DOUBLE_PRECISION",
            "BT_THREADSAFE=0",
            "BT_NO_PROFILE=1"
        });
    }
}
