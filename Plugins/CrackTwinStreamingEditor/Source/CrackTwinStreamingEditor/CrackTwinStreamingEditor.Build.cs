using UnrealBuildTool;

public class CrackTwinStreamingEditor : ModuleRules
{
    public CrackTwinStreamingEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "UnrealEd", "Slate", "SlateCore", "ToolMenus", "Sockets"
        });
    }
}
