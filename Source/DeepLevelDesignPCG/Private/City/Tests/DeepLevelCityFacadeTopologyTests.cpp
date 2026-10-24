// Copyright <--\, Inc. All Rights Reserved.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "City/DeepLevelCityLayout.h"
#include "City/Decoration/DeepLevelCityBuildingDecorationProfile.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "PackedLevelActor/PackedLevelActor.h"

namespace
{
FDeepLevelCityBuilding TopologyBuilding(int32 Id, const FVector& Location, const FVector& Extent)
{
	FDeepLevelCityBuilding B;
	B.StableId = FGuid(Id, 2, 3, 4);
	B.BuildingClass = APackedLevelActor::StaticClass();
	B.Transform.SetLocation(Location);
	B.FacadeGeometry.Extent = Extent;
	return B;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityFacadeTopologyDirtyTest,
	"DeepLevelDesignPCG.Editor.City.FacadeTopology.GlobalInvalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityFacadeTopologyDirtyTest::RunTest(const FString&)
{
	FDeepLevelCityGrid Grid;
	Grid.TileSize = 100; Grid.ChunkSizeInCells = 2;
	FDeepLevelCityLayoutFragment East, West;
	East.SourceGuid = FGuid(10, 1, 1, 1); West.SourceGuid = FGuid(20, 1, 1, 1);
	East.Buildings = {TopologyBuilding(1, FVector(0, 400, 0), FVector(500, 100, 100)),
		TopologyBuilding(2, FVector(0, -400, 0), FVector(500, 100, 100)),
		TopologyBuilding(3, FVector(400, 0, 0), FVector(100, 300, 100))};
	West.Buildings = {TopologyBuilding(4, FVector(-400, 0, 0), FVector(100, 300, 100))};
	FText Error;
	TSharedPtr<const FDeepLevelCityLayoutSnapshot> Closed, Open;
	if (!TestTrue(TEXT("Independent providers form one courtyard topology"), FDeepLevelCityLayoutBuilder::Build(Grid, {East, West}, Closed, Error))) { return false; }
	West.Buildings.Reset();
	if (!TestTrue(TEXT("Removing another provider's wall opens the courtyard"), FDeepLevelCityLayoutBuilder::Build(Grid, {East, West}, Open, Error))) { return false; }
	const FGuid Id = East.Buildings[2].StableId;
	const auto* Before = Closed->GetBuildings().FindByPredicate([&](const auto& B) { return B.StableId == Id; });
	const auto* After = Open->GetBuildings().FindByPredicate([&](const auto& B) { return B.StableId == Id; });
	if (!TestNotNull(TEXT("Existing east building retained"), Before) || !TestNotNull(TEXT("Opened east building retained"), After)) { return false; }
	TestEqual(TEXT("Local source revision did not change"), After->SourceRevision, Before->SourceRevision);
	TestTrue(TEXT("Enclosed rear point starts hidden"), !DeepLevelBuildingFacades::IsPointVisible(*Before, EDeepLevelBuildingVolumeFace::NegativeX, FVector(-100, 0, 0)));
	TestTrue(TEXT("Rear point becomes visible through the opening"), DeepLevelBuildingFacades::IsPointVisible(*After, EDeepLevelBuildingVolumeFace::NegativeX, FVector(-100, 0, 0)));
	TSet<FIntPoint> Dirty;
	FDeepLevelCityLayoutBuilder::FindDirtyChunks(Closed.Get(), *Open, Dirty);
	TestTrue(TEXT("Remote topology change dirties unchanged east building"), Dirty.Contains(Grid.CellToChunk(Grid.WorldToCell(After->Transform.GetLocation()))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityFacadeTopologySelectionTest,
	"DeepLevelDesignPCG.Editor.City.FacadeTopology.SameVariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityFacadeTopologySelectionTest::RunTest(const FString&)
{
	auto* Profile = NewObject<UDeepLevelCityBuildingDecorationProfile>();
	Profile->BuildingClass = APackedLevelActor::StaticClass();
	auto& Variant = Profile->Variants.Emplace_GetRef();
	Variant.VariantGuid = FGuid(30, 1, 1, 1);
	for (int32 Side : {-1, 1})
	{
		auto& Entry = Variant.Entries.Emplace_GetRef();
		Entry.EntryGuid = FGuid(Side + 2, 5, 6, 7);
		Entry.Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		Entry.LocalTransform.SetLocation(FVector(Side * 100, 0, 0));
	}
	auto* Set = NewObject<UDeepLevelCityDecorationSet>();
	Set->BuildingProfiles.Add(Profile);
	FDeepLevelCityLayoutFragment Fragment;
	Fragment.SourceGuid = FGuid(40, 1, 1, 1);
	Fragment.Buildings = {TopologyBuilding(1, FVector::ZeroVector, FVector(100))};
	FText Error;
	TSharedPtr<const FDeepLevelCityLayoutSnapshot> Snapshot;
	if (!TestTrue(TEXT("Isolated profile snapshot builds"), FDeepLevelCityLayoutBuilder::Build({}, {Fragment}, Snapshot, Error))) { return false; }
	TArray<FDeepLevelCityResolvedDecoration> Before, After;
	if (!TestTrue(TEXT("Isolated variant resolves"), FDeepLevelCityDecorationResolver::Resolve(*Snapshot, *Set, 7, Before, Error))) { return false; }
	TestEqual(TEXT("Both sides initially exist"), Before.Num(), 2);
	auto Neighbor = TopologyBuilding(2, FVector(200, 0, 0), FVector(100));
	Neighbor.BuildingClass = AActor::StaticClass();
	Fragment.Buildings.Add(Neighbor);
	if (!TestTrue(TEXT("Neighbor snapshot builds"), FDeepLevelCityLayoutBuilder::Build({}, {Fragment}, Snapshot, Error))) { return false; }
	if (!TestTrue(TEXT("Same variant resolves against neighbor visibility"), FDeepLevelCityDecorationResolver::Resolve(*Snapshot, *Set, 7, After, Error))) { return false; }
	if (!TestEqual(TEXT("Only the covered-side entry disappears"), After.Num(), 1)) { return false; }
	TestEqual(TEXT("Visible entry belongs to the original variant"), After[0].VariantId, Variant.VariantGuid);
	TestEqual(TEXT("Exposed negative X entry remains"), After[0].EntryGuid, Variant.Entries[0].EntryGuid);
	TestTrue(TEXT("Retained placement keeps its identity"), Before.ContainsByPredicate([&](const auto& B) { return B.StableId == After[0].StableId; }));
	return true;
}
#endif
