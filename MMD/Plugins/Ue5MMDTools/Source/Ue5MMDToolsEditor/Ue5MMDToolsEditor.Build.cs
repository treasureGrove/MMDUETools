using UnrealBuildTool;
using System.IO;
using System.Linq;
public class Ue5MMDToolsEditor : ModuleRules
{
    public Ue5MMDToolsEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicIncludePaths.AddRange(Directory.GetDirectories(Path.Combine(ModuleDirectory, "Public"), "*", SearchOption.AllDirectories).Prepend(Path.Combine(ModuleDirectory, "Public")));
        PrivateIncludePaths.AddRange(Directory.GetDirectories(Path.Combine(ModuleDirectory, "Private"), "*", SearchOption.AllDirectories).Prepend(Path.Combine(ModuleDirectory, "Private")));
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Ue5MMDTools", "Slate", "SlateCore", "UMG", "AnimGraph", "BlueprintGraph", "IKRig", "IKRigEditor" });
        PrivateDependencyModuleNames.AddRange(new[] { "Projects", "RenderCore", "RHI", "AnimGraphRuntime", "LevelSequence", "InputCore", "EditorStyle", "UnrealEd", "EditorFramework", "ToolMenus", "EditorWidgets", "AdvancedPreviewScene", "EditorSubsystem", "LevelEditor", "DesktopPlatform", "AssetTools", "AssetRegistry", "ContentBrowser", "PropertyEditor", "MaterialEditor", "ApplicationCore", "ImageWrapper", "SkeletalMeshUtilitiesCommon", "MeshUtilities", "MeshUtilitiesCommon", "StaticMeshDescription", "MeshDescription", "IKRigDeveloper", "Persona", "SkeletonEditor", "LevelSequenceEditor", "MovieScene", "MovieSceneTracks", "Sequencer", "Json", "JsonUtilities", "Renderer", "KismetCompiler" });
        PrivateDependencyModuleNames.Add("ControlRig");
    }
}
