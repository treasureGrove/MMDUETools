using UnrealBuildTool;

public class Ue5MMDTools : ModuleRules
{
    public Ue5MMDTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "Slate", "SlateCore" ,  "EditorStyle",
            "RenderCore", "RHI","AnimationCore"
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
            "LevelEditor",
            "DesktopPlatform",
            "AssetTools",
            "AssetRegistry",
             "Engine",
    "SkeletalMeshUtilitiesCommon",
    "MeshUtilities",

        });

        PublicIncludePaths.AddRange(new string[] { });
        PrivateIncludePaths.AddRange(new string[] { });
        DynamicallyLoadedModuleNames.AddRange(new string[] { });
    }
}