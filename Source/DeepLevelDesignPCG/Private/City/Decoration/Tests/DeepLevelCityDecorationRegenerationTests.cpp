// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "City/DeepLevelCityDecoration.h"
#include "City/Decoration/DeepLevelCityBuildingDecorationProfile.h"
#include "Building/DeepLevelBuildingPCG.h"
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
#include "PackedLevelActor/PackedLevelActor.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityFacadeOutputRegenerationTest,
	"DeepLevelDesignPCG.Editor.City.DecorationFacadeOutputRegeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityFacadeOutputRegenerationTest::RunTest(const FString&)
{
	if (!TestNotNull(TEXT("Engine exists"), GEngine)) { return false; }
	DeepLevelCityDecorationRegenerationTests::FWorldScope Scope;
	UWorld* World = Scope.GetWorld();
	if (!TestNotNull(TEXT("Automation world exists"), World)) { return false; }
	ADeepLevelCityLayoutActor* Layout = World->SpawnActor<ADeepLevelCityLayoutActor>();
	Layout->GridProfile = NewObject<UDeepLevelCityGridProfile>(Layout);
	Layout->GridProfile->TileSize = 100.0;
	Layout->DecorationSet = NewObject<UDeepLevelCityDecorationSet>(Layout);
	TStrongObjectPtr<UDeepLevelBuildingPlacementCatalog> Catalog(NewObject<UDeepLevelBuildingPlacementCatalog>());
	FDeepLevelBuildingPlacementDefinition Definition;
	Definition.BuildingClass = APackedLevelActor::StaticClass();
	Definition.PlacementVolume.Extent = FVector(200.0, 100.0, 100.0);
	Definition.bCalibrated = true;
	Catalog->Buildings.Add(Definition);
	ADeepLevelPCGBuildingLineActor* Provider = World->SpawnActor<ADeepLevelPCGBuildingLineActor>();
	Provider->CityLayout = Layout;
	Provider->Catalog = Catalog.Get();
	Provider->BuildingLine->ClearSplinePoints(false);
	Provider->BuildingLine->AddSplinePoint(FVector(-200.0, 0.0, 0.0), ESplineCoordinateSpace::Local, false);
	Provider->BuildingLine->AddSplinePoint(FVector(200.0, 0.0, 0.0), ESplineCoordinateSpace::Local, true);
	Layout->RegisterLayoutSource(*Provider);

	TStrongObjectPtr<UDeepLevelCityBuildingDecorationProfile> Profile(NewObject<UDeepLevelCityBuildingDecorationProfile>(Layout));
	Profile->BuildingClass = APackedLevelActor::StaticClass();
	FDeepLevelCityBuildingDecorationVariant& Variant = Profile->Variants.Emplace_GetRef();
	Variant.VariantGuid = FGuid(70, 1, 1, 1);
	const FGuid FrontIds[] = {FGuid(71, 1, 1, 1), FGuid(73, 1, 1, 1), FGuid(75, 1, 1, 1)};
	const FGuid SideIds[] = {FGuid(72, 1, 1, 1), FGuid(74, 1, 1, 1), FGuid(76, 1, 1, 1)};
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	for (int32 OutputIndex = 0; OutputIndex < 3; ++OutputIndex)
	{
		for (int32 FaceIndex = 0; FaceIndex < 2; ++FaceIndex)
		{
			FDeepLevelCityBuildingDecorationEntry& Entry = Variant.Entries.Emplace_GetRef();
			Entry.EntryGuid = FaceIndex == 0 ? FrontIds[OutputIndex] : SideIds[OutputIndex];
			Entry.Output = static_cast<EDeepLevelCityDecorationOutput>(OutputIndex);
			Entry.LocalTransform.SetLocation(FaceIndex == 0 ? FVector(0.0, -100.0, 0.0) : FVector(200.0, 0.0, 0.0));
			if (Entry.Output == EDeepLevelCityDecorationOutput::Mesh) { Entry.Mesh = Cube; }
			else if (Entry.Output == EDeepLevelCityDecorationOutput::Actor) { Entry.ActorClass = APointLight::StaticClass(); }
			else { Entry.DecalMaterial = UMaterial::GetDefaultMaterial(MD_DeferredDecal); }
		}
	}
	Layout->DecorationSet->BuildingProfiles.Add(Profile.Get());

	FStructProperty* PublishedFragmentProperty = FindFProperty<FStructProperty>(Provider->GetClass(), TEXT("GeneratedFragment"));
	if (!TestNotNull(TEXT("Building provider publication fragment exists"), PublishedFragmentProperty)) { return false; }
	auto* PublishedFragment = PublishedFragmentProperty->ContainerPtrToValuePtr<FDeepLevelCityLayoutFragment>(Provider);
	PublishedFragment->Buildings.Reset();
	FDeepLevelCityBuilding& Target = PublishedFragment->Buildings.Emplace_GetRef();
	Target.StableId = FGuid(80, 1, 1, 1);
	Target.BuildingClass = APackedLevelActor::StaticClass();
	Target.Transform = FTransform::Identity;
	Provider->bOutputCurrent = true;

	FText Error;
	TSet<FIntPoint> Dirty;
	if (!TestTrue(TEXT("Initial provider snapshot refreshes"), Layout->RefreshSnapshot(Dirty, Error))) { AddError(Error.ToString()); return false; }
	if (!TestTrue(TEXT("Initial facade outputs materialize"), Layout->DecorationComponent->Regenerate(false, Error))) { AddError(Error.ToString()); return false; }
	TArray<FDeepLevelCityResolvedDecoration> Before;
	if (!TestTrue(TEXT("Initial same-variant placements resolve"), FDeepLevelCityDecorationResolver::Resolve(
		*Layout->GetSnapshot(), *Layout->DecorationSet, Layout->DecorationSeed, Before, Error))) { AddError(Error.ToString()); return false; }
	const FGuid TargetId = Target.StableId;
	TArray<FGuid> RetainedFrontIds;
	TArray<FGuid> RemovedSideIds;
	for (const auto& Placement : Before)
	{
		if (Placement.BuildingId != TargetId) { continue; }
		if (Placement.VariantId != Variant.VariantGuid) { AddError(TEXT("Target switched variants during initial resolve.")); return false; }
		if (Placement.EntryGuid == FrontIds[0] || Placement.EntryGuid == FrontIds[1] || Placement.EntryGuid == FrontIds[2])
			RetainedFrontIds.Add(Placement.StableId);
		if (Placement.EntryGuid == SideIds[0] || Placement.EntryGuid == SideIds[1] || Placement.EntryGuid == SideIds[2])
			RemovedSideIds.Add(Placement.StableId);
	}
	if (!TestEqual(TEXT("One variant initially emits front and side for all output types"), RetainedFrontIds.Num() + RemovedSideIds.Num(), 6)) { return false; }
	if (!TestEqual(TEXT("Initial mesh instances include front and side"), [&] { TArray<UHierarchicalInstancedStaticMeshComponent*> C; Layout->GetComponents(C); int32 N = 0; for (const auto* X : C) { N += X->GetInstanceCount(); } return N; }(), 2)) { return false; }
	{
		TArray<UChildActorComponent*> C; Layout->GetComponents(C);
		if (!TestEqual(TEXT("Initial actor outputs include front and side"), C.Num(), 2)) { return false; }
	}
	{
		TArray<UDecalComponent*> C; Layout->GetComponents(C);
		if (!TestEqual(TEXT("Initial decals include front and side"), C.Num(), 2)) { return false; }
	}
	FDeepLevelCityBuilding& Neighbor = PublishedFragment->Buildings.Emplace_GetRef();
	Neighbor.StableId = FGuid(81, 1, 1, 1);
	Neighbor.BuildingClass = APackedLevelActor::StaticClass();
	Neighbor.Transform.SetLocation(FVector(400.0, 0.0, 0.0));
	if (!TestTrue(TEXT("Neighbor provider snapshot refreshes"), Layout->RefreshSnapshot(Dirty, Error))) { AddError(Error.ToString()); return false; }
	if (!TestTrue(TEXT("Topology change regenerates facade outputs"), Layout->DecorationComponent->Regenerate(false, Error))) { AddError(Error.ToString()); return false; }
	TArray<FDeepLevelCityResolvedDecoration> After;
	if (!TestTrue(TEXT("Updated same-variant placements resolve"), FDeepLevelCityDecorationResolver::Resolve(
		*Layout->GetSnapshot(), *Layout->DecorationSet, Layout->DecorationSeed, After, Error))) { AddError(Error.ToString()); return false; }
	for (const FGuid& Id : RemovedSideIds)
		TestFalse(TEXT("Covered side placement is removed"), After.ContainsByPredicate([&](const auto& P) { return P.StableId == Id; }));
	for (const FGuid& Id : RetainedFrontIds)
		TestTrue(TEXT("Front placement keeps its stable identity"), After.ContainsByPredicate([&](const auto& P) { return P.StableId == Id && P.VariantId == Variant.VariantGuid; }));
	TestEqual(TEXT("Target retains its three front placements"), After.FilterByPredicate([&](const auto& P) { return P.BuildingId == TargetId; }).Num(), 3);
	{
		TArray<UHierarchicalInstancedStaticMeshComponent*> C; Layout->GetComponents(C); int32 N = 0; for (const auto* X : C) { N += X->GetInstanceCount(); }
		TestEqual(TEXT("Regeneration removes stale side mesh while keeping visible meshes"), N, 3);
	}
	{
		TArray<UChildActorComponent*> C; Layout->GetComponents(C);
		TestEqual(TEXT("Regeneration removes stale side actor while keeping visible actors"), C.Num(), 3);
	}
	{
		TArray<UDecalComponent*> C; Layout->GetComponents(C);
		TestEqual(TEXT("Regeneration removes stale side decal while keeping visible decals"), C.Num(), 3);
	}
	return true;
}

#endif
