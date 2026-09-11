// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Road/DeepLevelRoadPCG.h"
#include "City/DeepLevelCityDecoration.h"

#include "Components/DecalComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"

namespace DeepLevelRoadLayoutFragmentTests
{
	class FWorldScope
	{
	public:
		explicit FWorldScope(FAutomationTestBase& Test)
		{
			if (!GEngine)
			{
				Test.AddError(TEXT("GEngine is unavailable."));
				return;
			}
			GameInstance.Reset(NewObject<UGameInstance>(GEngine));
			GameInstance->InitializeStandalone(TEXT("RoadLayoutFragmentWorld"));
		}

		~FWorldScope()
		{
			if (!GameInstance.IsValid())
			{
				return;
			}
			UWorld* World = GameInstance->GetWorld();
			GameInstance->Shutdown();
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}

		UWorld* GetWorld() const { return GameInstance.IsValid() ? GameInstance->GetWorld() : nullptr; }

	private:
		TStrongObjectPtr<UGameInstance> GameInstance;
	};

	FDeepLevelRoadTileDefinition MakeTile(const int32 Connections = 0, const int32 Approach = 0)
	{
		FDeepLevelRoadTileDefinition Tile;
		Tile.TileMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		Tile.ConnectionMask = Connections;
		Tile.ApproachJunctionDirectionMask = Approach;
		Tile.PlacementVolume.Extent = FVector(250.0, 250.0, 50.0);
		Tile.bCalibrated = true;
		return Tile;
	}

	UDeepLevelRoadTileCatalog* MakeCatalog(UObject* Outer, UDeepLevelCityGridProfile* GridProfile)
	{
		constexpr int32 PX = static_cast<int32>(EDeepLevelRoadConnection::PositiveX);
		constexpr int32 PY = static_cast<int32>(EDeepLevelRoadConnection::PositiveY);
		constexpr int32 NX = static_cast<int32>(EDeepLevelRoadConnection::NegativeX);
		constexpr int32 NY = static_cast<int32>(EDeepLevelRoadConnection::NegativeY);
		UDeepLevelRoadTileCatalog* Catalog = NewObject<UDeepLevelRoadTileCatalog>(Outer);
		Catalog->GridProfile = GridProfile;
		Catalog->Tiles = {
			MakeTile(PX), MakeTile(PX | NX), MakeTile(PX | PY), MakeTile(PX | PY | NX),
			MakeTile(PX | PY | NX | NY), MakeTile(PX | NX, PX), MakeTile()};
		return Catalog;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelRoadLayoutFragmentTest,
	"DeepLevelDesignPCG.Editor.RoadNetwork.LayoutFragment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelRoadLayoutFragmentTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelRoadLayoutFragmentTests;
	FWorldScope WorldScope(*this);
	UWorld* World = WorldScope.GetWorld();
	if (!TestNotNull(TEXT("Automation world exists"), World))
	{
		return false;
	}

	ADeepLevelCityLayoutActor* CityLayout = World->SpawnActor<ADeepLevelCityLayoutActor>();
	CityLayout->GridProfile = NewObject<UDeepLevelCityGridProfile>(CityLayout);
	CityLayout->SetActorLocation(FVector(100.0, 200.0, 300.0));
	ADeepLevelRoadNetworkActor* Network = World->SpawnActor<ADeepLevelRoadNetworkActor>();
	Network->CityLayout = CityLayout;
	Network->Catalog = MakeCatalog(Network, CityLayout->GridProfile);
	UDeepLevelRoadSplineComponent* Spline = Network->CreateRoadBranch();
	Spline->SetRoadPathFromWorldPoints({FVector(100.0, 200.0, 300.0), FVector(1100.0, 200.0, 300.0)});

	FDeepLevelCityGrid Grid;
	FText Error;
	if (!TestTrue(TEXT("City grid resolves"), CityLayout->ResolveGrid(Grid, Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	FDeepLevelCityLayoutFragment First;
	FDeepLevelCityLayoutFragment Second;
	TestTrue(TEXT("Road fragment builds"), Network->BuildCityLayoutFragment(Grid, First, Error));
	TestTrue(TEXT("Road fragment rebuilds"), Network->BuildCityLayoutFragment(Grid, Second, Error));
	TestTrue(TEXT("Road fragment has a stable source identity"), First.SourceGuid.IsValid());
	TestTrue(TEXT("Road fragment contains road and sidewalk occupancy"), First.Cells.Num() > 3);
	const int32 SurfaceAnchorCount = First.Anchors.FilterByPredicate([](const FDeepLevelCityAnchor& Anchor)
	{
		return Anchor.Tags.HasTagExact(DeepLevelCityTags::Anchor_Road_Surface)
			|| Anchor.Tags.HasTagExact(DeepLevelCityTags::Anchor_Sidewalk_Surface);
	}).Num();
	TestEqual(TEXT("Every occupied road cell has one surface anchor"), SurfaceAnchorCount, First.Cells.Num());
	TestTrue(TEXT("Road fragment adds oriented sidewalk edge anchors"), First.Anchors.ContainsByPredicate([](const FDeepLevelCityAnchor& Anchor)
	{
		return Anchor.Tags.HasTagExact(DeepLevelCityTags::Anchor_Sidewalk_Edge);
	}));
	TestTrue(TEXT("Sidewalk edge anchors classify local roads"), First.Anchors.ContainsByPredicate([](const FDeepLevelCityAnchor& Anchor)
	{
		return Anchor.Tags.HasTagExact(DeepLevelCityTags::Anchor_Sidewalk_Edge)
			&& Anchor.Tags.HasTagExact(DeepLevelCityTags::Anchor_Road_Local);
	}));
	TestTrue(TEXT("Sidewalk edge anchors expose usable sidewalk depth"), First.Anchors.ContainsByPredicate([](const FDeepLevelCityAnchor& Anchor)
	{
		return Anchor.Tags.HasTagExact(DeepLevelCityTags::Anchor_Sidewalk_Edge) && Anchor.ClearanceDepth > 0.0;
	}));
	TestTrue(TEXT("Road endpoints expose dead-end anchors"), First.Anchors.ContainsByPredicate([](const FDeepLevelCityAnchor& Anchor)
	{
		return Anchor.Tags.HasTagExact(DeepLevelCityTags::Anchor_Road_DeadEnd);
	}));
	TestEqual(TEXT("Repeated fragments preserve anchor count"), Second.Anchors.Num(), First.Anchors.Num());
	for (int32 Index = 0; Index < First.Anchors.Num(); ++Index)
	{
		TestEqual(TEXT("Repeated fragments preserve stable anchor IDs"), Second.Anchors[Index].StableId, First.Anchors[Index].StableId);
		TestEqual(TEXT("Surface anchor references its grid cell"), First.Anchors[Index].OccupiedCells.Num(), 1);
		if (First.Anchors[Index].Tags.HasTagExact(DeepLevelCityTags::Anchor_Road_Surface)
			|| First.Anchors[Index].Tags.HasTagExact(DeepLevelCityTags::Anchor_Sidewalk_Surface))
		{
			TestEqual(TEXT("Surface anchor remains centered on its shared grid cell"),
				FVector2D(First.Anchors[Index].Transform.GetLocation()),
				FVector2D(Grid.CellToWorld(First.Anchors[Index].OccupiedCells[0])));
			TestTrue(TEXT("Surface anchor uses the calibrated tile support height"),
				First.Anchors[Index].Transform.GetLocation().Z > Grid.Origin.Z);
		}
	}
	CityLayout->LayoutProviders.Add(Network);
	TSet<FIntPoint> DirtyChunks;
	TestTrue(TEXT("City Layout composes registered providers"), CityLayout->RebuildSnapshot(DirtyChunks, Error));
	TestTrue(TEXT("Initial composition reports populated dirty chunks"), !DirtyChunks.IsEmpty());
	TestTrue(TEXT("City Layout owns the immutable composed snapshot"), CityLayout->GetSnapshot().IsValid());
	TestTrue(TEXT("Unchanged composition rebuilds"), CityLayout->RebuildSnapshot(DirtyChunks, Error));
	TestTrue(TEXT("Unchanged composition produces no dirty chunks"), DirtyChunks.IsEmpty());

	UDeepLevelCityDecorationCategory* Category = NewObject<UDeepLevelCityDecorationCategory>(CityLayout);
	FDeepLevelCityDecorationEntry& MeshEntry = Category->Entries.Emplace_GetRef();
	MeshEntry.EntryGuid = FGuid(10, 20, 30, 1);
	MeshEntry.Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	MeshEntry.RequiredAnchorTags.AddTag(DeepLevelCityTags::Anchor_Sidewalk_Surface);
	MeshEntry.PlacementSlot = TEXT("Mesh");
	FDeepLevelCityDecorationEntry& ActorEntry = Category->Entries.Emplace_GetRef();
	ActorEntry.EntryGuid = FGuid(10, 20, 30, 2);
	ActorEntry.Output = EDeepLevelCityDecorationOutput::Actor;
	ActorEntry.ActorClass = AStaticMeshActor::StaticClass();
	ActorEntry.RequiredAnchorTags.AddTag(DeepLevelCityTags::Anchor_Sidewalk_Surface);
	ActorEntry.PlacementSlot = TEXT("Actor");
	FDeepLevelCityDecorationEntry& DecalEntry = Category->Entries.Emplace_GetRef();
	DecalEntry.EntryGuid = FGuid(10, 20, 30, 3);
	DecalEntry.Output = EDeepLevelCityDecorationOutput::Decal;
	DecalEntry.DecalMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
	DecalEntry.RequiredAnchorTags.AddTag(DeepLevelCityTags::Anchor_Sidewalk_Surface);
	DecalEntry.PlacementSlot = TEXT("Decal");
	CityLayout->DecorationSet = NewObject<UDeepLevelCityDecorationSet>(CityLayout);
	CityLayout->DecorationSet->Categories.Add(Category);
	TestTrue(TEXT("City decoration materializes all output adapters"), CityLayout->DecorationComponent->Regenerate(true, Error));
	TestTrue(TEXT("Materializer exposes placement diagnostics"), CityLayout->DecorationComponent->LastPlacementCount > 0);
	TestTrue(TEXT("Materializer exposes regenerated chunks"), !CityLayout->DecorationComponent->LastDirtyChunks.IsEmpty());
	TInlineComponentArray<UHierarchicalInstancedStaticMeshComponent*> HISMs(CityLayout);
	TInlineComponentArray<UDecalComponent*> Decals(CityLayout);
	TestTrue(TEXT("Mesh output uses chunk-owned HISM components"), HISMs.ContainsByPredicate(
		[](const UHierarchicalInstancedStaticMeshComponent* HISM) { return HISM && HISM->GetInstanceCount() > 0; }));
	TestTrue(TEXT("Decal output creates chunk-owned decal components"), !Decals.IsEmpty());
	TInlineComponentArray<UChildActorComponent*> ChildActors(CityLayout);
	TestTrue(TEXT("Actor output creates a persistent City Layout-owned child actor component"), ChildActors.ContainsByPredicate(
		[](const UChildActorComponent* Component) { return Component && Component->GetChildActor(); }));
	TestTrue(TEXT("Generated HISM output is serializable"), HISMs.ContainsByPredicate(
		[](const UHierarchicalInstancedStaticMeshComponent* HISM)
		{
			return HISM && !HISM->HasAnyFlags(RF_Transient) && HISM->CreationMethod == EComponentCreationMethod::Instance;
		}));
	TestTrue(TEXT("Generated decal output is serializable"), Decals.ContainsByPredicate(
		[](const UDecalComponent* Decal)
		{
			return Decal && !Decal->HasAnyFlags(RF_Transient) && Decal->CreationMethod == EComponentCreationMethod::Instance;
		}));
	const int32 InstanceComponentCount = CityLayout->GetInstanceComponents().Num();
	TestTrue(TEXT("Regeneration replaces saved outputs without duplication"), CityLayout->DecorationComponent->Regenerate(true, Error));
	TestEqual(TEXT("Regeneration preserves the generated component count"), CityLayout->GetInstanceComponents().Num(), InstanceComponentCount);
	return true;
}

#endif
