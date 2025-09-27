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
    "MeshUtilitiesCommon",     // 👈 添加这个
    "ToolMenus",
    "StaticMeshDescription",   // 👈 添加这个
    "MeshDescription",         // 👈 添加这个
        });

        PublicIncludePaths.AddRange(new string[] { });
        PrivateIncludePaths.AddRange(new string[] { });
        DynamicallyLoadedModuleNames.AddRange(new string[] { });
    }
}