// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "City/DeepLevelCityDecoration.h"
#include "Road/DeepLevelRoadPCG.h"
#include "Components/ChildActorComponent.h"
#include "Components/DecalComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace DeepLevelCityDecorationRegenerationTests
{
	class FWorldScope
	{
	public:
		FWorldScope()
		{
			GameInstance.Reset(NewObject<UGameInstance>(GEngine));
			GameInstance->InitializeStandalone(TEXT("CityDecorationRegenerationWorld"));
		}
		~FWorldScope()
		{
			UWorld* World = GameInstance->GetWorld();
			GameInstance->Shutdown();
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}
		UWorld* GetWorld() const { return GameInstance->GetWorld(); }
	private:
		TStrongObjectPtr<UGameInstance> GameInstance;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationRegenerationTest,
	"DeepLevelDesignPCG.Editor.City.DecorationOutputRegeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationRegenerationTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Engine exists"), GEngine)) { return false; }
	DeepLevelCityDecorationRegenerationTests::FWorldScope Scope;
	UWorld* World = Scope.GetWorld();
	if (!TestNotNull(TEXT("Automation world exists"), World)) { return false; }
	auto* Layout = World->SpawnActor<ADeepLevelCityLayoutActor>();
	Layout->GridProfile = NewObject<UDeepLevelCityGridProfile>(Layout);
	Layout->GridProfile->ChunkSizeInCells = 4;
	Layout->DecorationSet = NewObject<UDeepLevelCityDecorationSet>(Layout);
	auto* Road = World->SpawnActor<ADeepLevelRoadNetworkActor>();
	Road->CityLayout = Layout;
	Road->Catalog = NewObject<UDeepLevelRoadTileCatalog>(Road);
	for (const int32 Mask : {1, 5, 3, 7, 15, 0})
	{
		FDeepLevelRoadTileDefinition Tile;
		Tile.ConnectionMask = Mask;
		Tile.TileMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		Tile.PlacementVolume.Extent = FVector(250.0, 250.0, 50.0);
		Tile.bCalibrated = true;
		Road->Catalog->Tiles.Add(Tile);
	}
	FDeepLevelRoadTileDefinition ApproachTile = Road->Catalog->Tiles[1];
	ApproachTile.ApproachJunctionDirectionMask = static_cast<int32>(EDeepLevelRoadConnection::PositiveX);
	Road->Catalog->Tiles.Add(ApproachTile);
	UDeepLevelRoadSplineComponent* Spline = Road->CreateRoadBranch();
	Spline->SetRoadPathFromWorldPoints({FVector(0.0, 0.0, 0.0), FVector(1000.0, 0.0, 0.0)});
	Layout->RegisterLayoutSource(*Road);
	FText Error;
	TSet<FIntPoint> SnapshotDirty;
	if (!TestTrue(TEXT("Road snapshot refreshes"), Layout->RefreshSnapshot(SnapshotDirty, Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	auto* Category = NewObject<UDeepLevelCityDecorationCategory>(Layout->DecorationSet);
	FDeepLevelCityDecorationEntry Mesh;
	Mesh.EntryGuid = FGuid(20, 1, 1, 1);
	Mesh.Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Mesh.RequiredAnchorTags.AddTag(DeepLevelCityTags::Anchor_Road_Surface);
	Mesh.LocalTransform.SetLocation(FVector(3000.0, 0.0, 0.0));
	Category->Entries.Add(Mesh);
	FDeepLevelCityDecorationEntry Light = Mesh;
	Light.EntryGuid = FGuid(21, 1, 1, 1);
	Light.Output = EDeepLevelCityDecorationOutput::Actor;
	Light.ActorClass = APointLight::StaticClass();
	Light.PlacementSlot = TEXT("Light");
	Category->Entries.Add(Light);
	FDeepLevelCityDecorationEntry Decal = Mesh;
	Decal.EntryGuid = FGuid(22, 1, 1, 1);
	Decal.Output = EDeepLevelCityDecorationOutput::Decal;
	Decal.DecalMaterial = UMaterial::GetDefaultMaterial(MD_DeferredDecal);
	Decal.PlacementSlot = TEXT("Decal");
	Category->Entries.Add(Decal);
	Layout->DecorationSet->Categories.Add(Category);
	auto* Component = Layout->DecorationComponent.Get();
	if (!TestTrue(TEXT("All output adapters generate"), Component->Regenerate(false, Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	const int32 InitialCount = Component->LastPlacementCount;
	TestTrue(TEXT("Decorations were materialized"), InitialCount > 0);
	const TArray<FIntPoint> InitialChunks = Component->LastDirtyChunks;
	TArray<UHierarchicalInstancedStaticMeshComponent*> Meshes;
	TArray<UChildActorComponent*> Actors;
	TArray<UDecalComponent*> Decals;
	Layout->GetComponents(Meshes);
	Layout->GetComponents(Actors);
	Layout->GetComponents(Decals);
	TestTrue(TEXT("HISM mesh adapter is shared"), Meshes.Num() > 0);
	TestTrue(TEXT("Actor adapter creates light actors"), Actors.Num() > 0);
	TestTrue(TEXT("Decal adapter creates decal components"), Decals.Num() > 0);
	if (Meshes.IsEmpty()) { return false; }
	const TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> OriginalMesh = Meshes[0];
	TestTrue(TEXT("Unchanged outputs regenerate successfully"), Component->Regenerate(false, Error));
	TestEqual(TEXT("Unchanged output chunks stay clean"), Component->LastDirtyChunks.Num(), 0);
	TestTrue(TEXT("Unchanged output component remains owned"), OriginalMesh.IsValid());

	for (auto& Entry : Category->Entries) { Entry.LocalTransform.AddToTranslation(FVector(0.0, 5000.0, 0.0)); }
	TestTrue(TEXT("Offsets outside original geometry regenerate"), Component->Regenerate(false, Error));
	TestEqual(TEXT("Moved outputs do not duplicate"), Component->LastPlacementCount, InitialCount);
	for (const FIntPoint& Chunk : InitialChunks)
	{
		TestTrue(TEXT("Old decoration chunks are cleaned despite unchanged snapshot"), Component->LastDirtyChunks.Contains(Chunk));
	}
	TestFalse(TEXT("Old component is removed from owner"), Layout->GetInstanceComponents().Contains(OriginalMesh.Get()));
	Category->Entries[0].Mesh.Reset();
	TestFalse(TEXT("Invalid data is rejected before clearing outputs"), Component->Regenerate(false, Error));
	Meshes.Reset();
	Layout->GetComponents(Meshes);
	TestTrue(TEXT("Validation failure preserves materialized meshes"), Meshes.Num() > 0);
	Layout->DecorationSet->Categories.Reset();
	TestTrue(TEXT("Removing definitions clears previous chunks"), Component->Regenerate(false, Error));
	TestEqual(TEXT("No stale outputs remain"), Component->LastPlacementCount, 0);
	Meshes.Reset(); Actors.Reset(); Decals.Reset();
	Layout->GetComponents(Meshes);
	Layout->GetComponents(Actors);
	Layout->GetComponents(Decals);
	TestEqual(TEXT("Mesh cleanup removes components"), Meshes.Num(), 0);
	TestEqual(TEXT("Actor cleanup removes components"), Actors.Num(), 0);
	TestEqual(TEXT("Decal cleanup removes components"), Decals.Num(), 0);
	return true;
}

#endif
