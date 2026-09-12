// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Building/DeepLevelBuildingLayout.h"
#include "Misc/AutomationTest.h"
#include "PackedLevelActor/PackedLevelActor.h"
#include "UObject/StrongObjectPtr.h"

namespace DeepLevelBuildingRoadsideTests
{
	FDeepLevelCityLayoutFragment MakeRoad()
	{
		FDeepLevelCityLayoutFragment Road;
		Road.SourceGuid = FGuid(1, 2, 3, 4);
		for (int32 X = 0; X < 6; ++X)
		{
			Road.Cells.Add({FIntPoint(X, 0), static_cast<int32>(EDeepLevelCityOccupancy::Road), {}});
			Road.Cells.Add({FIntPoint(X, 1), static_cast<int32>(EDeepLevelCityOccupancy::Sidewalk), {}});
			Road.Cells.Add({FIntPoint(X, 2), static_cast<int32>(EDeepLevelCityOccupancy::Sidewalk), {}});
			FDeepLevelCityAnchor& Anchor = Road.Anchors.Emplace_GetRef();
			Anchor.StableId = FDeepLevelCityStableId::MakeAnchorId(Road.SourceGuid, FString::FromInt(X), TEXT("SidewalkEdge"));
			Anchor.Geometry = EDeepLevelCityAnchorGeometry::Segment;
			Anchor.Transform = FTransform(FVector(X * 500.0, 325.0, 0));
			Anchor.Extent = FVector(250, 0, 0);
			Anchor.OccupiedCells.Add(FIntPoint(X, 1));
			Anchor.Tags.AddTag(DeepLevelCityTags::Anchor_Sidewalk_Edge);
		}
		return Road;
	}

	TStrongObjectPtr<UDeepLevelBuildingPlacementCatalog> MakeCatalog()
	{
		TStrongObjectPtr<UDeepLevelBuildingPlacementCatalog> Catalog(NewObject<UDeepLevelBuildingPlacementCatalog>());
		FDeepLevelBuildingPlacementDefinition& Building = Catalog->Buildings.Emplace_GetRef();
		Building.BuildingClass = APackedLevelActor::StaticClass();
		Building.bCalibrated = true;
		Building.PlacementVolume.Extent = FVector(250, 100, 100);
		Building.PlacementVolume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Forbidden;
		Building.PlacementVolume.Exposure.NegativeX = EDeepLevelStreetExposureRule::Forbidden;
		Building.PlacementVolume.Exposure.PositiveY = EDeepLevelStreetExposureRule::Forbidden;
		Building.PlacementVolume.Exposure.NegativeY = EDeepLevelStreetExposureRule::Required;
		return Catalog;
	}

	bool Plan(const TArray<FDeepLevelCityLayoutFragment>& Fragments, const UDeepLevelBuildingPlacementCatalog& Catalog,
		FDeepLevelBuildingLinePlan& OutPlan, int32& Rejected, FText& Error)
	{
		FDeepLevelCityGrid Grid;
		TSharedPtr<const FDeepLevelCityLayoutSnapshot> Base;
		return FDeepLevelCityLayoutBuilder::Build(Grid, Fragments, Base, Error)
			&& DeepLevelBuildingRoadside::BuildPlan(*Base, Catalog, FGuid(5, 6, 7, 8), 1337, 1.0, 1.0,
				EDeepLevelCornerPlacementFlags::Inner, {}, OutPlan, Rejected, Error);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingRoadsideBoundaryTest,
	"DeepLevelDesignPCG.Building.Roadside.BoundaryAndDeterminism", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingRoadsideBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelBuildingRoadsideTests;
	auto Catalog = MakeCatalog();
	auto Road = MakeRoad();
	FDeepLevelBuildingLinePlan First, Second;
	FText Error;
	int32 Rejected = 0;
	if (!TestTrue(TEXT("Road data produces a plan"), Plan({Road}, *Catalog, First, Rejected, Error))) { AddError(Error.ToString()); return false; }
	TestEqual(TEXT("All six modules fit"), First.Placements.Num(), 6);
	for (const auto& Placement : First.Placements)
	{
		TestEqual(TEXT("Facade uses outer sidewalk boundary, not curb anchor"), Placement.PathSample.Location.Y, 1250.0);
		TestTrue(TEXT("Building extends into block"), Placement.PathSample.Right.Equals(FVector::RightVector));
		TestTrue(TEXT("Frontage identity assigned"), Placement.FrontageId.IsValid());
	}
	Road.Cells.Sort([](const auto& A, const auto& B) { return A.Cell.X > B.Cell.X; });
	Road.Anchors.Sort([](const auto& A, const auto& B) { return B.StableId < A.StableId; });
	TestTrue(TEXT("Reordered input remains valid"), Plan({Road}, *Catalog, Second, Rejected, Error));
	TestEqual(TEXT("Input order does not change placement count"), Second.Placements.Num(), First.Placements.Num());
	for (int32 Index = 0; Index < FMath::Min(First.Placements.Num(), Second.Placements.Num()); ++Index)
	{
		TestTrue(TEXT("Positions remain deterministic"), First.Placements[Index].PathSample.Location.Equals(Second.Placements[Index].PathSample.Location));
		TestEqual(TEXT("Frontage identity remains deterministic"), First.Placements[Index].FrontageId, Second.Placements[Index].FrontageId);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingRoadsideClearanceTest,
	"DeepLevelDesignPCG.Building.Roadside.ObstaclesAndEmptyOutput", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingRoadsideClearanceTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelBuildingRoadsideTests;
	auto Catalog = MakeCatalog();
	auto Road = MakeRoad();
	FDeepLevelCityLayoutFragment ExistingBuilding;
	ExistingBuilding.SourceGuid = FGuid(10, 11, 12, 13);
	for (int32 X = 0; X < 6; ++X) { ExistingBuilding.Cells.Add({FIntPoint(X, 3), static_cast<int32>(EDeepLevelCityOccupancy::Building), {}}); }
	FDeepLevelBuildingLinePlan Result;
	FText Error;
	int32 Rejected = 0;
	TestTrue(TEXT("Fully obstructed frontage is valid empty output"), Plan({Road, ExistingBuilding}, *Catalog, Result, Rejected, Error));
	TestEqual(TEXT("Existing buildings block all candidates"), Result.Placements.Num(), 0);
	TestEqual(TEXT("Rejected candidate summary"), Rejected, 6);
	Road.Anchors[2].Tags.AddTag(DeepLevelCityTags::Anchor_Road_Junction);
	TestTrue(TEXT("Junction splits frontage"), Plan({Road}, *Catalog, Result, Rejected, Error));
	for (const auto& Placement : Result.Placements)
	{
		TestTrue(TEXT("No facade inside junction approach"), FMath::Abs(Placement.PathSample.Location.X - 1000.0) >= 500.0 - 0.01);
	}
	Catalog->Buildings[0].PlacementVolume.Extent.X = 4000.0;
	TestTrue(TEXT("Valid catalog with no fitting building returns empty output"), Plan({MakeRoad()}, *Catalog, Result, Rejected, Error));
	TestTrue(TEXT("No fitting building produces no placements"), Result.Placements.IsEmpty());
	Road.Anchors.Reset();
	TestFalse(TEXT("Missing semantic edges is an error, not spline fallback"), Plan({Road}, *Catalog, Result, Rejected, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingFootprintContactTest,
	"DeepLevelDesignPCG.Building.Roadside.FootprintContact", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingFootprintContactTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelBuildingLayoutGeometry;
	const FFootprint A = {FVector2D(0,0), FVector2D(100,0), FVector2D(100,100), FVector2D(0,100)};
	FFootprint B = A;
	for (FVector2D& Corner : B) { Corner.X += 100.0; }
	TestFalse(TEXT("Shared boundary is allowed"), FootprintsOverlap(A, B));
	for (FVector2D& Corner : B) { Corner.X -= 1.0; }
	TestTrue(TEXT("Positive-area overlap is rejected"), FootprintsOverlap(A, B));
	return true;
}

#endif
