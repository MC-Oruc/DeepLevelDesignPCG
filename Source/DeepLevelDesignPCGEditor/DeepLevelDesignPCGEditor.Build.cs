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
                "AdvancedPreviewScene",
                "AssetRegistry",
                "AssetTools",
                "ComponentVisualizers",
                "ContentBrowser",
                "Core",
                "CoreUObject",
                "DeepLevelDesignPCG",
                "EditorStyle",
                "Engine",
                "InputCore",
                "ImageWrapper",
                "Json",
                "RenderCore",
                "ToolsetRegistry",
                "LevelEditor",
                "MessageLog",
                "PCG",
                "PropertyEditor",
                "Slate",
                "SlateCore",
                "ToolMenus",
                "UnrealEd",
            });
    }
}
