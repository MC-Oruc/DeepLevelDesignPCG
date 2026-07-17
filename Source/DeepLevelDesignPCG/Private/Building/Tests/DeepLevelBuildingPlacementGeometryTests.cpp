// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Building/DeepLevelBuildingPCG.h"
#include "Misc/AutomationTest.h"

namespace DeepLevelBuildingPlacementGeometryTests
{
	FVector FaceNormal(const EDeepLevelBuildingVolumeFace Face)
	{
		switch (Face)
		{
		case EDeepLevelBuildingVolumeFace::PositiveX: return FVector::ForwardVector;
		case EDeepLevelBuildingVolumeFace::NegativeX: return -FVector::ForwardVector;
		case EDeepLevelBuildingVolumeFace::PositiveY: return FVector::RightVector;
		case EDeepLevelBuildingVolumeFace::NegativeY: return -FVector::RightVector;
		default: return FVector::ZeroVector;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingVolumeGeometryTest, "DeepLevelDesignPCG.Editor.BuildingCalibration.VolumeGeometry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelBuildingVolumeGeometryTest::RunTest(const FString&)
{
	FDeepLevelBuildingPlacementDefinition Definition;
	Definition.BuildingClass = AActor::StaticClass();
	Definition.PlacementVolume.Center = FVector(100, 20, 50);
	Definition.PlacementVolume.Rotation = FRotator(5, 20, -3);
	Definition.PlacementVolume.Extent = FVector(200, 100, 300);
	Definition.PlacementVolume.Exposure.NegativeY = EDeepLevelStreetExposureRule::Required;
	const FDeepLevelResolvedBuildingGeometry Geometry = FDeepLevelBuildingPlacementGeometry::ResolveGeometry(Definition, EDeepLevelBuildingVolumeFace::NegativeY);
	TestEqual(TEXT("Width"), Geometry.HalfWidth * 2.0, 400.0);
	TestEqual(TEXT("Depth"), Geometry.HalfDepth * 2.0, 200.0);
	const FTransform ActorTransform = FDeepLevelBuildingPlacementGeometry::BuildActorTransform(Definition, EDeepLevelBuildingVolumeFace::NegativeY, FVector(1000,0,0), FVector::ForwardVector, FVector::RightVector);
	const FTransform VolumeWorld = FTransform(Definition.PlacementVolume.Rotation, Definition.PlacementVolume.Center) * ActorTransform;
	TestTrue(TEXT("Volume center"), VolumeWorld.GetLocation().Equals(FVector(1000,100,300), 0.01));
	TestTrue(TEXT("Street normal"), VolumeWorld.TransformVectorNoScale(-FVector::RightVector).Equals(-FVector::RightVector, 0.001));
	TestTrue(TEXT("Up"), VolumeWorld.TransformVectorNoScale(FVector::UpVector).Equals(FVector::UpVector, 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingAllStreetFacesStayUprightTest, "DeepLevelDesignPCG.Editor.BuildingCalibration.AllStreetFacesStayUpright", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelBuildingAllStreetFacesStayUprightTest::RunTest(const FString&)
{
	FDeepLevelBuildingPlacementDefinition Definition;
	Definition.BuildingClass = AActor::StaticClass();
	Definition.PlacementVolume.Extent = FVector(200, 100, 300);

	const TArray<FVector> SplineDirections = {FVector::ForwardVector, -FVector::ForwardVector};
	for (const FVector& SplineForward : SplineDirections)
	{
		const FVector SplineRight = FVector::CrossProduct(FVector::UpVector, SplineForward);
		const FVector ExpectedStreetDirection = -SplineRight;
		for (uint8 FaceIndex = 0; FaceIndex < 4; ++FaceIndex)
		{
			const EDeepLevelBuildingVolumeFace Face = static_cast<EDeepLevelBuildingVolumeFace>(FaceIndex);
			const FTransform ActorTransform = FDeepLevelBuildingPlacementGeometry::BuildActorTransform(
				Definition,
				Face,
				FVector::ZeroVector,
				SplineForward,
				SplineRight);
			const FTransform VolumeWorld = FTransform(
				Definition.PlacementVolume.Rotation,
				Definition.PlacementVolume.Center) * ActorTransform;

			TestTrue(
				TEXT("Street-facing normal matches spline street direction"),
				VolumeWorld.TransformVectorNoScale(DeepLevelBuildingPlacementGeometryTests::FaceNormal(Face))
					.Equals(ExpectedStreetDirection, 0.001));
			TestTrue(
				TEXT("Placement volume remains upright"),
				VolumeWorld.TransformVectorNoScale(FVector::UpVector).Equals(FVector::UpVector, 0.001));
			TestTrue(
				TEXT("Zero-tilt calibration produces an upright actor"),
				ActorTransform.TransformVectorNoScale(FVector::UpVector).Equals(FVector::UpVector, 0.001));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingStreetExposureTest, "DeepLevelDesignPCG.Editor.BuildingCalibration.StreetExposure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelBuildingStreetExposureTest::RunTest(const FString&)
{
	FDeepLevelBuildingPlacementDefinition Definition;
	Definition.BuildingClass = AActor::StaticClass();
	Definition.PlacementVolume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Forbidden;
	Definition.PlacementVolume.Exposure.NegativeX = EDeepLevelStreetExposureRule::Required;
	Definition.PlacementVolume.Exposure.PositiveY = EDeepLevelStreetExposureRule::Required;
	Definition.PlacementVolume.Exposure.NegativeY = EDeepLevelStreetExposureRule::Preferred;
	for (int32 Seed=0; Seed<32; ++Seed)
	{
		EDeepLevelBuildingVolumeFace Face;
		TestTrue(TEXT("Resolves"), FDeepLevelBuildingPlacementGeometry::ResolveStreetFace(Definition, Seed, Face));
		TestTrue(TEXT("Required only"), Face==EDeepLevelBuildingVolumeFace::NegativeX || Face==EDeepLevelBuildingVolumeFace::PositiveY);
	}
	Definition.PlacementVolume.Exposure.NegativeX = EDeepLevelStreetExposureRule::Forbidden;
	Definition.PlacementVolume.Exposure.PositiveY = EDeepLevelStreetExposureRule::Forbidden;
	Definition.PlacementVolume.Exposure.NegativeY = EDeepLevelStreetExposureRule::Forbidden;
	FText Error;
	TestFalse(TEXT("All forbidden rejected"), FDeepLevelBuildingPlacementGeometry::Validate(Definition, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingVolumeFaceResizeTest, "DeepLevelDesignPCG.Editor.BuildingCalibration.FaceResize", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelBuildingVolumeFaceResizeTest::RunTest(const FString&)
{
	FDeepLevelBuildingPlacementVolume Volume;
	Volume.Center = FVector::ZeroVector;
	Volume.Extent = FVector(100.0, 200.0, 300.0);

	TestTrue(
		TEXT("Positive X face expands"),
		FDeepLevelBuildingPlacementGeometry::ResizeVolumeFace(Volume, EDeepLevelBuildingVolumeFace::PositiveX, 40.0));
	TestEqual(TEXT("X extent expands by half the face distance"), Volume.Extent.X, 120.0);
	TestEqual(TEXT("Center follows the moved positive face"), Volume.Center, FVector(20.0, 0.0, 0.0));

	TestTrue(
		TEXT("Negative Y face contracts"),
		FDeepLevelBuildingPlacementGeometry::ResizeVolumeFace(Volume, EDeepLevelBuildingVolumeFace::NegativeY, -100.0));
	TestEqual(TEXT("Y extent contracts by half the face distance"), Volume.Extent.Y, 150.0);
	TestEqual(TEXT("Center follows the moved negative face"), Volume.Center, FVector(20.0, 50.0, 0.0));

	TestTrue(
		TEXT("Face contraction clamps the extent"),
		FDeepLevelBuildingPlacementGeometry::ResizeVolumeFace(Volume, EDeepLevelBuildingVolumeFace::PositiveX, -1000.0));
	TestEqual(TEXT("Extent remains positive"), Volume.Extent.X, 1.0);
	TestEqual(TEXT("Opposite X face remains fixed"), Volume.Center.X - Volume.Extent.X, -100.0);
	return true;
}

#endif
