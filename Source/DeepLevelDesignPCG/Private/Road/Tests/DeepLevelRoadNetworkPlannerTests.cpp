// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Road/DeepLevelRoadPCG.h"

#include "Components/SplineComponent.h"
#include "Data/PCGSplineData.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Engine/StaticMesh.h"

namespace DeepLevelRoadNetworkPlannerTests
{
	constexpr int32 TestPositiveX = static_cast<int32>(EDeepLevelRoadConnection::PositiveX);
	constexpr int32 TestPositiveY = static_cast<int32>(EDeepLevelRoadConnection::PositiveY);
	constexpr int32 TestNegativeX = static_cast<int32>(EDeepLevelRoadConnection::NegativeX);
	constexpr int32 TestNegativeY = static_cast<int32>(EDeepLevelRoadConnection::NegativeY);

	UPCGSplineData* MakeSpline(
		const FVector& Start,
		const FVector& End,
		const ESplinePointType::Type PointType = ESplinePointType::Linear,
		const FTransform& Transform = FTransform::Identity)
	{
		UPCGSplineData* Spline = NewObject<UPCGSplineData>();
		Spline->Initialize(
			{
				FSplinePoint(0, Start, PointType),
				FSplinePoint(1, End, PointType)
			},
			false,
			Transform);
		return Spline;
	}

	UPCGSplineData* MakeSpline(const TArray<FVector>& Positions)
	{
		TArray<FSplinePoint> Points;
		Points.Reserve(Positions.Num());
		for (int32 Index = 0; Index < Positions.Num(); ++Index)
		{
			Points.Emplace(Index, Positions[Index], ESplinePointType::Linear);
		}
		UPCGSplineData* Spline = NewObject<UPCGSplineData>();
		Spline->Initialize(Points, false, FTransform::Identity);
		return Spline;
	}

	FDeepLevelRoadTileDefinition MakeTile(const int32 ConnectionMask = 0, const int32 ApproachDirection = 0)
	{
		FDeepLevelRoadTileDefinition Definition;
		Definition.TileMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		Definition.ConnectionMask = ConnectionMask;
		Definition.ApproachJunctionDirectionMask = ApproachDirection;
		Definition.PlacementVolume.Extent = FVector(250.0, 250.0, 50.0);
		Definition.bCalibrated = true;
		return Definition;
	}

	UDeepLevelRoadTileCatalog* MakeCatalog()
	{
		UDeepLevelRoadTileCatalog* Catalog = NewObject<UDeepLevelRoadTileCatalog>();
		Catalog->GridProfile = NewObject<UDeepLevelCityGridProfile>(Catalog);
		Catalog->SidewalkWidthInTiles = 2;
		Catalog->Tiles = {
			MakeTile(TestPositiveX),
			MakeTile(TestPositiveX | TestNegativeX),
			MakeTile(TestPositiveX | TestPositiveY),
			MakeTile(TestPositiveX | TestPositiveY | TestNegativeX),
			MakeTile(TestPositiveX | TestPositiveY | TestNegativeX | TestNegativeY),
			MakeTile(TestPositiveX | TestNegativeX, TestPositiveX),
			MakeTile()
		};
		return Catalog;
	}

	FDeepLevelCityGrid MakeGrid()
	{
		return {FVector::ZeroVector, 500.0, 16};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelRoadNetworkCrossingTest,
	"DeepLevelDesignPCG.Editor.RoadNetwork.Crossing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelRoadNetworkCrossingTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelRoadNetworkPlannerTests;
	UDeepLevelRoadTileCatalog* Catalog = MakeCatalog();
	Catalog->Tiles.Last().PlacementVolume.Center = FVector(25.0, -35.0, 50.0);
	Catalog->Tiles.Last().PlacementVolume.Rotation = FRotator(0.0, 90.0, 0.0);
	UPCGSplineData* Horizontal = MakeSpline(FVector(-1000.0, 0.0, 0.0), FVector(1000.0, 0.0, 0.0));
	UPCGSplineData* Vertical = MakeSpline(FVector(0.0, -1000.0, 0.0), FVector(0.0, 1000.0, 0.0));
	const TArray<const UPCGSplineData*> Splines = {Horizontal, Vertical};

	FDeepLevelRoadNetworkPlan FirstPlan;
	FText Error;
	TestTrue(TEXT("Crossing network builds"), FDeepLevelRoadNetworkPlanner::BuildPlan(
		*Catalog, Splines, MakeGrid(), 41, FirstPlan, Error));
	TestEqual(TEXT("Crossing has nine unique road cells"), FirstPlan.RoadCellCount, 9);
	TestTrue(TEXT("Sidewalk ring is generated"), FirstPlan.SidewalkCellCount > 0);

	int32 ApproachCount = 0;
	bool bFoundCrossing = false;
	bool bFoundSecondSidewalkRow = false;
	for (const FDeepLevelRoadTilePlacement& Placement : FirstPlan.Placements)
	{
		const FDeepLevelRoadTileDefinition& Definition = Placement.Kind == EDeepLevelRoadTileKind::Sidewalk
			? Catalog->Tiles.Last()
			: Catalog->Tiles[0];
		const FTransform WorldVolume = FTransform(
			Definition.PlacementVolume.Rotation,
			Definition.PlacementVolume.Center) * Placement.Transform;
		TestTrue(TEXT("Tile OBB X is snapped to the 500 grid"), FMath::IsNearlyZero(FMath::Fmod(WorldVolume.GetLocation().X, 500.0)));
		TestTrue(TEXT("Tile OBB Y is snapped to the 500 grid"), FMath::IsNearlyZero(FMath::Fmod(WorldVolume.GetLocation().Y, 500.0)));
		if (Placement.Kind == EDeepLevelRoadTileKind::Road)
		{
			ApproachCount += Placement.bJunctionApproach ? 1 : 0;
			bFoundCrossing |= Placement.GridCell == FIntPoint::ZeroValue
				&& Placement.ConnectionMask == (TestPositiveX | TestPositiveY | TestNegativeX | TestNegativeY);
		}
		else
		{
			bFoundSecondSidewalkRow |= Placement.GridCell == FIntPoint(2, 2);
			TestTrue(TEXT("Calibrated sidewalk OBB center lands on its grid cell"),
				FVector2D(WorldVolume.GetLocation()).Equals(
					FVector2D(Placement.GridCell.X * 500.0, Placement.GridCell.Y * 500.0),
					0.01));
		}
	}
	TestTrue(TEXT("Crossing receives the four-way road tile"), bFoundCrossing);
	TestEqual(TEXT("Four road cells use junction-approach tiles"), ApproachCount, 4);
	TestTrue(TEXT("Sidewalk repeats for two cells beyond the road"), bFoundSecondSidewalkRow);

	FDeepLevelRoadNetworkPlan SecondPlan;
	TestTrue(TEXT("Same network rebuilds"), FDeepLevelRoadNetworkPlanner::BuildPlan(
		*Catalog, Splines, MakeGrid(), 41, SecondPlan, Error));
	TestEqual(TEXT("Deterministic placement count"), SecondPlan.Placements.Num(), FirstPlan.Placements.Num());
	for (int32 Index = 0; Index < FirstPlan.Placements.Num() && Index < SecondPlan.Placements.Num(); ++Index)
	{
		TestEqual(TEXT("Deterministic grid cell"), SecondPlan.Placements[Index].GridCell, FirstPlan.Placements[Index].GridCell);
		TestEqual(TEXT("Deterministic transform"), SecondPlan.Placements[Index].Transform, FirstPlan.Placements[Index].Transform);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelRoadNetworkExteriorCornerSidewalkTest,
	"DeepLevelDesignPCG.Editor.RoadNetwork.ExteriorCornerSidewalk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelRoadNetworkExteriorCornerSidewalkTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelRoadNetworkPlannerTests;
	UDeepLevelRoadTileCatalog* Catalog = MakeCatalog();
	UPCGSplineData* Corner = MakeSpline({
		FVector(0.0, 0.0, 0.0),
		FVector(500.0, 0.0, 0.0),
		FVector(500.0, 500.0, 0.0)});
	const TArray<const UPCGSplineData*> Splines = {Corner};

	FDeepLevelRoadNetworkPlan Plan;
	FText Error;
	TestTrue(TEXT("Corner road network builds"), FDeepLevelRoadNetworkPlanner::BuildPlan(
		*Catalog, Splines, MakeGrid(), 41, Plan, Error));

	TSet<FIntPoint> SidewalkCells;
	for (const FDeepLevelRoadTilePlacement& Placement : Plan.Placements)
	{
		if (Placement.Kind == EDeepLevelRoadTileKind::Sidewalk)
		{
			SidewalkCells.Add(Placement.GridCell);
		}
	}

	TestTrue(TEXT("Exterior corner near cell is filled"), SidewalkCells.Contains(FIntPoint(2, -1)));
	TestTrue(TEXT("Exterior corner width is filled"), SidewalkCells.Contains(FIntPoint(3, -2)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelRoadTileCatalogValidationTest,
	"DeepLevelDesignPCG.Editor.RoadNetwork.CatalogValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelRoadTileCatalogValidationTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelRoadNetworkPlannerTests;
	UDeepLevelRoadTileCatalog* Catalog = MakeCatalog();
	FText Error;
	TestTrue(TEXT("Complete canonical tile set validates"), Catalog->ValidateForGeneration(Error));
	TestEqual(TEXT("Single connection is classified as Dead End"), Catalog->Tiles[0].GetTopology(), EDeepLevelRoadTileTopology::DeadEnd);
	TestEqual(TEXT("Opposite connections are classified as Straight"), Catalog->Tiles[1].GetTopology(), EDeepLevelRoadTileTopology::Straight);
	TestEqual(TEXT("Adjacent connections are classified as Corner"), Catalog->Tiles[2].GetTopology(), EDeepLevelRoadTileTopology::Corner);
	TestEqual(TEXT("Three connections are classified as T-Junction"), Catalog->Tiles[3].GetTopology(), EDeepLevelRoadTileTopology::TJunction);
	TestEqual(TEXT("Four connections are classified as Four-Way"), Catalog->Tiles[4].GetTopology(), EDeepLevelRoadTileTopology::FourWay);
	TestTrue(TEXT("Approach tile is classified as Junction Approach"), Catalog->Tiles[5].IsJunctionApproach());
	TestEqual(TEXT("No connections are classified as Sidewalk"), Catalog->Tiles[6].GetTopology(), EDeepLevelRoadTileTopology::Sidewalk);

	UDeepLevelRoadTileCatalog* IncompleteCatalog = MakeCatalog();
	IncompleteCatalog->Tiles.RemoveAt(0);
	TestFalse(TEXT("Missing Dead End tile is rejected"), IncompleteCatalog->ValidateForGeneration(Error));
	TestTrue(TEXT("Completeness error names Dead End and its port layout"),
		Error.ToString().Contains(TEXT("Dead End")) && Error.ToString().Contains(TEXT("one ROAD port")));

	Catalog->Tiles[0].PlacementVolume.Extent.X = 500.0;
	TestFalse(TEXT("Non-1x1 tiles are rejected"), Catalog->ValidateForGeneration(Error));
	TestTrue(TEXT("Validation explains the 1x1 contract"), Error.ToString().Contains(TEXT("1x1")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelRoadSplineValidationTest,
	"DeepLevelDesignPCG.Editor.RoadNetwork.SplineValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelRoadSplineValidationTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelRoadNetworkPlannerTests;
	UDeepLevelRoadTileCatalog* Catalog = MakeCatalog();
	FDeepLevelRoadNetworkPlan Plan;
	FText Error;

	UPCGSplineData* Curved = MakeSpline(FVector::ZeroVector, FVector(1000.0, 0.0, 0.0), ESplinePointType::Curve);
	TestFalse(TEXT("Curved spline is rejected"), FDeepLevelRoadNetworkPlanner::BuildPlan(*Catalog, {Curved}, MakeGrid(), 1, Plan, Error));
	TestTrue(TEXT("Curved spline error explains Linear point type"), Error.ToString().Contains(TEXT("Linear")));

	UPCGSplineData* Diagonal = MakeSpline(FVector::ZeroVector, FVector(1000.0, 1000.0, 0.0));
	TestFalse(TEXT("Diagonal linear segment is rejected"), FDeepLevelRoadNetworkPlanner::BuildPlan(*Catalog, {Diagonal}, MakeGrid(), 1, Plan, Error));
	TestTrue(TEXT("Diagonal error explains grid-axis requirement"), Error.ToString().Contains(TEXT("grid axis")));

	UPCGSplineData* OffGrid = MakeSpline(FVector(250.0, 0.0, 0.0), FVector(1250.0, 0.0, 0.0));
	TestTrue(TEXT("Off-grid points snap to the nearest cells"), FDeepLevelRoadNetworkPlanner::BuildPlan(*Catalog, {OffGrid}, MakeGrid(), 1, Plan, Error));
	TestTrue(TEXT("Snapped off-grid spline creates road cells"), Plan.RoadCellCount > 0);

	UPCGSplineData* NearlyVertical = MakeSpline(FVector(0.2, 0.1, 0.0), FVector(-0.3, 1499.8, 0.0));
	TestTrue(TEXT("Small authoring drift still resolves to an axis-aligned road"), FDeepLevelRoadNetworkPlanner::BuildPlan(*Catalog, {NearlyVertical}, MakeGrid(), 1, Plan, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelRoadSplineTransformTest,
	"DeepLevelDesignPCG.Editor.RoadNetwork.SplineTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelRoadSplineTransformTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelRoadNetworkPlannerTests;
	UDeepLevelRoadTileCatalog* Catalog = MakeCatalog();
	const FTransform SplineTransform(FRotator(0.0, 90.0, 0.0), FVector(1000.0, 1500.0, 0.0));
	UPCGSplineData* Spline = MakeSpline(
		FVector::ZeroVector,
		FVector(1000.0, 0.0, 0.0),
		ESplinePointType::Linear,
		SplineTransform);

	FDeepLevelRoadNetworkPlan Plan;
	FText Error;
	TestTrue(TEXT("Transformed spline builds"), FDeepLevelRoadNetworkPlanner::BuildPlan(
		*Catalog, {Spline}, MakeGrid(), 17, Plan, Error));
	TestEqual(TEXT("Transformed spline occupies three cells"), Plan.RoadCellCount, 3);

	TSet<FIntPoint> RoadCells;
	for (const FDeepLevelRoadTilePlacement& Placement : Plan.Placements)
	{
		if (Placement.Kind == EDeepLevelRoadTileKind::Road)
		{
			RoadCells.Add(Placement.GridCell);
		}
	}
	TestTrue(TEXT("World transform translation is applied"), RoadCells.Contains(FIntPoint(2, 3)));
	TestTrue(TEXT("World transform rotation is applied"), RoadCells.Contains(FIntPoint(2, 5)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelRoadJunctionDistanceTest,
	"DeepLevelDesignPCG.Editor.RoadNetwork.JunctionDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelRoadJunctionDistanceTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelRoadNetworkPlannerTests;
	UDeepLevelRoadTileCatalog* Catalog = MakeCatalog();
	FText Error;

	auto BuildCorridor = [Catalog, &Error](const int32 EndX, FDeepLevelRoadNetworkPlan& OutPlan)
	{
		const double EndWorldX = EndX * 500.0;
		const TArray<const UPCGSplineData*> Splines = {
			MakeSpline(FVector(-500.0, 0.0, 0.0), FVector(EndWorldX + 500.0, 0.0, 0.0)),
			MakeSpline(FVector::ZeroVector, FVector(0.0, 500.0, 0.0)),
			MakeSpline(FVector(EndWorldX, 0.0, 0.0), FVector(EndWorldX, 500.0, 0.0))};
		return FDeepLevelRoadNetworkPlanner::BuildPlan(*Catalog, Splines, MakeGrid(), 17, OutPlan, Error);
	};

	FDeepLevelRoadNetworkPlan AdjacentPlan;
	TestTrue(TEXT("Adjacent junction regions build"), BuildCorridor(1, AdjacentPlan));
	int32 AdjacentApproaches = 0;
	for (const FDeepLevelRoadTilePlacement& Placement : AdjacentPlan.Placements)
	{
		AdjacentApproaches += Placement.bJunctionApproach ? 1 : 0;
	}
	TestEqual(TEXT("Adjacent junction regions need no approach cell between them"), AdjacentApproaches, 0);

	FDeepLevelRoadNetworkPlan LongPlan;
	TestTrue(TEXT("Long junction corridor builds"), BuildCorridor(8, LongPlan));
	TSet<FIntPoint> ApproachCells;
	for (const FDeepLevelRoadTilePlacement& Placement : LongPlan.Placements)
	{
		if (Placement.bJunctionApproach)
		{
			ApproachCells.Add(Placement.GridCell);
			TestTrue(TEXT("Every approach remains a straight road tile"),
				Placement.ConnectionMask == (TestPositiveX | TestNegativeX)
				|| Placement.ConnectionMask == (TestPositiveY | TestNegativeY));
		}
	}
	TestTrue(TEXT("Each corridor boundary receives an approach"),
		ApproachCells.Contains(FIntPoint(1, 0)) && ApproachCells.Contains(FIntPoint(7, 0)));
	TestFalse(TEXT("Corridor interior remains ordinary road"), ApproachCells.Contains(FIntPoint(4, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelRoadParallelSurfaceTest,
	"DeepLevelDesignPCG.Editor.RoadNetwork.ParallelSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelRoadParallelSurfaceTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelRoadNetworkPlannerTests;
	UDeepLevelRoadTileCatalog* Catalog = MakeCatalog();
	FText Error;

	for (int32 RowDistance = 1; RowDistance <= 7; ++RowDistance)
	{
		const double OtherY = RowDistance * 500.0;
		const TArray<const UPCGSplineData*> Splines = {
			MakeSpline(FVector(-1000.0, 0.0, 0.0), FVector(1000.0, 0.0, 0.0)),
			MakeSpline(FVector(-1000.0, OtherY, 0.0), FVector(1000.0, OtherY, 0.0))};
		FDeepLevelRoadNetworkPlan Plan;
		TestTrue(FString::Printf(TEXT("Parallel road distance %d builds"), RowDistance),
			FDeepLevelRoadNetworkPlanner::BuildPlan(*Catalog, Splines, MakeGrid(), 29, Plan, Error));

		TSet<FIntPoint> RoadCells;
		TSet<FIntPoint> SidewalkCells;
		for (const FDeepLevelRoadTilePlacement& Placement : Plan.Placements)
		{
			(Placement.Kind == EDeepLevelRoadTileKind::Road ? RoadCells : SidewalkCells).Add(Placement.GridCell);
		}
		for (int32 Y = -2; Y <= RowDistance + 2; ++Y)
		{
			const bool bRoad = Y == 0 || Y == RowDistance;
			const bool bWithinSidewalkBand = FMath::Min(FMath::Abs(Y), FMath::Abs(Y - RowDistance)) <= 2;
			TestEqual(
				FString::Printf(TEXT("Merged surface classification at row distance %d, Y %d"), RowDistance, Y),
				SidewalkCells.Contains(FIntPoint(0, Y)),
				!bRoad && bWithinSidewalkBand);
		}
		TestFalse(TEXT("Road cells are never replaced by sidewalk"),
			SidewalkCells.Contains(FIntPoint(0, 0)) || SidewalkCells.Contains(FIntPoint(0, RowDistance)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelRoadWideIntersectionTest,
	"DeepLevelDesignPCG.Editor.RoadNetwork.WideIntersection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelRoadWideIntersectionTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelRoadNetworkPlannerTests;
	UDeepLevelRoadTileCatalog* Catalog = MakeCatalog();
	const TArray<const UPCGSplineData*> Splines = {
		MakeSpline(FVector(-1000.0, 0.0, 0.0), FVector(1500.0, 0.0, 0.0)),
		MakeSpline(FVector(-1000.0, 500.0, 0.0), FVector(1500.0, 500.0, 0.0)),
		MakeSpline(FVector(0.0, -1000.0, 0.0), FVector(0.0, 1500.0, 0.0)),
		MakeSpline(FVector(500.0, -1000.0, 0.0), FVector(500.0, 1500.0, 0.0))};
	FDeepLevelRoadNetworkPlan Plan;
	FText Error;
	TestTrue(TEXT("Two-by-two wide intersection builds"),
		FDeepLevelRoadNetworkPlanner::BuildPlan(*Catalog, Splines, MakeGrid(), 31, Plan, Error));

	TSet<FIntPoint> JunctionCells;
	for (const FDeepLevelRoadTilePlacement& Placement : Plan.Placements)
	{
		if (Placement.Kind == EDeepLevelRoadTileKind::Road && FMath::CountBits(static_cast<uint64>(Placement.ConnectionMask)) >= 3)
		{
			JunctionCells.Add(Placement.GridCell);
		}
	}
	TestEqual(TEXT("Wide crossing becomes one four-cell junction region"), JunctionCells.Num(), 4);
	TestTrue(TEXT("Wide junction contains every overlapping road cell"),
		JunctionCells.Contains(FIntPoint(0, 0))
		&& JunctionCells.Contains(FIntPoint(1, 0))
		&& JunctionCells.Contains(FIntPoint(0, 1))
		&& JunctionCells.Contains(FIntPoint(1, 1)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelRoadCellOverrideTest,
	"DeepLevelDesignPCG.Editor.RoadNetwork.CellOverrides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelRoadCellOverrideTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelRoadNetworkPlannerTests;
	UDeepLevelRoadTileCatalog* Catalog = MakeCatalog();
	const TArray<const UPCGSplineData*> Splines = {
		MakeSpline(FVector::ZeroVector, FVector(1000.0, 0.0, 0.0))};
	FText Error;
	FDeepLevelRoadNetworkPlan BasePlan;
	TestTrue(TEXT("Base road network builds"), FDeepLevelRoadNetworkPlanner::BuildPlan(
		*Catalog, Splines, MakeGrid(), 41, BasePlan, Error));

	FDeepLevelRoadCellOverride Modify;
	Modify.GridCell = FIntPoint::ZeroValue;
	Modify.Mode = EDeepLevelRoadCellOverrideMode::Modify;
	Modify.LocalOffset = FVector(0.0, 0.0, 25.0);
	Modify.RotationOffset = FRotator(0.0, 15.0, 0.0);
	Modify.ScaleMultiplier = FVector(1.1, 1.1, 1.0);

	FDeepLevelRoadCellOverride Remove;
	Remove.GridCell = FIntPoint(0, 1);
	Remove.Mode = EDeepLevelRoadCellOverrideMode::Remove;

	FDeepLevelRoadCellOverride Add;
	Add.GridCell = FIntPoint(10, 10);
	Add.Mode = EDeepLevelRoadCellOverrideMode::Add;
	Add.AddedTileKind = EDeepLevelRoadTileKind::Sidewalk;
	Add.ReplacementMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	const TArray<FDeepLevelRoadCellOverride> Overrides = {Modify, Remove, Add};

	FDeepLevelRoadNetworkPlan OverridePlan;
	TestTrue(TEXT("Road network with final-placement overrides builds"), FDeepLevelRoadNetworkPlanner::BuildPlan(
		*Catalog, Splines, MakeGrid(), 41, OverridePlan, Error, Overrides));
	TestEqual(TEXT("Remove and Add preserve total placement count"), OverridePlan.Placements.Num(), BasePlan.Placements.Num());
	TestFalse(TEXT("Removed cell has no placement"), OverridePlan.Placements.ContainsByPredicate([](const FDeepLevelRoadTilePlacement& Placement)
	{
		return Placement.GridCell == FIntPoint(0, 1);
	}));

	const FDeepLevelRoadTilePlacement* ModifiedPlacement = OverridePlan.Placements.FindByPredicate([](const FDeepLevelRoadTilePlacement& Placement)
	{
		return Placement.GridCell == FIntPoint::ZeroValue;
	});
	const FDeepLevelRoadTilePlacement* BasePlacement = BasePlan.Placements.FindByPredicate([](const FDeepLevelRoadTilePlacement& Placement)
	{
		return Placement.GridCell == FIntPoint::ZeroValue;
	});
	if (TestNotNull(TEXT("Modified cell remains present"), ModifiedPlacement)
		&& TestNotNull(TEXT("Base cell exists for comparison"), BasePlacement))
	{
		TestEqual(
			TEXT("Modify applies local height offset on top of catalog calibration"),
			ModifiedPlacement->Transform.GetLocation().Z,
			BasePlacement->Transform.GetLocation().Z + 25.0);
		TestTrue(TEXT("Modify applies rotation offset"),
			FMath::IsNearlyEqual(
				ModifiedPlacement->Transform.Rotator().Yaw,
				BasePlacement->Transform.Rotator().Yaw + 15.0,
				0.01));
		TestTrue(TEXT("Modify applies scale multiplier"),
			ModifiedPlacement->Transform.GetScale3D().Equals(
				BasePlacement->Transform.GetScale3D() * FVector(1.1, 1.1, 1.0),
				0.01));
	}

	const FDeepLevelRoadTilePlacement* AddedPlacement = OverridePlan.Placements.FindByPredicate([](const FDeepLevelRoadTilePlacement& Placement)
	{
		return Placement.GridCell == FIntPoint(10, 10);
	});
	if (TestNotNull(TEXT("Added cell creates a placement"), AddedPlacement))
	{
		TestEqual(TEXT("Added cell uses authored kind"), AddedPlacement->Kind, EDeepLevelRoadTileKind::Sidewalk);
		TestTrue(TEXT("Added cell is positioned from the Road Network grid"),
			AddedPlacement->Transform.GetLocation().Equals(FVector(5000.0, 5000.0, 0.0), 0.01));
	}

	FDeepLevelRoadCellOverride Duplicate = Modify;
	Duplicate.Mode = EDeepLevelRoadCellOverrideMode::Remove;
	const TArray<FDeepLevelRoadCellOverride> DuplicateOverrides = {Modify, Duplicate};
	TestFalse(TEXT("Duplicate overrides are rejected"), FDeepLevelRoadNetworkPlanner::BuildPlan(
		*Catalog, Splines, MakeGrid(), 41, OverridePlan, Error, DuplicateOverrides));
	TestFalse(TEXT("Duplicate override validation reports an error"), Error.IsEmpty());
	return true;
}

#endif
