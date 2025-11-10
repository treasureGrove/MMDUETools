using UnrealBuildTool;
using System.IO;

public class Ue5MMDTools : ModuleRules
{
    public Ue5MMDTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "Slate", "SlateCore" ,  "EditorStyle",
            "RenderCore", "RHI",   "AnimGraph",
    "AnimGraphRuntime",
    "BlueprintGraph",
    "KismetCompiler",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Projects",
            "InputCore",
            "UnrealEd",
            "EditorFramework",
            "EditorStyle",
            "ToolMenus",
            "EditorWidgets",
            "AdvancedPreviewScene",
            "EditorSubsystem",
            "LevelEditor",
            "DesktopPlatform",
            "AssetTools",
            "AssetRegistry",
             "ApplicationCore",
             "Engine",
             "EditorSubsystem",
            "ApplicationCore",
    "SkeletalMeshUtilitiesCommon",
    "MeshUtilities",
    "MeshUtilitiesCommon",
    "ToolMenus",
    "StaticMeshDescription",
    "MeshDescription",
     "IKRig",              
                "IKRigEditor",   
                "IKRigDeveloper",
                "Persona",
                "SkeletonEditor",
                "BulletVendor"

        });

        // Expose bullet headers
        string BulletRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "Bullet"));
        PublicIncludePaths.Add(Path.Combine(BulletRoot, "src"));

        PublicIncludePaths.AddRange(new string[] { });
        PrivateIncludePaths.AddRange(new string[] { });
        DynamicallyLoadedModuleNames.AddRange(new string[] { });
    }
}