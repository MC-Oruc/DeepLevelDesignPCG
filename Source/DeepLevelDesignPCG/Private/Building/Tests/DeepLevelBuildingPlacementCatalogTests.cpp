// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Building/DeepLevelBuildingPCG.h"

#include "Misc/AutomationTest.h"
#include "PackedLevelActor/PackedLevelActor.h"

namespace DeepLevelBuildingPlacementCatalogTests
{
	FDeepLevelBuildingPlacementDefinition MakeValidBuilding()
	{
		FDeepLevelBuildingPlacementDefinition Definition;
		Definition.BuildingClass = APackedLevelActor::StaticClass();
		Definition.PlacementVolume.Extent = FVector(100.0);
		Definition.bCalibrated = true;
		return Definition;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingCatalogGenerationValidationTest,
	"DeepLevelDesignPCG.Editor.BuildingCatalog.GenerationValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingCatalogGenerationValidationTest::RunTest(const FString&)
{
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	Catalog->Buildings.Add(DeepLevelBuildingPlacementCatalogTests::MakeValidBuilding());

	FText Error;
	TestTrue(TEXT("Valid catalog is accepted"), Catalog->ValidateForGeneration(Error));
	TestTrue(TEXT("Valid catalog has no error"), Error.IsEmpty());

	Catalog->Buildings[0].bCalibrated = false;
	TestFalse(TEXT("Uncalibrated building is rejected"), Catalog->ValidateForGeneration(Error));
	TestFalse(TEXT("Calibration error is reported"), Error.IsEmpty());

	Catalog->Buildings[0].bCalibrated = true;
	Catalog->Buildings.Add(DeepLevelBuildingPlacementCatalogTests::MakeValidBuilding());
	TestFalse(TEXT("Duplicate building class is rejected"), Catalog->ValidateForGeneration(Error));

	Catalog->Buildings.SetNum(1);
	FDeepLevelBuildingSequencePreset& Preset = Catalog->Presets.Emplace_GetRef();
	Preset.Name = TEXT("MissingReference");
	Preset.Buildings.Add(AActor::StaticClass());
	TestFalse(TEXT("Preset class outside the catalog is rejected"), Catalog->ValidateForGeneration(Error));

	return true;
}

#endif
