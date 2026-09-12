// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "City/DeepLevelCityLayout.h"
#include "City/DeepLevelCityDecoration.h"

#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelCityLayoutTest,
	"DeepLevelDesignPCG.Editor.City.Layout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityLayoutTest::RunTest(const FString& Parameters)
{
	FDeepLevelCityGrid Grid;
	Grid.Origin = FVector(250.0, -250.0, 100.0);
	Grid.TileSize = 500.0;
	Grid.ChunkSizeInCells = 4;
	FText Error;
	TestTrue(TEXT("Flat city grid validates"), Grid.Validate(Error));
	TestEqual(TEXT("World position resolves to canonical cell"), Grid.WorldToCell(FVector(750.0, 250.0, 400.0)), FIntPoint(1, 1));
	TestEqual(TEXT("Cell uses the authoritative ground height"), Grid.CellToWorld(FIntPoint(1, 1)), FVector(750.0, 250.0, 100.0));
	TestEqual(TEXT("Negative cells use floor-based chunks"), Grid.CellToChunk(FIntPoint(-1, -4)), FIntPoint(-1, -1));

	ADeepLevelCityLayoutActor* LayoutActor = NewObject<ADeepLevelCityLayoutActor>();
	LayoutActor->GridProfile = NewObject<UDeepLevelCityGridProfile>(LayoutActor);
	LayoutActor->GridProfile->TileSize = 500.0;
	LayoutActor->GridProfile->ChunkSizeInCells = 4;
	LayoutActor->SetActorLocation(Grid.Origin);
	FDeepLevelCityGrid ResolvedGrid;
	TestTrue(TEXT("City Layout resolves its authoritative grid"), LayoutActor->ResolveGrid(ResolvedGrid, Error));
	TestEqual(TEXT("City Layout actor owns the grid origin"), ResolvedGrid.Origin, Grid.Origin);
	TestEqual(TEXT("City Grid Profile owns tile size"), ResolvedGrid.TileSize, Grid.TileSize);

	const FGuid RoadGuid(1, 2, 3, 4);
	const FGuid AnchorId = FDeepLevelCityStableId::MakeAnchorId(RoadGuid, TEXTVIEW("Cell:1,1"), TEXT("SidewalkEdge"));
	TestEqual(TEXT("Anchor IDs are deterministic"), AnchorId,
		FDeepLevelCityStableId::MakeAnchorId(RoadGuid, TEXTVIEW("Cell:1,1"), TEXT("SidewalkEdge")));
	TestNotEqual(TEXT("Anchor slots produce distinct IDs"), AnchorId,
		FDeepLevelCityStableId::MakeAnchorId(RoadGuid, TEXTVIEW("Cell:1,1"), TEXT("Curb")));

	FDeepLevelCityLayoutFragment Road;
	Road.SourceGuid = RoadGuid;
	Road.SourceRevision = 2;
	Road.Cells.Add({FIntPoint(1, 1), static_cast<int32>(EDeepLevelCityOccupancy::Sidewalk), {}});
	FDeepLevelCityAnchor& Anchor = Road.Anchors.Emplace_GetRef();
	Anchor.StableId = AnchorId;
	Anchor.Geometry = EDeepLevelCityAnchorGeometry::Segment;
	Anchor.Transform.SetLocation(Grid.CellToWorld(FIntPoint(1, 1)));
	Anchor.Extent = FVector(250.0, 0.0, 0.0);
	Anchor.OccupiedCells.Add(FIntPoint(1, 1));
	Anchor.SourceRevision = Road.SourceRevision;

	FDeepLevelCityLayoutFragment Building;
	Building.SourceGuid = FGuid(5, 6, 7, 8);
	Building.SourceRevision = 3;
	Building.Cells.Add({FIntPoint(1, 1), static_cast<int32>(EDeepLevelCityOccupancy::Building), {}});
	Building.Cells.Add({FIntPoint(5, 0), static_cast<int32>(EDeepLevelCityOccupancy::Building), {}});

	TSharedPtr<const FDeepLevelCityLayoutSnapshot> FirstSnapshot;
	const TArray<FDeepLevelCityLayoutFragment> Fragments = {Road, Building};
	TestTrue(TEXT("Independent fragments merge"), FDeepLevelCityLayoutBuilder::Build(Grid, Fragments, FirstSnapshot, Error));
	if (!TestTrue(TEXT("Builder returns an immutable snapshot"), FirstSnapshot.IsValid()))
	{
		return false;
	}
	const FDeepLevelCityCellState* MergedCell = FirstSnapshot->FindCell(FIntPoint(1, 1));
	if (TestNotNull(TEXT("Overlapping provider cell is present"), MergedCell))
	{
		const int32 Expected = static_cast<int32>(EDeepLevelCityOccupancy::Sidewalk)
			| static_cast<int32>(EDeepLevelCityOccupancy::Building);
		TestEqual(TEXT("Provider occupancy is composed without shared writes"), MergedCell->OccupancyMask, Expected);
	}

	TSet<FIntPoint> InitialDirtyChunks;
	FDeepLevelCityLayoutBuilder::FindDirtyChunks(nullptr, *FirstSnapshot, InitialDirtyChunks);
	TestTrue(TEXT("Initial layout dirties the populated local chunk"), InitialDirtyChunks.Contains(FIntPoint::ZeroValue));
	TestTrue(TEXT("Initial layout dirties the second populated chunk"), InitialDirtyChunks.Contains(FIntPoint(1, 0)));

	Building.Cells.Last().Cell = FIntPoint(9, 0);
	Building.SourceRevision++;
	const TArray<FDeepLevelCityLayoutFragment> UpdatedFragments = {Road, Building};
	TSharedPtr<const FDeepLevelCityLayoutSnapshot> UpdatedSnapshot;
	TestTrue(TEXT("Updated fragments merge"), FDeepLevelCityLayoutBuilder::Build(Grid, UpdatedFragments, UpdatedSnapshot, Error));
	TSet<FIntPoint> DirtyChunks;
	FDeepLevelCityLayoutBuilder::FindDirtyChunks(FirstSnapshot.Get(), *UpdatedSnapshot, DirtyChunks);
	TestTrue(TEXT("Removed occupancy dirties its old chunk"), DirtyChunks.Contains(FIntPoint(1, 0)));
	TestTrue(TEXT("Added occupancy dirties its new chunk"), DirtyChunks.Contains(FIntPoint(2, 0)));
	TestFalse(TEXT("Unchanged road chunk stays clean"), DirtyChunks.Contains(FIntPoint::ZeroValue));

	FDeepLevelCityLayoutFragment DuplicateSource = Road;
	TSharedPtr<const FDeepLevelCityLayoutSnapshot> InvalidSnapshot;
	TestFalse(TEXT("Duplicate source fragments are rejected"), FDeepLevelCityLayoutBuilder::Build(
		Grid, {Road, DuplicateSource}, InvalidSnapshot, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelCityDecorationResolverTest,
	"DeepLevelDesignPCG.Editor.City.DecorationResolver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationResolverTest::RunTest(const FString& Parameters)
{
	FDeepLevelCityGrid Grid{FVector::ZeroVector, 500.0, 4};
	FDeepLevelCityLayoutFragment Road;
	Road.SourceGuid = FGuid(1, 2, 3, 4);
	for (int32 X = 0; X < 2; ++X)
	{
		FDeepLevelCityCellState& Cell = Road.Cells.Emplace_GetRef();
		Cell.Cell = FIntPoint(X, 0);
		Cell.OccupancyMask = static_cast<int32>(EDeepLevelCityOccupancy::Sidewalk);
		Cell.Tags.AddTag(DeepLevelCityTags::Cell_Sidewalk);
		FDeepLevelCityAnchor& Anchor = Road.Anchors.Emplace_GetRef();
		Anchor.StableId = FDeepLevelCityStableId::MakeAnchorId(Road.SourceGuid, FString::Printf(TEXT("Cell:%d:0"), X), TEXT("SidewalkSurface"));
		Anchor.Geometry = EDeepLevelCityAnchorGeometry::Surface;
		Anchor.Transform.SetLocation(Grid.CellToWorld(Cell.Cell));
		Anchor.Tags.AddTag(DeepLevelCityTags::Anchor_Sidewalk_Surface);
		Anchor.OccupiedCells.Add(Cell.Cell);
	}
	FDeepLevelCityLayoutFragment Building;
	Building.SourceGuid = FGuid(5, 6, 7, 8);
	Building.Cells.Add({FIntPoint(1, 0), static_cast<int32>(EDeepLevelCityOccupancy::Building), {}});
	TSharedPtr<const FDeepLevelCityLayoutSnapshot> Snapshot;
	FText Error;
	TestTrue(TEXT("Resolver test snapshot builds"), FDeepLevelCityLayoutBuilder::Build(Grid, {Road, Building}, Snapshot, Error));

	UDeepLevelCityDecorationCategory* Category = NewObject<UDeepLevelCityDecorationCategory>();
	const FGuid LowPriorityGuid(10, 0, 0, 1);
	FDeepLevelCityDecorationEntry& LowPriority = Category->Entries.Emplace_GetRef();
	LowPriority.EntryGuid = LowPriorityGuid;
	LowPriority.Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	LowPriority.RequiredAnchorTags.AddTag(DeepLevelCityTags::Anchor_Sidewalk_Surface);
	LowPriority.RequiredOccupancyMask = static_cast<int32>(EDeepLevelCityOccupancy::Sidewalk);
	LowPriority.ClearanceBlockingOccupancyMask = static_cast<int32>(EDeepLevelCityOccupancy::Building);
	LowPriority.ClearanceRadius = 100.0;
	LowPriority.MinimumSpacing = 600.0;
	LowPriority.Priority = 1;
	FDeepLevelCityDecorationEntry LowPriorityCopy = LowPriority;
	FDeepLevelCityDecorationEntry& HighPriority = Category->Entries.Emplace_GetRef(LowPriorityCopy);
	HighPriority.EntryGuid = FGuid(10, 0, 0, 2);
	HighPriority.Priority = 10;
	const FGuid HighPriorityGuid = HighPriority.EntryGuid;
	FDeepLevelCityDecorationEntry& IndependentSlot = Category->Entries.Emplace_GetRef(LowPriorityCopy);
	IndependentSlot.EntryGuid = FGuid(10, 0, 0, 3);
	IndependentSlot.PlacementSlot = TEXT("Independent");
	UDeepLevelCityDecorationSet* DecorationSet = NewObject<UDeepLevelCityDecorationSet>();
	DecorationSet->Categories.Add(Category);

	TArray<FDeepLevelCityResolvedDecoration> First;
	TArray<FDeepLevelCityResolvedDecoration> Second;
	TestTrue(TEXT("Categorized decoration resolves"), FDeepLevelCityDecorationResolver::Resolve(*Snapshot, *DecorationSet, 77, First, Error));
	TestTrue(TEXT("Categorized decoration resolves deterministically"), FDeepLevelCityDecorationResolver::Resolve(*Snapshot, *DecorationSet, 77, Second, Error));
	TestEqual(TEXT("Physical clearance blocks overlapping solid outputs across placement slots"), First.Num(), 1);
	if (First.Num() == 1)
	{
		TestEqual(TEXT("Higher priority entry claims the shared anchor slot"), First[0].EntryGuid, HighPriorityGuid);
		TestEqual(TEXT("Resolved placement remains on the free sidewalk cell"), First[0].Transform.GetLocation(), Grid.CellToWorld(FIntPoint::ZeroValue));
		TestEqual(TEXT("Stable placement ID survives repeated resolution"), Second[0].StableId, First[0].StableId);

		FDeepLevelCityDecorationOverride Modify;
		Modify.Mode = EDeepLevelCityDecorationOverrideMode::Modify;
		Modify.PlacementId = First[0].StableId;
		Modify.TransformOffset.SetLocation(FVector(0.0, 0.0, 25.0));
		TArray<FDeepLevelCityResolvedDecoration> Modified;
		TestTrue(TEXT("Modify override resolves"), FDeepLevelCityDecorationResolver::Resolve(
			*Snapshot, *DecorationSet, 77, Modified, Error, {Modify}));
		TestEqual(TEXT("Modify override preserves stable ID"), Modified[0].StableId, First[0].StableId);
		TestEqual(TEXT("Modify override applies its transform"), Modified[0].Transform.GetLocation(), FVector(0.0, 0.0, 25.0));

		FDeepLevelCityDecorationOverride Remove;
		Remove.Mode = EDeepLevelCityDecorationOverrideMode::Remove;
		Remove.PlacementId = First[0].StableId;
		TArray<FDeepLevelCityResolvedDecoration> Removed;
		TestTrue(TEXT("Remove override resolves"), FDeepLevelCityDecorationResolver::Resolve(
			*Snapshot, *DecorationSet, 77, Removed, Error, {Remove}));
		TestEqual(TEXT("Remove override removes the targeted placement"), Removed.Num(), 0);

		FDeepLevelCityDecorationOverride Add;
		Add.Mode = EDeepLevelCityDecorationOverrideMode::Add;
		Add.AnchorId = Road.Anchors[0].StableId;
		Add.EntryGuid = LowPriorityGuid;
		TArray<FDeepLevelCityResolvedDecoration> Added;
		TestTrue(TEXT("Add override resolves"), FDeepLevelCityDecorationResolver::Resolve(
			*Snapshot, *DecorationSet, 77, Added, Error, {Add}));
		TestEqual(TEXT("Add override appends the explicitly selected catalog entry"), Added.Num(), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelCityStreetCadenceTest,
	"DeepLevelDesignPCG.Editor.City.StreetCadence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityStreetCadenceTest::RunTest(const FString& Parameters)
{
	FDeepLevelCityGrid Grid{FVector(500.0, 3000.0, 0.0), 500.0, 4};
	UDeepLevelCityDecorationCategory* Category = NewObject<UDeepLevelCityDecorationCategory>();
	FDeepLevelCityDecorationEntry& Lamp = Category->Entries.Emplace_GetRef();
	Lamp.EntryGuid = FGuid(42, 1, 1, 1);
	Lamp.Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Lamp.AnchorInterval = 8;
	Lamp.AnchorPhase = 1;
	Lamp.bStaggerOppositeEdges = true;
	Lamp.MinimumSpacing = 1000.0;
	Lamp.ClearanceRadius = 50.0;
	UDeepLevelCityDecorationSet* Set = NewObject<UDeepLevelCityDecorationSet>();
	Set->Categories.Add(Category);
	FText Error;
	for (const bool bAlongX : {true, false})
	{
		FDeepLevelCityLayoutFragment Road;
		Road.SourceGuid = FGuid(42, 2, 2, 2);
		for (int32 Coordinate = -16; Coordinate < 16; ++Coordinate)
		{
			for (const int32 Side : {-1, 1})
			{
				const FIntPoint Cell = bAlongX ? FIntPoint(Coordinate, Side) : FIntPoint(Side, Coordinate);
				Road.Cells.Add({Cell, static_cast<int32>(EDeepLevelCityOccupancy::Sidewalk), {}});
				FDeepLevelCityAnchor& Anchor = Road.Anchors.Emplace_GetRef();
				Anchor.StableId = FDeepLevelCityStableId::MakeAnchorId(Road.SourceGuid,
					FString::Printf(TEXT("%d:%d"), Cell.X, Cell.Y), TEXT("Edge"));
				Anchor.Geometry = EDeepLevelCityAnchorGeometry::Segment;
				Anchor.OccupiedCells.Add(Cell);
				Anchor.Transform = FTransform(FRotator(0.0, (bAlongX ? 0.0 : 90.0) + (Side < 0 ? 180.0 : 0.0), 0.0), Grid.CellToWorld(Cell));
			}
		}
		TSharedPtr<const FDeepLevelCityLayoutSnapshot> Snapshot;
		if (!TestTrue(TEXT("Street snapshot builds"), FDeepLevelCityLayoutBuilder::Build(Grid, {Road}, Snapshot, Error)))
		{
			return false;
		}
		TArray<FDeepLevelCityResolvedDecoration> First;
		TestTrue(TEXT("Staggered street resolves"), FDeepLevelCityDecorationResolver::Resolve(*Snapshot, *Set, 77, First, Error));
		TestEqual(TEXT("Both curbs retain four lamps without opposite-side spacing holes"), First.Num(), 8);
		for (const FDeepLevelCityResolvedDecoration& Placement : First)
		{
			const FIntPoint Cell = Grid.WorldToCell(Placement.Transform.GetLocation());
			const int32 Coordinate = bAlongX ? Cell.X : Cell.Y;
			const int32 Side = bAlongX ? Cell.Y : Cell.X;
			TestEqual(TEXT("Opposite curb is offset by twenty meters, including negative cells"),
				((Coordinate % 8) + 8) % 8, Side < 0 ? 5 : 1);
		}
		Lamp.EntryGuid = FGuid(42, 9, 9, 9);
		TArray<FDeepLevelCityResolvedDecoration> Second;
		TestTrue(TEXT("Different seed and asset identity preserve authored street rhythm"),
			FDeepLevelCityDecorationResolver::Resolve(*Snapshot, *Set, 1234, Second, Error));
		TestEqual(TEXT("Same coverage after seed change"), Second.Num(), First.Num());
		for (const FDeepLevelCityResolvedDecoration& Placement : First)
		{
			TestTrue(TEXT("No seed-driven lamp movement"), Second.ContainsByPredicate([&Placement](const auto& Other)
			{
				return Other.Transform.GetLocation().Equals(Placement.Transform.GetLocation());
			}));
		}
	}
	Lamp.AnchorInterval = 7;
	TestFalse(TEXT("Uneven stagger cadence is rejected"), Category->Validate(Error));
	return true;
}

#endif
