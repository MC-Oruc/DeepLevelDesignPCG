// Copyright <--\, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DeepLevelDesignPCG : ModuleRules
{
    public DeepLevelDesignPCG(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                // Public city layout contracts expose gameplay tags.
                "GameplayTags",
                "PCG",
            });
    }
}
