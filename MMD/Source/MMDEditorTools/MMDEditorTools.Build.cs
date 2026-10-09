using UnrealBuildTool;
public class MMDEditorTools : ModuleRules
{
    public MMDEditorTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "MMD", "UnrealEd", "Blutility" });
    }
}
