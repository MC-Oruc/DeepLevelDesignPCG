// Copyright <--\, Inc. All Rights Reserved.

#include "Building/DeepLevelBuildingSidewalkInfill.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "PackedLevelActor/PackedLevelActor.h"
#include "Road/DeepLevelRoadPCG.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingSidewalkInfillTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.SidewalkInfill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingSidewalkInfillTest::RunTest(const FString& Parameters)
{
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	FDeepLevelBuildingPlacementDefinition& Definition = Catalog->Buildings.Emplace_GetRef();
	Definition.BuildingClass = APackedLevelActor::StaticClass();
	Definition.PlacementVolume.Extent = FVector(200.0, 100.0, 50.0);
	Definition.bCalibrated = true;

	FDeepLevelBuildingLinePlacement Placement;
	Placement.FrontageId = FGuid(1, 2, 3, 4);
	Placement.BuildingClass = APackedLevelActor::StaticClass();
	Placement.PathSample.Forward = FVector::ForwardVector;
	Placement.PathSample.Right = FVector::RightVector;
	FDeepLevelBuildingLinePlan Before;
	Before.Placements.Add(Placement);
	FDeepLevelBuildingLinePlan After = Before;
	After.Placements[0].PathSample.Location.X = 100.0;

	TArray<FDeepLevelCityAnchor> Anchors;
	FText Error;
	const FDeepLevelCityGrid Grid{FVector::ZeroVector, 500.0, 16};
	TestTrue(TEXT("Vacated footprint builds"), DeepLevelBuildingSidewalkInfill::BuildAnchors(
		*Catalog, Grid, FGuid(5, 6, 7, 8), 1, Before, After, Anchors, Error));
	TestEqual(TEXT("Movement creates one rectangular infill"), Anchors.Num(), 1);
	if (Anchors.Num() == 1)
	{
		TestTrue(TEXT("Infill carries the semantic tag"),
			Anchors[0].Tags.HasTagExact(DeepLevelCityTags::Anchor_Sidewalk_Infill));
		TestEqual(TEXT("Infill width matches vacated strip"), Anchors[0].Extent.X * 2.0, 100.0);
		TestEqual(TEXT("Infill depth matches building footprint"), Anchors[0].Extent.Y * 2.0, 200.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelRoadSidewalkInfillTest,
	"DeepLevelDesignPCG.Editor.RoadNetwork.SidewalkInfill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelRoadSidewalkInfillTest::RunTest(const FString& Parameters)
{
	UDeepLevelRoadTileCatalog* Catalog = NewObject<UDeepLevelRoadTileCatalog>();
	FDeepLevelRoadTileDefinition& Definition = Catalog->Tiles.Emplace_GetRef();
	Definition.TileMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Definition.PlacementVolume.Extent = FVector(250.0, 250.0, 50.0);
	Definition.bCalibrated = true;

	FDeepLevelCityAnchor Anchor;
	Anchor.StableId = FGuid(10, 11, 12, 13);
	Anchor.Geometry = EDeepLevelCityAnchorGeometry::Surface;
	Anchor.Transform = FTransform(FVector::ZeroVector);
	Anchor.Extent = FVector(100.0, 100.0, 0.0);
	Anchor.Tags.AddTag(DeepLevelCityTags::Anchor_Sidewalk_Infill);

	FDeepLevelRoadNetworkPlan Plan;
	FDeepLevelRoadTilePlacement& ExistingSidewalk = Plan.Placements.Emplace_GetRef();
	ExistingSidewalk.GridCell = FIntPoint(0, 0);
	ExistingSidewalk.Kind = EDeepLevelRoadTileKind::Sidewalk;
	FDeepLevelRoadTilePlacement& ExistingRoad = Plan.Placements.Emplace_GetRef();
	ExistingRoad.GridCell = FIntPoint(1, 0);
	ExistingRoad.Kind = EDeepLevelRoadTileKind::Road;
	FText Error;
	const FDeepLevelCityGrid Grid{FVector::ZeroVector, 500.0, 16};
	TestTrue(TEXT("Road planner consumes infill"), FDeepLevelRoadNetworkPlanner::AppendSidewalkInfills(
		*Catalog, Grid, MakeArrayView(&Anchor, 1), 42, Plan, Error));
	TestEqual(TEXT("One-tile safety ring fills seven free cells"), Plan.SidewalkInfillCount, 7);
	TestEqual(TEXT("Existing placements remain and seven cells are appended"), Plan.Placements.Num(), 9);
	TSet<FIntPoint> UniqueCells;
	for (int32 Index = 0; Index < Plan.Placements.Num(); ++Index)
	{
		const FDeepLevelRoadTilePlacement& Placement = Plan.Placements[Index];
		TestFalse(TEXT("Each grid cell is emitted once"), UniqueCells.Contains(Placement.GridCell));
		UniqueCells.Add(Placement.GridCell);
		if (Index >= 2)
		{
			TestTrue(TEXT("Infill keeps calibrated tile scale"),
				Placement.Transform.GetScale3D().Equals(FVector::OneVector, 0.001));
			TestFalse(TEXT("Infill never replaces the road cell"), Placement.GridCell == FIntPoint(1, 0));
		}
	}
	return true;
}

#endif
