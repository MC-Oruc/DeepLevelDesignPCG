// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Building/DeepLevelBuildingPCG.h"

#include "Components/SplineComponent.h"
#include "Data/PCGSplineData.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Misc/AutomationTest.h"
#include "PCGModule.h"
#include "PackedLevelActor/PackedLevelActor.h"

namespace DeepLevelBuildingLinePlannerTests
{
	FDeepLevelBuildingPlacementDefinition MakeBuilding(UClass* Class, const double Width, const double Weight = 1.0)
	{
		FDeepLevelBuildingPlacementDefinition Definition;
		Definition.BuildingClass = Class;
		Definition.PlacementVolume.Extent = FVector(Width * 0.5, 100.0, 100.0);
		Definition.PlacementVolume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Forbidden;
		Definition.PlacementVolume.Exposure.NegativeX = EDeepLevelStreetExposureRule::Forbidden;
		Definition.PlacementVolume.Exposure.PositiveY = EDeepLevelStreetExposureRule::Forbidden;
		Definition.PlacementVolume.Exposure.NegativeY = EDeepLevelStreetExposureRule::Required;
		Definition.SelectionWeight = Weight;
		return Definition;
	}

	bool MakePath(const TArray<FSplinePoint>& Points, FDeepLevelBuildingLinePath& OutPath)
	{
		UPCGSplineData* Spline = NewObject<UPCGSplineData>();
		Spline->Initialize(Points, false, FTransform::Identity);
		FText Error;
		return FDeepLevelBuildingLinePath::Build(*Spline, OutPath, Error);
	}

	bool MakeClosedPath(const TArray<FSplinePoint>& Points, FDeepLevelBuildingLinePath& OutPath)
	{
		UPCGSplineData* Spline = NewObject<UPCGSplineData>();
		Spline->Initialize(Points, true, FTransform::Identity);
		FText Error;
		return FDeepLevelBuildingLinePath::Build(*Spline, OutPath, Error);
	}

	bool MakeStraightPath(const double Length, FDeepLevelBuildingLinePath& OutPath)
	{
		return MakePath(
			{
				FSplinePoint(0, FVector::ZeroVector, ESplinePointType::Linear),
				FSplinePoint(1, FVector(Length, 0.0, 0.0), ESplinePointType::Linear)
			},
			OutPath);
	}

	bool FacadesIntersect(const FDeepLevelBuildingLinePlacement& A, const double AHalfWidth, const FDeepLevelBuildingLinePlacement& B, const double BHalfWidth)
	{
		const FVector2D AForward(A.PathSample.Forward);
		const FVector2D BForward(B.PathSample.Forward);
		const FVector2D ACenter(A.PathSample.Location);
		const FVector2D BCenter(B.PathSample.Location);
		const FVector2D AStart = ACenter - AForward * AHalfWidth;
		const FVector2D AVector = AForward * AHalfWidth * 2.0;
		const FVector2D BStart = BCenter - BForward * BHalfWidth;
		const FVector2D BVector = BForward * BHalfWidth * 2.0;
		const double Denominator = AVector.X * BVector.Y - AVector.Y * BVector.X;
		if (FMath::IsNearlyZero(Denominator))
		{
			return false;
		}
		const FVector2D Offset = BStart - AStart;
		const double T = (Offset.X * BVector.Y - Offset.Y * BVector.X) / Denominator;
		const double U = (Offset.X * AVector.Y - Offset.Y * AVector.X) / Denominator;
		return T > 1.e-3 && T < 1.0 - 1.e-3 && U > 1.e-3 && U < 1.0 - 1.e-3;
	}

	struct FTestFootprint
	{
		FVector2D Center;
		FVector2D Forward;
		FVector2D Right;
		double HalfWidth = 0.0;
		double HalfDepth = 0.0;
	};

	FTestFootprint MakeFootprint(const FDeepLevelBuildingLinePlacement& Placement, const FDeepLevelBuildingPlacementDefinition& Definition)
	{
		const FDeepLevelResolvedBuildingGeometry ResolvedGeometry = FDeepLevelBuildingPlacementGeometry::ResolveGeometry(Definition, Placement.StreetFace);
		FTestFootprint Footprint;
		Footprint.Forward = FVector2D(Placement.PathSample.Forward).GetSafeNormal();
		Footprint.Right = FVector2D(Placement.PathSample.Right).GetSafeNormal();
		Footprint.Center = FVector2D(Placement.PathSample.Location) + Footprint.Right * ResolvedGeometry.HalfDepth;
		Footprint.HalfWidth = ResolvedGeometry.HalfWidth;
		Footprint.HalfDepth = ResolvedGeometry.HalfDepth;
		return Footprint;
	}

	bool FootprintsOverlap(const FTestFootprint& A, const FTestFootprint& B)
	{
		const FVector2D CenterDelta = B.Center - A.Center;
		for (const FVector2D& Axis : {A.Forward, A.Right, B.Forward, B.Right})
		{
			const double ARadius = FMath::Abs(FVector2D::DotProduct(A.Forward, Axis)) * A.HalfWidth
				+ FMath::Abs(FVector2D::DotProduct(A.Right, Axis)) * A.HalfDepth;
			const double BRadius = FMath::Abs(FVector2D::DotProduct(B.Forward, Axis)) * B.HalfWidth
				+ FMath::Abs(FVector2D::DotProduct(B.Right, Axis)) * B.HalfDepth;
			if (FMath::Abs(FVector2D::DotProduct(CenterDelta, Axis)) >= ARadius + BRadius - 1.0)
			{
				return false;
			}
		}
		return true;
	}

	double GetLargestPathGap(
		const FDeepLevelBuildingLinePlan& Plan,
		const UDeepLevelBuildingPlacementCatalog& Catalog,
		const double PathLength)
	{
		(void)Catalog;
		double Cursor = 0.0;
		double LargestGap = 0.0;
		for (const FDeepLevelBuildingLinePlacement& Placement : Plan.Placements)
		{
			LargestGap = FMath::Max(LargestGap, Placement.CoverageStart - Cursor);
			Cursor = FMath::Max(Cursor, Placement.CoverageEnd);
		}
		return FMath::Max(LargestGap, PathLength - Cursor);
	}

	double GetLargestCircularPathGap(const FDeepLevelBuildingLinePlan& Plan, const double PathLength)
	{
		struct FInterval
		{
			double Start = 0.0;
			double End = 0.0;
		};

		TArray<FInterval> Intervals;
		for (const FDeepLevelBuildingLinePlacement& Placement : Plan.Placements)
		{
			const double CoverageLength = Placement.CoverageEnd - Placement.CoverageStart;
			if (CoverageLength >= PathLength - UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				return 0.0;
			}
			double Start = FMath::Fmod(Placement.CoverageStart, PathLength);
			if (Start < 0.0)
			{
				Start += PathLength;
			}
			const double End = Start + CoverageLength;
			if (End <= PathLength)
			{
				Intervals.Add({Start, End});
			}
			else
			{
				Intervals.Add({Start, PathLength});
				Intervals.Add({0.0, End - PathLength});
			}
		}
		if (Intervals.IsEmpty())
		{
			return PathLength;
		}
		Intervals.Sort([](const FInterval& A, const FInterval& B) { return A.Start < B.Start; });
		double FirstStart = Intervals[0].Start;
		double Cursor = Intervals[0].End;
		double LargestGap = 0.0;
		for (int32 Index = 1; Index < Intervals.Num(); ++Index)
		{
			LargestGap = FMath::Max(LargestGap, Intervals[Index].Start - Cursor);
			Cursor = FMath::Max(Cursor, Intervals[Index].End);
		}
		return FMath::Max(LargestGap, FirstStart + PathLength - Cursor);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLineDeterminismTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLineDeterminismTest::RunTest(const FString& Parameters)
{
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	Catalog->Buildings.Add(DeepLevelBuildingLinePlannerTests::MakeBuilding(AActor::StaticClass(), 100.0));
	Catalog->Buildings.Add(DeepLevelBuildingLinePlannerTests::MakeBuilding(APawn::StaticClass(), 150.0));

	FDeepLevelBuildingLinePlan First;
	FDeepLevelBuildingLinePlan Second;
	FDeepLevelBuildingLinePath Path;
	FText Error;
	TestTrue(TEXT("Straight path builds"), DeepLevelBuildingLinePlannerTests::MakeStraightPath(1000.0, Path));
	TestTrue(TEXT("First plan succeeds"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 42, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, First, Error));
	TestTrue(TEXT("Second plan succeeds"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 42, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, Second, Error));
	TestEqual(TEXT("Placement count"), First.Placements.Num(), Second.Placements.Num());
	for (int32 Index = 0; Index < First.Placements.Num() && Index < Second.Placements.Num(); ++Index)
	{
		TestEqual(TEXT("Class path"), First.Placements[Index].BuildingClass.ToString(), Second.Placements[Index].BuildingClass.ToString());
		TestEqual(TEXT("Distance"), First.Placements[Index].Distance, Second.Placements[Index].Distance);
		TestEqual(TEXT("Street face"), First.Placements[Index].StreetFace, Second.Placements[Index].StreetFace);
	}

	FDeepLevelBuildingLinePlan DifferentSeed;
	TestTrue(TEXT("Different-seed plan succeeds"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 99, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, DifferentSeed, Error));
	bool bHasDifferentPlacement = First.Placements.Num() != DifferentSeed.Placements.Num();
	for (int32 Index = 0; !bHasDifferentPlacement && Index < First.Placements.Num(); ++Index)
	{
		bHasDifferentPlacement = First.Placements[Index].BuildingClass != DifferentSeed.Placements[Index].BuildingClass
			|| First.Placements[Index].StreetFace != DifferentSeed.Placements[Index].StreetFace;
	}
	TestTrue(TEXT("Different seed changes the resolved sequence"), bHasDifferentPlacement);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLineClosedLoopPackingTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.ClosedLoopPacking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLineClosedLoopPackingTest::RunTest(const FString& Parameters)
{
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	FDeepLevelBuildingPlacementDefinition CornerBuilding = DeepLevelBuildingLinePlannerTests::MakeBuilding(AActor::StaticClass(), 200.0);
	CornerBuilding.PlacementVolume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Required;
	CornerBuilding.PlacementVolume.Extent.Y = 50.0;
	Catalog->Buildings.Add(CornerBuilding);
	FDeepLevelBuildingPlacementDefinition SpanBuilding = DeepLevelBuildingLinePlannerTests::MakeBuilding(APawn::StaticClass(), 300.0);
	SpanBuilding.PlacementVolume.Extent.Y = 25.0;
	Catalog->Buildings.Add(SpanBuilding);

	FDeepLevelBuildingLinePath Path;
	TestTrue(TEXT("Closed square path builds"), DeepLevelBuildingLinePlannerTests::MakeClosedPath(
		{
			FSplinePoint(0, FVector::ZeroVector, ESplinePointType::Linear),
			FSplinePoint(1, FVector(2500.0, 0.0, 0.0), ESplinePointType::Linear),
			FSplinePoint(2, FVector(2500.0, 2500.0, 0.0), ESplinePointType::Linear),
			FSplinePoint(3, FVector(0.0, 2500.0, 0.0), ESplinePointType::Linear)
		},
		Path));
	TestTrue(TEXT("Square path is closed"), Path.IsClosed());

	FDeepLevelBuildingLinePlan Plan;
	FDeepLevelBuildingLinePlan RepeatedPlan;
	FText Error;
	TestTrue(
		TEXT("Closed square plan succeeds"),
		FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 97, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, Plan, Error));
	TestTrue(
		TEXT("Repeated closed square plan succeeds"),
		FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 97, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, RepeatedPlan, Error));
	TestEqual(TEXT("Closed plan placement count is deterministic"), RepeatedPlan.Placements.Num(), Plan.Placements.Num());
	for (int32 Index = 0; Index < FMath::Min(Plan.Placements.Num(), RepeatedPlan.Placements.Num()); ++Index)
	{
		TestEqual(TEXT("Closed plan class sequence is deterministic"), RepeatedPlan.Placements[Index].BuildingClass.ToString(), Plan.Placements[Index].BuildingClass.ToString());
		TestEqual(TEXT("Closed plan distances are deterministic"), RepeatedPlan.Placements[Index].Distance, Plan.Placements[Index].Distance);
	}

	int32 CornerCount = 0;
	for (const FDeepLevelBuildingLinePlacement& Placement : Plan.Placements)
	{
		CornerCount += Placement.bCornerPlacement ? 1 : 0;
	}
	TestEqual(TEXT("Closed square resolves all four corners including the seam"), CornerCount, 4);
	TestTrue(
		TEXT("Closed square does not retain a building-sized circular gap"),
		DeepLevelBuildingLinePlannerTests::GetLargestCircularPathGap(Plan, Path.GetLength()) < 300.0);
	for (int32 AIndex = 0; AIndex < Plan.Placements.Num(); ++AIndex)
	{
		const FDeepLevelBuildingPlacementDefinition& ADefinition = Plan.Placements[AIndex].BuildingClass == CornerBuilding.BuildingClass
			? CornerBuilding
			: SpanBuilding;
		const DeepLevelBuildingLinePlannerTests::FTestFootprint AFootprint = DeepLevelBuildingLinePlannerTests::MakeFootprint(Plan.Placements[AIndex], ADefinition);
		for (int32 BIndex = AIndex + 1; BIndex < Plan.Placements.Num(); ++BIndex)
		{
			const FDeepLevelBuildingPlacementDefinition& BDefinition = Plan.Placements[BIndex].BuildingClass == CornerBuilding.BuildingClass
				? CornerBuilding
				: SpanBuilding;
			const DeepLevelBuildingLinePlannerTests::FTestFootprint BFootprint = DeepLevelBuildingLinePlannerTests::MakeFootprint(Plan.Placements[BIndex], BDefinition);
			TestFalse(TEXT("Closed square placement footprints do not overlap"), DeepLevelBuildingLinePlannerTests::FootprintsOverlap(AFootprint, BFootprint));
		}
	}

	UDeepLevelBuildingPlacementCatalog* VarietyCatalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	FDeepLevelBuildingPlacementDefinition VarietyA = DeepLevelBuildingLinePlannerTests::MakeBuilding(AActor::StaticClass(), 500.0);
	FDeepLevelBuildingPlacementDefinition VarietyB = DeepLevelBuildingLinePlannerTests::MakeBuilding(APawn::StaticClass(), 500.0);
	VarietyA.PlacementVolume.Extent.Y = 1.0;
	VarietyB.PlacementVolume.Extent.Y = 1.0;
	VarietyCatalog->Buildings.Add(VarietyA);
	VarietyCatalog->Buildings.Add(VarietyB);
	FDeepLevelBuildingLinePath VarietyPath;
	TestTrue(TEXT("Irregular closed variety path builds"), DeepLevelBuildingLinePlannerTests::MakeClosedPath(
		{
			FSplinePoint(0, FVector::ZeroVector, ESplinePointType::Linear),
			FSplinePoint(1, FVector(1500.0, 0.0, 0.0), ESplinePointType::Linear),
			FSplinePoint(2, FVector(1030.0, 1944.5, 0.0), ESplinePointType::Linear)
		},
		VarietyPath));
	FDeepLevelBuildingLinePlan VarietyPlan;
	TestTrue(
		TEXT("Closed cyclic variety plan succeeds"),
		FDeepLevelBuildingLinePlanner::BuildPlan(
			*VarietyCatalog,
			VarietyPath,
			31,
			1.0,
			1.0,
			EDeepLevelCornerPlacementFlags::None,
			VarietyPlan,
			Error));
	TestTrue(TEXT("Closed cyclic variety plan contains multiple placements"), VarietyPlan.Placements.Num() > 1);
	if (VarietyPlan.Placements.Num() > 1)
	{
		TestNotEqual(
			TEXT("Closed cyclic variety treats the last and first placements as neighbors"),
			VarietyPlan.Placements.Last().BuildingClass.ToString(),
			VarietyPlan.Placements[0].BuildingClass.ToString());
	}

	for (const ESplinePointType::Type Type : {ESplinePointType::Curve, ESplinePointType::CurveClamped})
	{
		FDeepLevelBuildingLinePath CurvedPath;
		TestTrue(TEXT("Curved closed packing path builds"), DeepLevelBuildingLinePlannerTests::MakeClosedPath(
			{
				FSplinePoint(0, FVector(0.0, 0.0, 0.0), Type),
				FSplinePoint(1, FVector(4000.0, 0.0, 0.0), Type),
				FSplinePoint(2, FVector(4000.0, 4000.0, 0.0), Type),
				FSplinePoint(3, FVector(0.0, 4000.0, 0.0), Type)
			},
			CurvedPath));
		FDeepLevelBuildingLinePlan CurvedPlan;
		TestTrue(
			TEXT("Curved closed packing succeeds"),
			FDeepLevelBuildingLinePlanner::BuildPlan(
				*VarietyCatalog,
				CurvedPath,
				47,
				1.0,
				1.0,
				EDeepLevelCornerPlacementFlags::None,
				CurvedPlan,
				Error));
		TestFalse(TEXT("Curved closed packing produces buildings"), CurvedPlan.Placements.IsEmpty());
	}

	UDeepLevelBuildingPlacementCatalog* SingleBuildingCatalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	SingleBuildingCatalog->Buildings.Add(SpanBuilding);
	FDeepLevelBuildingLinePlan SingleBuildingPlan;
	TestTrue(
		TEXT("Single-building catalog continues around a closed loop"),
		FDeepLevelBuildingLinePlanner::BuildPlan(
			*SingleBuildingCatalog,
			Path,
			19,
			1.0,
			1.0,
			EDeepLevelCornerPlacementFlags::None,
			SingleBuildingPlan,
			Error));
	TestFalse(TEXT("Single-building closed loop produces placements"), SingleBuildingPlan.Placements.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLinePresetAndCooldownTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.PresetAndCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLinePresetAndCooldownTest::RunTest(const FString& Parameters)
{
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	Catalog->Buildings.Add(DeepLevelBuildingLinePlannerTests::MakeBuilding(AActor::StaticClass(), 100.0, 0.0));
	Catalog->Buildings.Add(DeepLevelBuildingLinePlannerTests::MakeBuilding(APawn::StaticClass(), 100.0, 0.0));
	Catalog->Buildings.Add(DeepLevelBuildingLinePlannerTests::MakeBuilding(ACharacter::StaticClass(), 100.0, 1.0));

	FDeepLevelBuildingSequencePreset& Preset = Catalog->Presets.Emplace_GetRef();
	Preset.Name = TEXT("AB");
	Preset.SelectionWeight = 100.0;
	Preset.Buildings = {AActor::StaticClass(), APawn::StaticClass()};

	FDeepLevelBuildingLinePlan Plan;
	FDeepLevelBuildingLinePath Path;
	FText Error;
	TestTrue(TEXT("Straight path builds"), DeepLevelBuildingLinePlannerTests::MakeStraightPath(500.0, Path));
	TestTrue(TEXT("Plan succeeds"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 9, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, Plan, Error));
	TestEqual(TEXT("Five buildings fit"), Plan.Placements.Num(), 5);
	TestTrue(TEXT("Preset order appears"), Plan.Placements.ContainsByPredicate([](const FDeepLevelBuildingLinePlacement& Placement)
	{
		return Placement.BuildingClass == AActor::StaticClass();
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLineCenteringAndValidationTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.CenteringAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLineCenteringAndValidationTest::RunTest(const FString& Parameters)
{
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	Catalog->Buildings.Add(DeepLevelBuildingLinePlannerTests::MakeBuilding(AActor::StaticClass(), 300.0));
	Catalog->Buildings.Add(DeepLevelBuildingLinePlannerTests::MakeBuilding(APawn::StaticClass(), 300.0));

	FDeepLevelBuildingLinePlan Plan;
	FDeepLevelBuildingLinePath Path;
	FText Error;
	TestTrue(TEXT("Straight path builds"), DeepLevelBuildingLinePlannerTests::MakeStraightPath(1000.0, Path));
	TestTrue(TEXT("Plan succeeds"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 3, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, Plan, Error));
	TestEqual(TEXT("Used length"), Plan.UsedLength, 900.0);
	TestEqual(TEXT("Remainder split equally"), Plan.StartOffset, 50.0);
	TestEqual(TEXT("First placement center follows centered remainder"), Plan.Placements[0].Distance, 200.0);

	Catalog->Buildings[0].PlacementVolume.Extent.X = 0.0;
	TestFalse(TEXT("Zero extent rejected"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 3, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, Plan, Error));
	TestFalse(TEXT("Validation error provided"), Error.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLineSingleBuildingContinuityTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.SingleBuildingContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLineSingleBuildingContinuityTest::RunTest(const FString& Parameters)
{
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	Catalog->Buildings.Add(DeepLevelBuildingLinePlannerTests::MakeBuilding(AActor::StaticClass(), 100.0));
	FDeepLevelBuildingLinePath Path;
	FDeepLevelBuildingLinePlan Plan;
	FText Error;
	TestTrue(TEXT("Straight path builds"), DeepLevelBuildingLinePlannerTests::MakeStraightPath(1000.0, Path));
	TestTrue(TEXT("Single-building plan succeeds"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 7, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, Plan, Error));
	TestEqual(TEXT("Only available building fills the line"), Plan.Placements.Num(), 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLineGlobalPackingTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.GlobalPacking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLineGlobalPackingTest::RunTest(const FString& Parameters)
{
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	Catalog->Buildings.Add(DeepLevelBuildingLinePlannerTests::MakeBuilding(AActor::StaticClass(), 700.0, 100.0));
	Catalog->Buildings.Add(DeepLevelBuildingLinePlannerTests::MakeBuilding(APawn::StaticClass(), 400.0, 1.0));

	FDeepLevelBuildingLinePath Path;
	FDeepLevelBuildingLinePlan Plan;
	FText Error;
	TestTrue(TEXT("Packing path builds"), DeepLevelBuildingLinePlannerTests::MakeStraightPath(1200.0, Path));
	TestTrue(TEXT("Global packing succeeds"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 17, 0.0, 1.0, EDeepLevelCornerPlacementFlags::All, Plan, Error));
	TestEqual(TEXT("Global packing fills the full frontage"), Plan.UsedLength, 1200.0);
	TestEqual(TEXT("Optimal packing uses three modules"), Plan.Placements.Num(), 3);
	for (const FDeepLevelBuildingLinePlacement& Placement : Plan.Placements)
	{
		TestEqual(
			TEXT("Coverage outranks a locally heavier incomplete module"),
			Placement.BuildingClass.ToString(),
			TSoftClassPtr<AActor>(APawn::StaticClass()).ToString());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLineSegmentCoverageTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.SegmentCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLineSegmentCoverageTest::RunTest(const FString& Parameters)
{
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	Catalog->Buildings.Add(DeepLevelBuildingLinePlannerTests::MakeBuilding(AActor::StaticClass(), 900.0));
	Catalog->Buildings.Add(DeepLevelBuildingLinePlannerTests::MakeBuilding(APawn::StaticClass(), 650.0));
	Catalog->Buildings.Add(DeepLevelBuildingLinePlannerTests::MakeBuilding(ACharacter::StaticClass(), 400.0));
	for (FDeepLevelBuildingPlacementDefinition& Definition : Catalog->Buildings)
	{
		Definition.PlacementVolume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Neutral;
		Definition.PlacementVolume.Exposure.NegativeX = EDeepLevelStreetExposureRule::Neutral;
	}

	FDeepLevelBuildingLinePath Path;
	TestTrue(TEXT("Long multi-corner path builds"), DeepLevelBuildingLinePlannerTests::MakePath(
		{
			FSplinePoint(0, FVector::ZeroVector, ESplinePointType::Linear),
			FSplinePoint(1, FVector(5000.0, 0.0, 0.0), ESplinePointType::Linear),
			FSplinePoint(2, FVector(5000.0, 5000.0, 0.0), ESplinePointType::Linear),
			FSplinePoint(3, FVector(10000.0, 5000.0, 0.0), ESplinePointType::Linear),
			FSplinePoint(4, FVector(10000.0, 10000.0, 0.0), ESplinePointType::Linear)
		},
		Path));

	FDeepLevelBuildingLinePlan Plan;
	FText Error;
	TestTrue(TEXT("Segment packing succeeds"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 71, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, Plan, Error));
	TestTrue(
		TEXT("No route span retains a building-sized gap"),
		DeepLevelBuildingLinePlannerTests::GetLargestPathGap(Plan, *Catalog, Path.GetLength()) < 400.0);
	int32 CornerCount = 0;
	for (const FDeepLevelBuildingLinePlacement& Placement : Plan.Placements)
	{
		CornerCount += Placement.bCornerPlacement ? 1 : 0;
	}
	TestEqual(TEXT("Every hard corner owns one reservation"), CornerCount, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLineStateGrowthSafetyTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.StateGrowthSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLineStateGrowthSafetyTest::RunTest(const FString& Parameters)
{
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	for (UClass* BuildingClass : {AActor::StaticClass(), APawn::StaticClass(), ACharacter::StaticClass()})
	{
		FDeepLevelBuildingPlacementDefinition Definition = DeepLevelBuildingLinePlannerTests::MakeBuilding(BuildingClass, 100.0);
		Definition.PlacementVolume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Preferred;
		Definition.PlacementVolume.Exposure.NegativeX = EDeepLevelStreetExposureRule::Preferred;
		Definition.PlacementVolume.Exposure.PositiveY = EDeepLevelStreetExposureRule::Preferred;
		Definition.PlacementVolume.Exposure.NegativeY = EDeepLevelStreetExposureRule::Preferred;
		Catalog->Buildings.Add(Definition);
	}
	for (int32 PresetIndex = 0; PresetIndex < 8; ++PresetIndex)
	{
		FDeepLevelBuildingSequencePreset& Preset = Catalog->Presets.Emplace_GetRef();
		Preset.Name = *FString::Printf(TEXT("AllocatorGrowth_%d"), PresetIndex);
		Preset.Buildings.Add(Catalog->Buildings[PresetIndex % Catalog->Buildings.Num()].BuildingClass);
		Preset.Buildings.Add(Catalog->Buildings[(PresetIndex + 1) % Catalog->Buildings.Num()].BuildingClass);
	}

	FDeepLevelBuildingLinePath Path;
	TestTrue(TEXT("Allocator growth path builds"), DeepLevelBuildingLinePlannerTests::MakeStraightPath(5000.0, Path));
	FDeepLevelBuildingLinePlan FirstPlan;
	FDeepLevelBuildingLinePlan SecondPlan;
	FText Error;
	TestTrue(TEXT("Large state graph packs without invalidating source nodes"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 101, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, FirstPlan, Error));
	TestTrue(TEXT("Repeated large state graph remains deterministic"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 101, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, SecondPlan, Error));
	TestEqual(TEXT("State growth preserves deterministic placement count"), FirstPlan.Placements.Num(), SecondPlan.Placements.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLineRotationVarietyTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.RotationVariety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLineRotationVarietyTest::RunTest(const FString& Parameters)
{
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	FDeepLevelBuildingPlacementDefinition Definition = DeepLevelBuildingLinePlannerTests::MakeBuilding(AActor::StaticClass(), 100.0);
	Definition.PlacementVolume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Preferred;
	Definition.PlacementVolume.Exposure.NegativeX = EDeepLevelStreetExposureRule::Preferred;
	Definition.PlacementVolume.Exposure.PositiveY = EDeepLevelStreetExposureRule::Forbidden;
	Definition.PlacementVolume.Exposure.NegativeY = EDeepLevelStreetExposureRule::Forbidden;
	Catalog->Buildings.Add(Definition);

	FDeepLevelBuildingLinePath Path;
	FDeepLevelBuildingLinePlan Plan;
	FText Error;
	TestTrue(TEXT("Straight path builds"), DeepLevelBuildingLinePlannerTests::MakeStraightPath(1200.0, Path));
	TestTrue(TEXT("Plan succeeds"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 31, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, Plan, Error));
	TSet<EDeepLevelBuildingVolumeFace> UsedFaces;
	for (const FDeepLevelBuildingLinePlacement& Placement : Plan.Placements)
	{
		UsedFaces.Add(Placement.StreetFace);
	}
	TestEqual(TEXT("Both required rotations are used"), UsedFaces.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLineCornerPreferenceTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.CornerPreference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLineCornerPreferenceTest::RunTest(const FString& Parameters)
{
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	FDeepLevelBuildingPlacementDefinition CornerBuilding = DeepLevelBuildingLinePlannerTests::MakeBuilding(AActor::StaticClass(), 200.0);
	CornerBuilding.PlacementVolume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Required;
	CornerBuilding.PlacementVolume.Exposure.NegativeY = EDeepLevelStreetExposureRule::Required;
	Catalog->Buildings.Add(CornerBuilding);
	Catalog->Buildings.Add(DeepLevelBuildingLinePlannerTests::MakeBuilding(APawn::StaticClass(), 200.0));

	FDeepLevelBuildingLinePath Path;
	TestTrue(TEXT("Corner path builds"), DeepLevelBuildingLinePlannerTests::MakePath(
		{
			FSplinePoint(0, FVector::ZeroVector, ESplinePointType::Linear),
			FSplinePoint(1, FVector(1000.0, 0.0, 0.0), ESplinePointType::Linear),
			FSplinePoint(2, FVector(1000.0, 1000.0, 0.0), ESplinePointType::Linear)
		},
		Path));
	FDeepLevelBuildingLinePlan Plan;
	FText Error;
	TestTrue(TEXT("Corner plan succeeds"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 11, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, Plan, Error));
	const FDeepLevelBuildingLinePlacement* ClosestToCorner = nullptr;
	double ClosestDistance = TNumericLimits<double>::Max();
	for (const FDeepLevelBuildingLinePlacement& Placement : Plan.Placements)
	{
		const double Distance = FMath::Abs(Placement.Distance - 1000.0);
		if (Distance < ClosestDistance)
		{
			ClosestDistance = Distance;
			ClosestToCorner = &Placement;
		}
	}
	TestNotNull(TEXT("A building is placed near the corner"), ClosestToCorner);
	if (ClosestToCorner)
	{
		TestEqual(TEXT("Dual-exposure building is preferred at the corner"), ClosestToCorner->BuildingClass.ToString(), TSoftClassPtr<AActor>(AActor::StaticClass()).ToString());
	}
	for (int32 Index = 1; Index < Plan.Placements.Num(); ++Index)
	{
		TestFalse(
			TEXT("Adjacent street facades do not cross"),
			DeepLevelBuildingLinePlannerTests::FacadesIntersect(Plan.Placements[Index - 1], 100.0, Plan.Placements[Index], 100.0));
	}
	for (int32 AIndex = 0; AIndex < Plan.Placements.Num(); ++AIndex)
	{
		const FDeepLevelBuildingPlacementDefinition* ADefinition = Catalog->Buildings.FindByPredicate([&Plan, AIndex](const FDeepLevelBuildingPlacementDefinition& Definition)
		{
			return Definition.BuildingClass == Plan.Placements[AIndex].BuildingClass;
		});
		for (int32 BIndex = AIndex + 1; ADefinition && BIndex < Plan.Placements.Num(); ++BIndex)
		{
			const FDeepLevelBuildingPlacementDefinition* BDefinition = Catalog->Buildings.FindByPredicate([&Plan, BIndex](const FDeepLevelBuildingPlacementDefinition& Definition)
			{
				return Definition.BuildingClass == Plan.Placements[BIndex].BuildingClass;
			});
			if (BDefinition)
			{
				TestFalse(
					TEXT("Placement footprints do not overlap at the corner"),
					DeepLevelBuildingLinePlannerTests::FootprintsOverlap(
						DeepLevelBuildingLinePlannerTests::MakeFootprint(Plan.Placements[AIndex], *ADefinition),
						DeepLevelBuildingLinePlannerTests::MakeFootprint(Plan.Placements[BIndex], *BDefinition)));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLineCornerPlacementPolicyTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.CornerPlacementPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLineCornerPlacementPolicyTest::RunTest(const FString& Parameters)
{
	const int32 DefaultCornerMask = static_cast<int32>(EDeepLevelCornerPlacementFlags::Inner);
	const UClass* AuthoringBlueprintClass = LoadClass<ADeepLevelPCGBuildingLineActor>(
		nullptr,
		TEXT("/DeepLevelDesignPCG/Building/BP_DeepLevelBuildingLine.BP_DeepLevelBuildingLine_C"));
	TestNotNull(TEXT("Building line authoring Blueprint loads"), AuthoringBlueprintClass);
	if (AuthoringBlueprintClass)
	{
		const ADeepLevelPCGBuildingLineActor* AuthoringBlueprintCDO = AuthoringBlueprintClass->GetDefaultObject<ADeepLevelPCGBuildingLineActor>();
		TestNotNull(TEXT("Building line Blueprint uses the authored spline component"), AuthoringBlueprintCDO->BuildingLine.Get());
		if (AuthoringBlueprintCDO->BuildingLine)
		{
			TestTrue(
				TEXT("Building line Blueprint exposes per-spline corner placement"),
				AuthoringBlueprintCDO->BuildingLine->IsA<UDeepLevelBuildingLineSplineComponent>());
		}
	}
	UWorld* TestWorld = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		TEXT("DeepLevelBuildingLineCornerPolicyWorld"),
		nullptr,
		false);
	TestNotNull(TEXT("Building line policy test world is created"), TestWorld);
	ADeepLevelPCGBuildingLineActor* BuildingLineActor = TestWorld
		? TestWorld->SpawnActor<ADeepLevelPCGBuildingLineActor>()
		: nullptr;
	TestNotNull(TEXT("Building line authoring actor is spawned"), BuildingLineActor);
	UDeepLevelBuildingLineSplineComponent* AuthoredSpline = BuildingLineActor ? BuildingLineActor->BuildingLine : nullptr;
	TestNotNull(TEXT("Building line actor owns the authored spline component"), AuthoredSpline);
	if (!AuthoredSpline)
	{
		if (TestWorld)
		{
			TestWorld->DestroyWorld(false);
		}
		return false;
	}
	TestEqual(
		TEXT("Building line component defaults to inner corners"),
		AuthoredSpline->CornerPlacementMask,
		static_cast<int32>(EDeepLevelCornerPlacementFlags::Inner));
	const FIntProperty* ComponentCornerPlacementProperty = FindFProperty<FIntProperty>(
		UDeepLevelBuildingLineSplineComponent::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UDeepLevelBuildingLineSplineComponent, CornerPlacementMask));
	TestNotNull(TEXT("Building line component exposes corner placement"), ComponentCornerPlacementProperty);
	if (ComponentCornerPlacementProperty)
	{
		TestEqual(
			TEXT("Building line settings use a short component-owned Details category"),
			ComponentCornerPlacementProperty->GetMetaData(TEXT("Category")),
			FString(TEXT("Deep Level Design PCG|BuildingLine")));
	}
	const FObjectProperty* BuildingLineProperty = FindFProperty<FObjectProperty>(
		ADeepLevelPCGBuildingLineActor::StaticClass(),
		GET_MEMBER_NAME_CHECKED(ADeepLevelPCGBuildingLineActor, BuildingLine));
	TestNotNull(TEXT("Building line actor exposes its spline component"), BuildingLineProperty);
	if (BuildingLineProperty)
	{
		TestEqual(
			TEXT("Building line pointer stays in the owner component bucket"),
			BuildingLineProperty->GetMetaData(TEXT("Category")),
			FString(TEXT("Deep Level Design PCG|PCG|Components")));
	}
	AuthoredSpline->CornerPlacementMask = static_cast<int32>(EDeepLevelCornerPlacementFlags::Outer);
	AuthoredSpline->ClearSplinePoints(false);
	AuthoredSpline->AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::Local, false);
	AuthoredSpline->AddSplinePoint(FVector(1000.0, 0.0, 0.0), ESplineCoordinateSpace::Local, true);
	FPCGGetDataFunctionRegistryParams DataParams;
	DataParams.DataTypeFilter = EPCGDataType::Spline;
	FPCGGetDataFunctionRegistryOutput DataOutput;
	TestEqual(
		TEXT("Building line component is handled by the registered DeepLevel PCG data producer"),
		FPCGModule::ConstGetDataFunctionRegistry().GetDataFromComponent(
			nullptr,
			DataParams,
			AuthoredSpline,
			DataOutput),
		1);
	TestEqual(TEXT("Building line component produces one spline input"), DataOutput.Collection.TaggedData.Num(), 1);
	const UPCGSplineData* AuthoredSplineData = DataOutput.Collection.TaggedData.IsEmpty()
		? nullptr
		: Cast<UPCGSplineData>(DataOutput.Collection.TaggedData[0].Data);
	TestNotNull(TEXT("Building line component produces spline data"), AuthoredSplineData);
	if (AuthoredSplineData)
	{
		EDeepLevelCornerPlacementFlags AuthoredFlags = EDeepLevelCornerPlacementFlags::None;
		FText ProducerError;
		TestTrue(
			TEXT("Produced spline metadata resolves without manual graph attributes"),
			FDeepLevelBuildingLineCornerPolicy::Resolve(
				*AuthoredSplineData,
				DefaultCornerMask,
				AuthoredFlags,
				ProducerError));
		TestEqual(
			TEXT("Produced spline metadata carries the component policy"),
			static_cast<uint8>(AuthoredFlags),
			static_cast<uint8>(EDeepLevelCornerPlacementFlags::Outer));
	}
	TestWorld->DestroyWorld(false);

	UPCGSplineData* DefaultSpline = NewObject<UPCGSplineData>();
	EDeepLevelCornerPlacementFlags ResolvedFlags = EDeepLevelCornerPlacementFlags::None;
	FText Error;
	TestTrue(
		TEXT("Spline without metadata uses the node mask"),
		FDeepLevelBuildingLineCornerPolicy::Resolve(*DefaultSpline, DefaultCornerMask, ResolvedFlags, Error));
	TestEqual(
		TEXT("Node mask resolves to inner corners"),
		static_cast<uint8>(ResolvedFlags),
		static_cast<uint8>(EDeepLevelCornerPlacementFlags::Inner));

	UPCGSplineData* OverrideSpline = NewObject<UPCGSplineData>();
	OverrideSpline->MutableMetadata()->CreateAttribute<int32>(
		DeepLevelBuildingLineCornerPlacement::MetadataAttributeName,
		static_cast<int32>(EDeepLevelCornerPlacementFlags::Outer),
		false,
		false);
	TestTrue(
		TEXT("Spline metadata resolves a valid override"),
		FDeepLevelBuildingLineCornerPolicy::Resolve(*OverrideSpline, DefaultCornerMask, ResolvedFlags, Error));
	TestEqual(
		TEXT("Spline metadata overrides the node mask"),
		static_cast<uint8>(ResolvedFlags),
		static_cast<uint8>(EDeepLevelCornerPlacementFlags::Outer));

	UPCGSplineData* InvalidSpline = NewObject<UPCGSplineData>();
	InvalidSpline->MutableMetadata()->CreateAttribute<int32>(
		DeepLevelBuildingLineCornerPlacement::MetadataAttributeName,
		4,
		false,
		false);
	TestFalse(
		TEXT("Spline metadata rejects unsupported mask bits"),
		FDeepLevelBuildingLineCornerPolicy::Resolve(*InvalidSpline, DefaultCornerMask, ResolvedFlags, Error));
	TestFalse(
		TEXT("Node setting rejects unsupported mask bits"),
		FDeepLevelBuildingLineCornerPolicy::Resolve(*DefaultSpline, 4, ResolvedFlags, Error));

	UPCGSplineData* WrongTypeSpline = NewObject<UPCGSplineData>();
	WrongTypeSpline->MutableMetadata()->CreateAttribute<FString>(
		DeepLevelBuildingLineCornerPlacement::MetadataAttributeName,
		TEXT("Outer"),
		false,
		false);
	TestFalse(
		TEXT("Spline metadata rejects a non-integer corner policy"),
		FDeepLevelBuildingLineCornerPolicy::Resolve(*WrongTypeSpline, DefaultCornerMask, ResolvedFlags, Error));

	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	FDeepLevelBuildingPlacementDefinition Building = DeepLevelBuildingLinePlannerTests::MakeBuilding(AActor::StaticClass(), 200.0);
	Building.PlacementVolume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Neutral;
	Building.PlacementVolume.Exposure.NegativeX = EDeepLevelStreetExposureRule::Neutral;
	Catalog->Buildings.Add(Building);

	FDeepLevelBuildingLinePath OuterPath;
	TestTrue(TEXT("Outer corner path builds"), DeepLevelBuildingLinePlannerTests::MakePath(
		{
			FSplinePoint(0, FVector::ZeroVector, ESplinePointType::Linear),
			FSplinePoint(1, FVector(1200.0, 0.0, 0.0), ESplinePointType::Linear),
			FSplinePoint(2, FVector(1200.0, -1200.0, 0.0), ESplinePointType::Linear)
		},
		OuterPath));
	FDeepLevelBuildingLinePlan DisabledPlan;
	TestTrue(
		TEXT("Outer corner plan succeeds while corner placement is disabled"),
		FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, OuterPath, 13, 1.0, 1.0, EDeepLevelCornerPlacementFlags::Inner, DisabledPlan, Error));
	TestFalse(
		TEXT("Disabled outer corner has no reserved corner building"),
		DisabledPlan.Placements.ContainsByPredicate([](const FDeepLevelBuildingLinePlacement& Placement)
		{
			return Placement.bCornerPlacement;
		}));

	FDeepLevelBuildingLinePlan EnabledPlan;
	TestTrue(
		TEXT("Outer corner plan succeeds while corner placement is enabled"),
		FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, OuterPath, 13, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, EnabledPlan, Error));
	TestTrue(
		TEXT("Enabled outer corner receives a reserved corner building"),
		EnabledPlan.Placements.ContainsByPredicate([](const FDeepLevelBuildingLinePlacement& Placement)
		{
			return Placement.bCornerPlacement;
		}));

	UDeepLevelBuildingPlacementCatalog* StraightOnlyCatalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	StraightOnlyCatalog->Buildings.Add(
		DeepLevelBuildingLinePlannerTests::MakeBuilding(AActor::StaticClass(), 200.0));
	FDeepLevelBuildingLinePlan SkippedCornerPlan;
	TestTrue(
		TEXT("Roadside policy skips an unplaceable corner and continues"),
		FDeepLevelBuildingLinePlanner::BuildPlan(
			*StraightOnlyCatalog, OuterPath, 13, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All,
			SkippedCornerPlan, Error, true, true));
	TestEqual(TEXT("Skipped corner is reported"), SkippedCornerPlan.SkippedCornerIndices.Num(), 1);
	if (!SkippedCornerPlan.SkippedCornerIndices.IsEmpty())
	{
		TestEqual(TEXT("Skipped corner keeps its one-based index"), SkippedCornerPlan.SkippedCornerIndices[0], 1);
	}

	FDeepLevelBuildingLinePath InnerPath;
	TestTrue(TEXT("Inner corner path builds"), DeepLevelBuildingLinePlannerTests::MakePath(
		{
			FSplinePoint(0, FVector::ZeroVector, ESplinePointType::Linear),
			FSplinePoint(1, FVector(1200.0, 0.0, 0.0), ESplinePointType::Linear),
			FSplinePoint(2, FVector(1200.0, 1200.0, 0.0), ESplinePointType::Linear)
		},
		InnerPath));
	FDeepLevelBuildingLinePlan InnerPlan;
	TestTrue(
		TEXT("Inner corner plan succeeds with outer placement disabled"),
		FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, InnerPath, 13, 1.0, 1.0, EDeepLevelCornerPlacementFlags::Inner, InnerPlan, Error));
	TestTrue(
		TEXT("Inner corner still receives a reserved corner building"),
		InnerPlan.Placements.ContainsByPredicate([](const FDeepLevelBuildingLinePlacement& Placement)
		{
			return Placement.bCornerPlacement;
		}));

	FDeepLevelBuildingLinePlan OuterOnlyPlan;
	TestTrue(
		TEXT("Inner corner plan succeeds with only outer placement enabled"),
		FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, InnerPath, 13, 1.0, 1.0, EDeepLevelCornerPlacementFlags::Outer, OuterOnlyPlan, Error));
	TestFalse(
		TEXT("Outer-only mask leaves the inner corner unreserved"),
		OuterOnlyPlan.Placements.ContainsByPredicate([](const FDeepLevelBuildingLinePlacement& Placement)
		{
			return Placement.bCornerPlacement;
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLineExposureConstraintsTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.ExposureConstraints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLineExposureConstraintsTest::RunTest(const FString& Parameters)
{
	FDeepLevelBuildingPlacementDefinition CornerOnlyBuilding = DeepLevelBuildingLinePlannerTests::MakeBuilding(AActor::StaticClass(), 200.0, 100.0);
	CornerOnlyBuilding.PlacementVolume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Required;
	FDeepLevelBuildingPlacementDefinition RegularBuilding = DeepLevelBuildingLinePlannerTests::MakeBuilding(APawn::StaticClass(), 200.0, 1.0);
	RegularBuilding.PlacementVolume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Neutral;

	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	Catalog->Buildings.Add(CornerOnlyBuilding);
	Catalog->Buildings.Add(RegularBuilding);
	FDeepLevelBuildingLinePath StraightPath;
	TestTrue(TEXT("Exposure straight path builds"), DeepLevelBuildingLinePlannerTests::MakeStraightPath(1200.0, StraightPath));
	FDeepLevelBuildingLinePlan StraightPlan;
	FText Error;
	TestTrue(TEXT("Exposure straight plan succeeds"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, StraightPath, 41, 0.0, 1.0, EDeepLevelCornerPlacementFlags::All, StraightPlan, Error));
	for (const FDeepLevelBuildingLinePlacement& Placement : StraightPlan.Placements)
	{
		TestEqual(
			TEXT("A second required face prevents use on a straight span"),
			Placement.BuildingClass.ToString(),
			TSoftClassPtr<AActor>(APawn::StaticClass()).ToString());
	}

	FDeepLevelBuildingLinePath CornerPath;
	TestTrue(TEXT("Exposure corner path builds"), DeepLevelBuildingLinePlannerTests::MakePath(
		{
			FSplinePoint(0, FVector::ZeroVector, ESplinePointType::Linear),
			FSplinePoint(1, FVector(1200.0, 0.0, 0.0), ESplinePointType::Linear),
			FSplinePoint(2, FVector(1200.0, 1200.0, 0.0), ESplinePointType::Linear)
		},
		CornerPath));
	FDeepLevelBuildingLinePlan CornerPlan;
	TestTrue(TEXT("Exposure corner plan succeeds"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, CornerPath, 41, 0.0, 1.0, EDeepLevelCornerPlacementFlags::All, CornerPlan, Error));
	const FDeepLevelBuildingLinePlacement* CornerPlacement = CornerPlan.Placements.FindByPredicate([](const FDeepLevelBuildingLinePlacement& Placement)
	{
		return Placement.bCornerPlacement;
	});
	TestNotNull(TEXT("Hard corner receives a constrained placement"), CornerPlacement);
	if (CornerPlacement)
	{
		TestEqual(
			TEXT("Both required faces select the compatible corner building"),
			CornerPlacement->BuildingClass.ToString(),
			TSoftClassPtr<AActor>(AActor::StaticClass()).ToString());
	}

	UDeepLevelBuildingPlacementCatalog* ForbiddenCatalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	FDeepLevelBuildingPlacementDefinition ForbiddenCornerBuilding = CornerOnlyBuilding;
	ForbiddenCornerBuilding.PlacementVolume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Forbidden;
	ForbiddenCornerBuilding.SelectionWeight = 1000.0;
	ForbiddenCatalog->Buildings.Add(ForbiddenCornerBuilding);
	ForbiddenCatalog->Buildings.Add(RegularBuilding);
	FDeepLevelBuildingLinePlan ForbiddenPlan;
	TestTrue(TEXT("Forbidden corner plan succeeds with a valid alternative"), FDeepLevelBuildingLinePlanner::BuildPlan(*ForbiddenCatalog, CornerPath, 41, 0.0, 1.0, EDeepLevelCornerPlacementFlags::All, ForbiddenPlan, Error));
	const FDeepLevelBuildingLinePlacement* ValidCornerPlacement = ForbiddenPlan.Placements.FindByPredicate([](const FDeepLevelBuildingLinePlacement& Placement)
	{
		return Placement.bCornerPlacement;
	});
	TestNotNull(TEXT("Forbidden corner still receives an alternative"), ValidCornerPlacement);
	if (ValidCornerPlacement)
	{
		TestEqual(
			TEXT("A forbidden exposed side rejects the otherwise heavier rotation"),
			ValidCornerPlacement->BuildingClass.ToString(),
			TSoftClassPtr<AActor>(APawn::StaticClass()).ToString());
	}

	UDeepLevelBuildingPlacementCatalog* SecondaryRequiredCatalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	FDeepLevelBuildingPlacementDefinition SecondaryRequiredBuilding = DeepLevelBuildingLinePlannerTests::MakeBuilding(ACharacter::StaticClass(), 200.0, 100.0);
	SecondaryRequiredBuilding.PlacementVolume.Exposure.NegativeY = EDeepLevelStreetExposureRule::Preferred;
	SecondaryRequiredBuilding.PlacementVolume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Required;
	SecondaryRequiredCatalog->Buildings.Add(SecondaryRequiredBuilding);
	SecondaryRequiredCatalog->Buildings.Add(RegularBuilding);
	FDeepLevelBuildingLinePlan SecondaryRequiredPlan;
	TestTrue(
		TEXT("A preferred primary face can expose a required side at a corner"),
		FDeepLevelBuildingLinePlanner::BuildPlan(*SecondaryRequiredCatalog, CornerPath, 41, 0.0, 1.0, EDeepLevelCornerPlacementFlags::All, SecondaryRequiredPlan, Error));
	const FDeepLevelBuildingLinePlacement* SecondaryRequiredCorner = SecondaryRequiredPlan.Placements.FindByPredicate([](const FDeepLevelBuildingLinePlacement& Placement)
	{
		return Placement.bCornerPlacement;
	});
	TestNotNull(TEXT("Secondary required corner receives a placement"), SecondaryRequiredCorner);
	if (SecondaryRequiredCorner)
	{
		TestEqual(
			TEXT("Corner rotation exposes the required side through the preferred primary face"),
			SecondaryRequiredCorner->BuildingClass.ToString(),
			TSoftClassPtr<AActor>(ACharacter::StaticClass()).ToString());
		TestEqual(
			TEXT("Preferred primary face is resolved for the compatible corner rotation"),
			SecondaryRequiredCorner->StreetFace,
			EDeepLevelBuildingVolumeFace::NegativeY);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLineCornerBuildingVarietyTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.CornerBuildingVariety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLineCornerBuildingVarietyTest::RunTest(const FString& Parameters)
{
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	FDeepLevelBuildingPlacementDefinition PrimaryCornerBuilding = DeepLevelBuildingLinePlannerTests::MakeBuilding(AActor::StaticClass(), 200.0);
	PrimaryCornerBuilding.PlacementVolume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Required;
	PrimaryCornerBuilding.PlacementVolume.Exposure.NegativeY = EDeepLevelStreetExposureRule::Required;
	Catalog->Buildings.Add(PrimaryCornerBuilding);
	FDeepLevelBuildingPlacementDefinition SecondaryCornerBuilding = DeepLevelBuildingLinePlannerTests::MakeBuilding(APawn::StaticClass(), 175.0);
	SecondaryCornerBuilding.PlacementVolume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Neutral;
	SecondaryCornerBuilding.PlacementVolume.Exposure.NegativeX = EDeepLevelStreetExposureRule::Neutral;
	Catalog->Buildings.Add(SecondaryCornerBuilding);
	FDeepLevelBuildingPlacementDefinition TertiaryCornerBuilding = DeepLevelBuildingLinePlannerTests::MakeBuilding(ACharacter::StaticClass(), 150.0);
	TertiaryCornerBuilding.PlacementVolume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Neutral;
	TertiaryCornerBuilding.PlacementVolume.Exposure.NegativeX = EDeepLevelStreetExposureRule::Neutral;
	Catalog->Buildings.Add(TertiaryCornerBuilding);

	FDeepLevelBuildingLinePath Path;
	TestTrue(TEXT("Multi-corner path builds"), DeepLevelBuildingLinePlannerTests::MakePath(
		{
			FSplinePoint(0, FVector::ZeroVector, ESplinePointType::Linear),
			FSplinePoint(1, FVector(2000.0, 0.0, 0.0), ESplinePointType::Linear),
			FSplinePoint(2, FVector(2000.0, 2000.0, 0.0), ESplinePointType::Linear),
			FSplinePoint(3, FVector(4000.0, 2000.0, 0.0), ESplinePointType::Linear),
			FSplinePoint(4, FVector(4000.0, 4000.0, 0.0), ESplinePointType::Linear),
			FSplinePoint(5, FVector(6000.0, 4000.0, 0.0), ESplinePointType::Linear)
		},
		Path));
	FDeepLevelBuildingLinePlan Plan;
	FText Error;
	TestTrue(TEXT("Multi-corner plan succeeds"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 23, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, Plan, Error));
	FDeepLevelBuildingLinePlan RepeatedPlan;
	TestTrue(TEXT("Repeated multi-corner plan succeeds"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 23, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, RepeatedPlan, Error));
	TestEqual(TEXT("Corner plan placement count is deterministic"), RepeatedPlan.Placements.Num(), Plan.Placements.Num());
	for (int32 Index = 0; Index < FMath::Min(Plan.Placements.Num(), RepeatedPlan.Placements.Num()); ++Index)
	{
		TestEqual(
			TEXT("Corner plan building sequence is deterministic"),
			RepeatedPlan.Placements[Index].BuildingClass.ToString(),
			Plan.Placements[Index].BuildingClass.ToString());
		TestEqual(
			TEXT("Corner ownership is deterministic"),
			RepeatedPlan.Placements[Index].bCornerPlacement,
			Plan.Placements[Index].bCornerPlacement);
	}

	TArray<const FDeepLevelBuildingLinePlacement*> CornerPlacements;
	for (const FDeepLevelBuildingLinePlacement& Placement : Plan.Placements)
	{
		if (Placement.bCornerPlacement)
		{
			CornerPlacements.Add(&Placement);
		}
	}
	TestTrue(TEXT("All spline corners receive a corner placement"), CornerPlacements.Num() >= 4);
	TSet<FString> FirstCornerCycle;
	for (int32 Index = 0; Index < FMath::Min(CornerPlacements.Num(), 3); ++Index)
	{
		FirstCornerCycle.Add(CornerPlacements[Index]->BuildingClass.ToString());
	}
	TestEqual(TEXT("Corner buildings do not repeat before available alternatives are used"), FirstCornerCycle.Num(), 3);
	TestTrue(
		TEXT("Multi-corner packing does not leave a building-sized path gap"),
		DeepLevelBuildingLinePlannerTests::GetLargestPathGap(Plan, *Catalog, Path.GetLength()) < 400.0);

	FDeepLevelBuildingLinePlan NoVarietyPlan;
	TestTrue(TEXT("Zero-variety corner plan succeeds"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 23, 0.0, 1.0, EDeepLevelCornerPlacementFlags::All, NoVarietyPlan, Error));
	for (const FDeepLevelBuildingLinePlacement& Placement : NoVarietyPlan.Placements)
	{
		if (Placement.bCornerPlacement)
		{
			TestEqual(
				TEXT("Zero variety preserves the strongest authored corner candidate"),
				Placement.BuildingClass.ToString(),
				TSoftClassPtr<AActor>(AActor::StaticClass()).ToString());
		}
	}

	UDeepLevelBuildingPlacementCatalog* CornerOnlyCatalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	CornerOnlyCatalog->Buildings.Add(PrimaryCornerBuilding);
	FDeepLevelBuildingLinePlan SingleBuildingPlan;
	TestFalse(
		TEXT("A corner-only building cannot silently populate straight spans"),
		FDeepLevelBuildingLinePlanner::BuildPlan(*CornerOnlyCatalog, Path, 23, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, SingleBuildingPlan, Error));
	TestFalse(TEXT("Corner-only catalog reports a generation error"), Error.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLineDeepCornerClearanceTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.DeepCornerClearance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLineDeepCornerClearanceTest::RunTest(const FString& Parameters)
{
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	FDeepLevelBuildingPlacementDefinition DeepBuilding = DeepLevelBuildingLinePlannerTests::MakeBuilding(AActor::StaticClass(), 200.0);
	DeepBuilding.PlacementVolume.Extent.Y = 500.0;
	DeepBuilding.PlacementVolume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Neutral;
	Catalog->Buildings.Add(DeepBuilding);

	FDeepLevelBuildingLinePath Path;
	TestTrue(TEXT("Deep corner path builds"), DeepLevelBuildingLinePlannerTests::MakePath(
		{
			FSplinePoint(0, FVector::ZeroVector, ESplinePointType::Linear),
			FSplinePoint(1, FVector(2000.0, 0.0, 0.0), ESplinePointType::Linear),
			FSplinePoint(2, FVector(2000.0, 3000.0, 0.0), ESplinePointType::Linear)
		},
		Path));
	FDeepLevelBuildingLinePlan Plan;
	FText Error;
	TestTrue(TEXT("Deep corner plan succeeds"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 19, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, Plan, Error));
	TestTrue(TEXT("Deep buildings remain usable around the corner"), Plan.Placements.Num() >= 4);
	for (int32 AIndex = 0; AIndex < Plan.Placements.Num(); ++AIndex)
	{
		for (int32 BIndex = AIndex + 1; BIndex < Plan.Placements.Num(); ++BIndex)
		{
			TestFalse(
				TEXT("Deep placement footprints remain separated"),
				DeepLevelBuildingLinePlannerTests::FootprintsOverlap(
					DeepLevelBuildingLinePlannerTests::MakeFootprint(Plan.Placements[AIndex], DeepBuilding),
					DeepLevelBuildingLinePlannerTests::MakeFootprint(Plan.Placements[BIndex], DeepBuilding)));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLineVisualScaleCooldownTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.VisualScaleCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLineVisualScaleCooldownTest::RunTest(const FString& Parameters)
{
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	Catalog->Buildings.Add(DeepLevelBuildingLinePlannerTests::MakeBuilding(AActor::StaticClass(), 400.0));
	Catalog->Buildings.Add(DeepLevelBuildingLinePlannerTests::MakeBuilding(APawn::StaticClass(), 100.0));
	Catalog->Buildings.Add(DeepLevelBuildingLinePlannerTests::MakeBuilding(ACharacter::StaticClass(), 100.0));

	FDeepLevelBuildingLinePath Path;
	FDeepLevelBuildingLinePlan Plan;
	FText Error;
	TestTrue(TEXT("Straight path builds"), DeepLevelBuildingLinePlannerTests::MakeStraightPath(6000.0, Path));
	TestTrue(TEXT("Plan succeeds"), FDeepLevelBuildingLinePlanner::BuildPlan(*Catalog, Path, 83, 1.0, 1.0, EDeepLevelCornerPlacementFlags::All, Plan, Error));

	double LastLargeDistance = -1.0;
	double MinimumLargeRepeatDistance = TNumericLimits<double>::Max();
	for (const FDeepLevelBuildingLinePlacement& Placement : Plan.Placements)
	{
		if (Placement.BuildingClass != AActor::StaticClass())
		{
			continue;
		}
		if (LastLargeDistance >= 0.0)
		{
			MinimumLargeRepeatDistance = FMath::Min(MinimumLargeRepeatDistance, Placement.Distance - LastLargeDistance);
		}
		LastLargeDistance = Placement.Distance;
	}
	TestTrue(TEXT("Large building repeats are physically separated"), MinimumLargeRepeatDistance >= 800.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLineNarrowBlockClearanceTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.NarrowBlockClearance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLineNarrowBlockClearanceTest::RunTest(const FString& Parameters)
{
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	FDeepLevelBuildingPlacementDefinition DeepBuilding = DeepLevelBuildingLinePlannerTests::MakeBuilding(AActor::StaticClass(), 1000.0);
	DeepBuilding.PlacementVolume.Extent.Y = 750.0;
	Catalog->Buildings.Add(DeepBuilding);

	FDeepLevelBuildingLinePath Path;
	TestTrue(TEXT("Narrow rectangular closed path builds"), DeepLevelBuildingLinePlannerTests::MakeClosedPath(
		{
			FSplinePoint(0, FVector::ZeroVector, ESplinePointType::Linear),
			FSplinePoint(1, FVector(4000.0, 0.0, 0.0), ESplinePointType::Linear),
			FSplinePoint(2, FVector(4000.0, 2500.0, 0.0), ESplinePointType::Linear),
			FSplinePoint(3, FVector(0.0, 2500.0, 0.0), ESplinePointType::Linear)
		},
		Path));

	FDeepLevelBuildingLinePlan Plan;
	FText Error;
	const bool bSolved = FDeepLevelBuildingLinePlanner::BuildPlan(
		*Catalog,
		Path,
		1337,
		1.0,
		1.0,
		EDeepLevelCornerPlacementFlags::None,
		Plan,
		Error,
		true,
		true,
		EDeepLevelBuildingClearancePolicy::DecorativeBlock);

	TestTrue(TEXT("Narrow block plan succeeds with DecorativeBlock and allow empty"), bSolved);
	TestTrue(TEXT("Narrow block placed at least one building"), !Plan.Placements.IsEmpty());

	for (int32 AIndex = 0; AIndex < Plan.Placements.Num(); ++AIndex)
	{
		const DeepLevelBuildingLinePlannerTests::FTestFootprint AFootprint =
			DeepLevelBuildingLinePlannerTests::MakeFootprint(Plan.Placements[AIndex], DeepBuilding);
		for (int32 BIndex = AIndex + 1; BIndex < Plan.Placements.Num(); ++BIndex)
		{
			const DeepLevelBuildingLinePlannerTests::FTestFootprint BFootprint =
				DeepLevelBuildingLinePlannerTests::MakeFootprint(Plan.Placements[BIndex], DeepBuilding);
			TestFalse(
				TEXT("Opposite or cross-span building footprints do not penetrate each other in narrow block"),
				DeepLevelBuildingLinePlannerTests::FootprintsOverlap(AFootprint, BFootprint));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingLayoutFragmentTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.LayoutFragment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingLayoutFragmentTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("DeepLevelBuildingLayoutFragmentWorld"), nullptr, false);
	if (!TestNotNull(TEXT("Building fragment test world is created"), World))
	{
		return false;
	}
	ADeepLevelCityLayoutActor* CityLayout = World->SpawnActor<ADeepLevelCityLayoutActor>();
	CityLayout->GridProfile = NewObject<UDeepLevelCityGridProfile>(CityLayout);
	CityLayout->SetActorLocation(FVector(100.0, 200.0, 300.0));
	ADeepLevelPCGBuildingLineActor* BuildingActor = World->SpawnActor<ADeepLevelPCGBuildingLineActor>();
	BuildingActor->CityLayout = CityLayout;
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>(BuildingActor);
	FDeepLevelBuildingPlacementDefinition Definition = DeepLevelBuildingLinePlannerTests::MakeBuilding(APackedLevelActor::StaticClass(), 500.0);
	Definition.bCalibrated = true;
	Catalog->Buildings.Add(Definition);
	BuildingActor->Catalog = Catalog;
	BuildingActor->BuildingLine->ClearSplinePoints(false);
	BuildingActor->BuildingLine->AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::Local, false);
	BuildingActor->BuildingLine->AddSplinePoint(FVector(2000.0, 0.0, 0.0), ESplineCoordinateSpace::Local, true);

	FDeepLevelCityGrid Grid;
	FText Error;
	TestTrue(TEXT("Shared city grid resolves"), CityLayout->ResolveGrid(Grid, Error));
	FDeepLevelCityLayoutFragment First;
	FDeepLevelCityLayoutFragment Second;
	if (!TestTrue(TEXT("Building fragment builds"), BuildingActor->BuildCityLayoutFragment(Grid, First, Error)))
	{
		AddError(Error.ToString());
		World->DestroyWorld(false);
		return false;
	}
	TestTrue(TEXT("Building fragment rebuilds"), BuildingActor->BuildCityLayoutFragment(Grid, Second, Error));
	TestTrue(TEXT("Building fragment has a stable source identity"), First.SourceGuid.IsValid());
	TestTrue(TEXT("Building footprints occupy shared grid cells"), !First.Cells.IsEmpty());
	TestTrue(TEXT("Building fragment emits facade and corner anchors"), !First.Anchors.IsEmpty() && First.Anchors.Num() % 8 == 0);
	TestEqual(TEXT("Repeated building fragments preserve anchor count"), Second.Anchors.Num(), First.Anchors.Num());
	for (int32 Index = 0; Index < First.Anchors.Num(); ++Index)
	{
		const FDeepLevelCityAnchor& Anchor = First.Anchors[Index];
		TestEqual(TEXT("Repeated building fragments preserve stable IDs"), Second.Anchors[Index].StableId, Anchor.StableId);
		TestTrue(TEXT("Building anchors retain rasterized footprint cells"), !Anchor.OccupiedCells.IsEmpty());
		TestTrue(
			TEXT("Building anchors are classified as facade or corner"),
			Anchor.Tags.HasTagExact(DeepLevelCityTags::Anchor_Building_Facade)
				|| Anchor.Tags.HasTagExact(DeepLevelCityTags::Anchor_Building_Corner));
	}
	for (const FDeepLevelCityCellState& Cell : First.Cells)
	{
		TestTrue(TEXT("Rasterized cells carry Building occupancy"),
			(Cell.OccupancyMask & static_cast<int32>(EDeepLevelCityOccupancy::Building)) != 0);
	}
	World->DestroyWorld(false);
	return true;
}

#endif
