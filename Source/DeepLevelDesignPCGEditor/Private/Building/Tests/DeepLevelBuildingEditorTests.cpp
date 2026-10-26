// Copyright <--\, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Building/DeepLevelBuildingPCG.h"
#include "Building/DeepLevelBuildingPreviewPresentation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingPreviewExposureTest,
	"DeepLevelDesignPCG.Editor.BuildingCalibration.PreviewExposure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingPreviewExposureTest::RunTest(const FString&)
{
	using namespace DeepLevelBuildingPreviewPresentation;
	FDeepLevelBuildingPlacementVolume Volume;
	Volume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Preferred;
	Volume.Exposure.NegativeY = EDeepLevelStreetExposureRule::Required;
	TestTrue(TEXT("Required face outranks preferred and neutral"), ViewDirection(Volume).Equals(-FVector::RightVector));
	Volume.Exposure.NegativeY = EDeepLevelStreetExposureRule::Forbidden;
	Volume.Exposure.PositiveY = EDeepLevelStreetExposureRule::Preferred;
	const FVector Diagonal = FVector(1, 1, 0).GetSafeNormal();
	TestTrue(TEXT("Equal adjacent preferred faces share the camera angle"), ViewDirection(Volume).Equals(Diagonal));
	Volume.Exposure.PositiveX = Volume.Exposure.PositiveY = EDeepLevelStreetExposureRule::Required;
	Volume.Rotation = FRotator(0, 45, 0);
	const FTransform Actor(FRotator(0, 30, 0));
	TestTrue(TEXT("Calibration and actor rotation both affect the view"), ViewDirection(Volume, Actor).Equals(FRotator(0, 75, 0).RotateVector(Diagonal)));
	Volume.Rotation = FRotator::ZeroRotator;
	Volume.Exposure.PositiveY = EDeepLevelStreetExposureRule::Forbidden;
	Volume.Exposure.NegativeX = EDeepLevelStreetExposureRule::Required;
	TestTrue(TEXT("Opposite faces choose a deterministic nonzero direction"), ViewDirection(Volume).Equals(FVector::ForwardVector));
	return true;
}

#endif
