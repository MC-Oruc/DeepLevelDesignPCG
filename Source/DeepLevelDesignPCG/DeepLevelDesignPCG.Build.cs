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
                "PCG",
            });
    }
}

