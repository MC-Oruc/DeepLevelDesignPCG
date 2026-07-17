// Copyright <--\, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DeepLevelDesignPCGEditor : ModuleRules
{
    public DeepLevelDesignPCGEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
            new[]
            {
                "Core",
                "CoreUObject",
                "DeepLevelDesignPCG",
                "Engine",
                "UnrealEd",
            });
    }
}

