// Copyright <--\, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetToolsModule.h"
#include "Misc/AutomationTest.h"
#include "Building/DeepLevelBuildingPCG.h"
#include "GameFramework/Pawn.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingCatalogAssetActionsTest,
	"DeepLevelDesignPCG.Editor.BuildingCalibration.AssetActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingCatalogAssetActionsTest::RunTest(const FString&)
{
	const TSharedPtr<IAssetTypeActions> Actions = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"))
		.Get().GetAssetTypeActionsForClass(UDeepLevelBuildingPlacementCatalog::StaticClass()).Pin();
	TestTrue(TEXT("Building catalog custom asset actions are registered"), Actions.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingCalibrationDataTest,
	"DeepLevelDesignPCG.Editor.BuildingCalibration.DataContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingCalibrationDataTest::RunTest(const FString&)
{
	FDeepLevelBuildingPlacementDefinition Definition;
	TestFalse(TEXT("New building definitions start uncalibrated"), Definition.bCalibrated);
	Definition.PlacementVolume.Center = FVector(10, 20, 30);
	Definition.PlacementVolume.Rotation = FRotator(4, 37, -2);
	Definition.PlacementVolume.Extent = FVector(375, 200, 500);
	Definition.PlacementVolume.Exposure.NegativeY = EDeepLevelStreetExposureRule::Required;
	Definition.bCalibrated = true;
	TestEqual(TEXT("Volume center is retained"), Definition.PlacementVolume.Center, FVector(10, 20, 30));
	TestEqual(TEXT("Volume extent is retained"), Definition.PlacementVolume.Extent, FVector(375, 200, 500));
	TestEqual(TEXT("Required face is retained"), Definition.PlacementVolume.Exposure.NegativeY, EDeepLevelStreetExposureRule::Required);
	TestTrue(TEXT("Calibration state is retained"), Definition.bCalibrated);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingPresetSequenceDataTest,
	"DeepLevelDesignPCG.Editor.BuildingPresetEditor.SequenceData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingPresetSequenceDataTest::RunTest(const FString&)
{
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	FDeepLevelBuildingPlacementDefinition& A = Catalog->Buildings.Emplace_GetRef();
	A.BuildingClass = AActor::StaticClass();
	A.PlacementVolume.Extent.X = 150;
	FDeepLevelBuildingPlacementDefinition& B = Catalog->Buildings.Emplace_GetRef();
	B.BuildingClass = APawn::StaticClass();
	B.PlacementVolume.Extent.X = 250;
	FDeepLevelBuildingSequencePreset& Preset = Catalog->Presets.Emplace_GetRef();
	Preset.Name = TEXT("AB");
	Preset.Buildings = {A.BuildingClass, B.BuildingClass, A.BuildingClass};
	TestEqual(TEXT("Sequence retains duplicates and order"), Preset.Buildings.Num(), 3);
	TestEqual(TEXT("First class"), Preset.Buildings[0].ToString(), A.BuildingClass.ToString());
	TestEqual(TEXT("Second class"), Preset.Buildings[1].ToString(), B.BuildingClass.ToString());
	return true;
}

#endif
