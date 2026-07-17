// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Building/DeepLevelBuildingPCG.h"

#include "Components/SplineComponent.h"
#include "Data/PCGSplineData.h"
#include "Misc/AutomationTest.h"

namespace DeepLevelBuildingLinePathTests
{
	UPCGSplineData* MakeSpline(const ESplinePointType::Type Type)
	{
		TArray<FSplinePoint> Points;
		Points.Emplace(0, FVector(0.0, 0.0, 0.0), Type);
		Points.Emplace(1, FVector(1000.0, 0.0, 0.0), Type);
		Points.Emplace(2, FVector(1000.0, 1000.0, 0.0), Type);
		UPCGSplineData* Spline = NewObject<UPCGSplineData>();
		Spline->Initialize(Points, false, FTransform::Identity);
		return Spline;
	}

	UPCGSplineData* MakeClosedSpline(const ESplinePointType::Type Type)
	{
		TArray<FSplinePoint> Points;
		Points.Emplace(0, FVector(0.0, 0.0, 0.0), Type);
		Points.Emplace(1, FVector(1000.0, 0.0, 0.0), Type);
		Points.Emplace(2, FVector(1000.0, 1000.0, 0.0), Type);
		Points.Emplace(3, FVector(0.0, 1000.0, 0.0), Type);
		UPCGSplineData* Spline = NewObject<UPCGSplineData>();
		Spline->Initialize(Points, true, FTransform::Identity);
		return Spline;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLineMultiSegmentPathTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.MultiSegmentPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLineMultiSegmentPathTest::RunTest(const FString& Parameters)
{
	for (const ESplinePointType::Type Type : {ESplinePointType::Linear, ESplinePointType::Curve, ESplinePointType::CurveClamped})
	{
		FDeepLevelBuildingLinePath Path;
		FText Error;
		TestTrue(TEXT("Supported open spline builds"), FDeepLevelBuildingLinePath::Build(*DeepLevelBuildingLinePathTests::MakeSpline(Type), Path, Error));
		TestTrue(TEXT("Multi-segment path has length"), Path.GetLength() > 1000.0);

		FDeepLevelBuildingLinePathSample Sample;
		TestTrue(TEXT("Path midpoint can be sampled"), Path.Sample(Path.GetLength() * 0.5, Sample));
		TestTrue(TEXT("Path frame is horizontal"), FMath::IsNearlyZero(Sample.Forward.Z));

		TArray<double> HardCornerDistances;
		Path.GetHardCornerDistances(15.0, HardCornerDistances);
		if (Type == ESplinePointType::Linear)
		{
			TestEqual(TEXT("Linear path exposes one hard corner"), HardCornerDistances.Num(), 1);
			if (!HardCornerDistances.IsEmpty())
			{
				TestEqual(TEXT("Hard corner uses cumulative path distance"), HardCornerDistances[0], 1000.0);
			}
		}
	}

	TArray<FSplinePoint> SelfIntersectingPoints;
	SelfIntersectingPoints.Emplace(0, FVector(0.0, 0.0, 0.0), ESplinePointType::Linear);
	SelfIntersectingPoints.Emplace(1, FVector(1000.0, 1000.0, 0.0), ESplinePointType::Linear);
	SelfIntersectingPoints.Emplace(2, FVector(0.0, 1000.0, 0.0), ESplinePointType::Linear);
	SelfIntersectingPoints.Emplace(3, FVector(1000.0, 0.0, 0.0), ESplinePointType::Linear);
	UPCGSplineData* SelfIntersectingSpline = NewObject<UPCGSplineData>();
	SelfIntersectingSpline->Initialize(SelfIntersectingPoints, true, FTransform::Identity);
	FDeepLevelBuildingLinePath InvalidPath;
	FText InvalidError;
	TestFalse(
		TEXT("Self-intersecting closed spline is rejected"),
		FDeepLevelBuildingLinePath::Build(*SelfIntersectingSpline, InvalidPath, InvalidError));
	TestFalse(TEXT("Self-intersecting closed spline reports an error"), InvalidError.IsEmpty());

	FDeepLevelBuildingLinePath ForwardPath;
	FDeepLevelBuildingLinePath ReversedPath;
	TestTrue(TEXT("Forward closed square builds"), FDeepLevelBuildingLinePath::Build(
		*DeepLevelBuildingLinePathTests::MakeClosedSpline(ESplinePointType::Linear),
		ForwardPath,
		InvalidError));
	TArray<FSplinePoint> ReversedPoints;
	ReversedPoints.Emplace(0, FVector(1000.0, 0.0, 0.0), ESplinePointType::Linear);
	ReversedPoints.Emplace(1, FVector(0.0, 0.0, 0.0), ESplinePointType::Linear);
	ReversedPoints.Emplace(2, FVector(0.0, 1000.0, 0.0), ESplinePointType::Linear);
	ReversedPoints.Emplace(3, FVector(1000.0, 1000.0, 0.0), ESplinePointType::Linear);
	UPCGSplineData* ReversedSpline = NewObject<UPCGSplineData>();
	ReversedSpline->Initialize(ReversedPoints, true, FTransform::Identity);
	TestTrue(TEXT("Reversed closed square builds"), FDeepLevelBuildingLinePath::Build(*ReversedSpline, ReversedPath, InvalidError));
	FDeepLevelBuildingLinePathSample ForwardSample;
	FDeepLevelBuildingLinePathSample ReversedSample;
	TestTrue(TEXT("Forward closed square side can be sampled"), ForwardPath.Sample(100.0, ForwardSample));
	TestTrue(TEXT("Reversed closed square side can be sampled"), ReversedPath.Sample(100.0, ReversedSample));
	TestTrue(
		TEXT("Reversing a closed spline reverses the existing placement side"),
		FVector::DotProduct(ForwardSample.Right, ReversedSample.Right) < -0.99);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLinePathValidationTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.PathValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLinePathValidationTest::RunTest(const FString& Parameters)
{
	for (const ESplinePointType::Type Type : {ESplinePointType::Linear, ESplinePointType::Curve, ESplinePointType::CurveClamped})
	{
		FDeepLevelBuildingLinePath Path;
		FText Error;
		TestTrue(
			TEXT("Supported closed spline builds"),
			FDeepLevelBuildingLinePath::Build(*DeepLevelBuildingLinePathTests::MakeClosedSpline(Type), Path, Error));
		TestTrue(TEXT("Closed path retains its topology"), Path.IsClosed());
		FDeepLevelBuildingLinePathSample StartSample;
		FDeepLevelBuildingLinePathSample EndSample;
		TestTrue(TEXT("Closed path start can be sampled"), Path.Sample(0.0, StartSample));
		TestTrue(TEXT("Closed path end wraps to its start"), Path.Sample(Path.GetLength(), EndSample));
		TestTrue(TEXT("Closed path seam location is continuous"), StartSample.Location.Equals(EndSample.Location, 0.1));

		if (Type == ESplinePointType::Linear)
		{
			TArray<double> HardCornerDistances;
			Path.GetHardCornerDistances(15.0, HardCornerDistances);
			TestEqual(TEXT("Closed square exposes its seam and all other corners"), HardCornerDistances.Num(), 4);
			if (!HardCornerDistances.IsEmpty())
			{
				TestEqual(TEXT("Closed square seam is the first corner"), HardCornerDistances[0], 0.0);
			}
		}
	}
	return true;
}

#endif
