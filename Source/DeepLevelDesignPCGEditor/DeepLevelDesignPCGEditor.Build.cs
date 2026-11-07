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
                "GeometryScriptingCore", "GeometryScriptingEditor", "GeometryCore", "GeometryFramework",
                "AssetRegistry",
                "AssetTools",
                "ComponentVisualizers",
                "ContentBrowser",
                "Core",
                "CoreUObject",
                "DeepLevelDesignPCG",
                "ZoneGraph",
                "EditorStyle",
                "Engine",
                "GameplayTags",
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
