// Copyright <--\, Inc. All Rights Reserved.

#include "City/DeepLevelCityDecoration.h"
#include "DeepLevelDesignPCGModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DeepLevelCityDecoration)

#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Components/DecalComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

#define LOCTEXT_NAMESPACE "DeepLevelCityDecoration"

namespace
{
	bool DecorationHasRequiredOccupancy(
		const FDeepLevelCityLayoutSnapshot& Snapshot,
		const FDeepLevelCityAnchor& Anchor,
		const int32 RequiredMask)
	{
		if (RequiredMask == 0)
		{
			return true;
		}
		return Anchor.OccupiedCells.ContainsByPredicate([&Snapshot, RequiredMask](const FIntPoint& Cell)
		{
			const FDeepLevelCityCellState* State = Snapshot.FindCell(Cell);
			return State && (State->OccupancyMask & RequiredMask) == RequiredMask;
		});
	}

	bool DecorationOverlapsBlockedCell(
		const FDeepLevelCityLayoutSnapshot& Snapshot,
		const FVector& Location,
		const double Radius,
		const int32 BlockingMask)
	{
		if (BlockingMask == 0)
		{
			return false;
		}
		const FDeepLevelCityGrid& Grid = Snapshot.GetGrid();
		const double HalfTile = Grid.TileSize * 0.5;
		const FIntPoint CenterCell = Grid.WorldToCell(Location);
		const int32 SearchRadius = FMath::CeilToInt((Radius + HalfTile) / Grid.TileSize);
		for (int32 X = CenterCell.X - SearchRadius; X <= CenterCell.X + SearchRadius; ++X)
		{
			for (int32 Y = CenterCell.Y - SearchRadius; Y <= CenterCell.Y + SearchRadius; ++Y)
			{
				const FDeepLevelCityCellState* State = Snapshot.FindCell(FIntPoint(X, Y));
				if (!State || (State->OccupancyMask & BlockingMask) == 0)
				{
					continue;
				}
				const FVector CellCenter = Grid.CellToWorld(FIntPoint(X, Y));
				const double ClosestX = FMath::Clamp(Location.X, CellCenter.X - HalfTile, CellCenter.X + HalfTile);
				const double ClosestY = FMath::Clamp(Location.Y, CellCenter.Y - HalfTile, CellCenter.Y + HalfTile);
				if (FVector2D::DistSquared(FVector2D(Location), FVector2D(ClosestX, ClosestY)) < FMath::Square(Radius))
				{
					return true;
				}
			}
		}
		return false;
	}

	bool DecorationGuidLess(const FGuid& A, const FGuid& B)
	{
		if (A.A != B.A) return A.A < B.A;
		if (A.B != B.B) return A.B < B.B;
		if (A.C != B.C) return A.C < B.C;
		return A.D < B.D;
	}

	struct FDecorationCandidate
	{
		FDeepLevelCityResolvedDecoration Placement;
		FName Slot;
		int32 SlotIndex = 0;
		int32 ClearanceBlockingOccupancyMask = 0;
	};

	uint32 HashDecorationGuid(const FGuid& Guid)
	{
		return HashCombineFast(HashCombineFast(Guid.A, Guid.B), HashCombineFast(Guid.C, Guid.D));
	}

	const FDeepLevelCityAnchor* FindDecorationAnchor(
		const FDeepLevelCityLayoutSnapshot& Snapshot,
		const FGuid& AnchorId)
	{
		return Snapshot.GetAnchors().FindByPredicate([&AnchorId](const FDeepLevelCityAnchor& Anchor)
		{
			return Anchor.StableId == AnchorId;
		});
	}

	const FDeepLevelCityDecorationEntry* FindDecorationEntry(
		const UDeepLevelCityDecorationSet& DecorationSet,
		const FGuid& EntryGuid,
		const UDeepLevelCityDecorationCategory*& OutCategory)
	{
		OutCategory = nullptr;
		for (const UDeepLevelCityDecorationCategory* Category : DecorationSet.Categories)
		{
			const FDeepLevelCityDecorationEntry* Entry = Category->Entries.FindByPredicate(
				[&EntryGuid](const FDeepLevelCityDecorationEntry& Candidate) { return Candidate.EntryGuid == EntryGuid; });
			if (Entry)
			{
				OutCategory = Category;
				return Entry;
			}
		}
		return nullptr;
	}

	bool ValidateResolvedOutputs(
		const TConstArrayView<FDeepLevelCityResolvedDecoration> Placements,
		FText& OutError)
	{
		for (const FDeepLevelCityResolvedDecoration& Placement : Placements)
		{
			if (Placement.Output == EDeepLevelCityDecorationOutput::Mesh)
			{
				if (!Placement.Mesh.LoadSynchronous())
				{
					OutError = LOCTEXT("InvalidDecorationMesh", "City Decoration contains an invalid mesh output.");
					return false;
				}
				continue;
			}
			if (Placement.Output == EDeepLevelCityDecorationOutput::Actor)
			{
				UClass* ActorClass = Placement.ActorClass.LoadSynchronous();
				if (!ActorClass || ActorClass->HasAnyClassFlags(CLASS_Abstract))
				{
					OutError = LOCTEXT("InvalidDecorationActor", "City Decoration contains an invalid or abstract Actor output.");
					return false;
				}
				continue;
			}
			if (!Placement.DecalMaterial.LoadSynchronous())
			{
				OutError = LOCTEXT("InvalidDecorationDecal", "City Decoration contains an invalid Decal output.");
				return false;
			}
		}
		return true;
	}
}

void UDeepLevelCityDecorationCategory::PostLoad()
{
	Super::PostLoad();
	EnsureEntryGuids(false);
}

void UDeepLevelCityDecorationCategory::PostDuplicate(const EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	EnsureEntryGuids(true);
}

#if WITH_EDITOR
void UDeepLevelCityDecorationCategory::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	EnsureEntryGuids(false);
}
#endif

void UDeepLevelCityDecorationCategory::EnsureEntryGuids(const bool bRegenerateAll)
{
	TSet<FGuid> Guids;
	for (FDeepLevelCityDecorationEntry& Entry : Entries)
	{
		if (bRegenerateAll || !Entry.EntryGuid.IsValid() || Guids.Contains(Entry.EntryGuid))
		{
			Entry.EntryGuid = FGuid::NewGuid();
		}
		Guids.Add(Entry.EntryGuid);
	}
}

bool UDeepLevelCityDecorationCategory::Validate(FText& OutError) const
{
	OutError = FText::GetEmpty();
	TSet<FGuid> Guids;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FDeepLevelCityDecorationEntry& Entry = Entries[Index];
		if (!Entry.EntryGuid.IsValid() || Guids.Contains(Entry.EntryGuid))
		{
			OutError = FText::Format(LOCTEXT("InvalidEntryGuid", "Decoration entry {0} requires a unique valid ID."), FText::AsNumber(Index + 1));
			return false;
		}
		Guids.Add(Entry.EntryGuid);
		if (Entry.PlacementSlot.IsNone() || Entry.SlotsPerAnchor < 1
			|| !FMath::IsFinite(Entry.Probability) || Entry.Probability < 0.0 || Entry.Probability > 1.0
			|| !FMath::IsFinite(Entry.MinimumSpacing) || Entry.MinimumSpacing < 0.0
			|| !FMath::IsFinite(Entry.ClearanceRadius) || Entry.ClearanceRadius < 0.0
			|| !FMath::IsFinite(Entry.RequiredAnchorClearanceDepth) || Entry.RequiredAnchorClearanceDepth < 0.0
			|| Entry.AnchorInterval < 1
			|| Entry.AnchorPhase < 0
			|| (Entry.bStaggerOppositeEdges && (Entry.AnchorInterval < 2 || Entry.AnchorInterval % 2 != 0))
			|| (Entry.Output == EDeepLevelCityDecorationOutput::Decal
				&& (Entry.DecalSize.ContainsNaN() || Entry.DecalSize.GetMin() <= 0.0))
			|| Entry.LocalTransform.ContainsNaN())
		{
			OutError = FText::Format(LOCTEXT("InvalidEntryRules", "Decoration entry {0} has invalid placement rules."), FText::AsNumber(Index + 1));
			return false;
		}
		const bool bHasOutput = Entry.Output == EDeepLevelCityDecorationOutput::Mesh
			? !Entry.Mesh.IsNull()
			: Entry.Output == EDeepLevelCityDecorationOutput::Actor
				? !Entry.ActorClass.IsNull()
				: !Entry.DecalMaterial.IsNull();
		if (!bHasOutput)
		{
			OutError = FText::Format(LOCTEXT("MissingEntryOutput", "Decoration entry {0} is missing its output asset."), FText::AsNumber(Index + 1));
			return false;
		}
	}
	return true;
}

bool UDeepLevelCityDecorationSet::Validate(FText& OutError) const
{
	OutError = FText::GetEmpty();
	TSet<FGuid> Guids;
	for (int32 Index = 0; Index < Categories.Num(); ++Index)
	{
		const UDeepLevelCityDecorationCategory* Category = Categories[Index];
		if (!Category)
		{
			OutError = FText::Format(LOCTEXT("MissingCategory", "Decoration Set category {0} is missing."), FText::AsNumber(Index + 1));
			return false;
		}
		if (!Category->Validate(OutError))
		{
			return false;
		}
		for (const FDeepLevelCityDecorationEntry& Entry : Category->Entries)
		{
			if (Guids.Contains(Entry.EntryGuid))
			{
				OutError = LOCTEXT("DuplicateSetEntryGuid", "Decoration Set contains duplicate entry IDs across categories.");
				return false;
			}
			Guids.Add(Entry.EntryGuid);
		}
	}
	return true;
}

bool FDeepLevelCityDecorationResolver::Resolve(
	const FDeepLevelCityLayoutSnapshot& Snapshot,
	const UDeepLevelCityDecorationSet& DecorationSet,
	const int32 Seed,
	TArray<FDeepLevelCityResolvedDecoration>& OutPlacements,
	FText& OutError,
	const TConstArrayView<FDeepLevelCityDecorationOverride> Overrides)
{
	OutPlacements.Reset();
	if (!DecorationSet.Validate(OutError))
	{
		return false;
	}

	TArray<FDecorationCandidate> Candidates;
	for (const FDeepLevelCityAnchor& Anchor : Snapshot.GetAnchors())
	{
		for (const UDeepLevelCityDecorationCategory* Category : DecorationSet.Categories)
		{
			for (const FDeepLevelCityDecorationEntry& Entry : Category->Entries)
			{
				if (!Anchor.Tags.HasAll(Entry.RequiredAnchorTags)
					|| Anchor.Tags.HasAny(Entry.ExcludedAnchorTags)
					|| !DecorationHasRequiredOccupancy(Snapshot, Anchor, Entry.RequiredOccupancyMask)
					|| Anchor.ClearanceDepth < Entry.RequiredAnchorClearanceDepth)
				{
					continue;
				}
				for (int32 SlotIndex = 0; SlotIndex < Entry.SlotsPerAnchor; ++SlotIndex)
				{
					const FGuid PlacementId = FDeepLevelCityStableId::MakePlacementId(Anchor.StableId, Entry.EntryGuid, SlotIndex);
					FRandomStream Random(static_cast<int32>(HashCombineFast(GetTypeHash(Seed), HashDecorationGuid(PlacementId))));
					if (Entry.AnchorInterval > 1 && !Anchor.OccupiedCells.IsEmpty())
					{
						const FVector Tangent = Anchor.Transform.GetUnitAxis(EAxis::X);
						const bool bAlongX = FMath::Abs(Tangent.X) >= FMath::Abs(Tangent.Y);
						const int32 Coordinate = bAlongX
							? Anchor.OccupiedCells[0].X
							: Anchor.OccupiedCells[0].Y;
						const bool bReverseEdge = Anchor.Geometry == EDeepLevelCityAnchorGeometry::Segment
							&& (bAlongX ? Tangent.X : Tangent.Y) < 0.0;
						const int64 Phase = static_cast<int64>(Entry.AnchorPhase)
							+ (Entry.bStaggerOppositeEdges && bReverseEdge ? Entry.AnchorInterval / 2 : 0);
						const int64 CadenceIndex = ((static_cast<int64>(Coordinate) - Phase) % Entry.AnchorInterval
							+ Entry.AnchorInterval) % Entry.AnchorInterval;
						if (CadenceIndex != 0)
						{
							continue;
						}
					}
					if (Entry.Probability <= 0.0 || Random.FRand() >= Entry.Probability)
					{
						continue;
					}
					FDecorationCandidate& Candidate = Candidates.Emplace_GetRef();
					Candidate.Slot = Entry.PlacementSlot;
					Candidate.SlotIndex = SlotIndex;
					Candidate.ClearanceBlockingOccupancyMask = Entry.ClearanceBlockingOccupancyMask;
					Candidate.Placement.StableId = PlacementId;
					Candidate.Placement.AnchorId = Anchor.StableId;
					Candidate.Placement.EntryGuid = Entry.EntryGuid;
					Candidate.Placement.Output = Entry.Output;
					Candidate.Placement.Mesh = Entry.Mesh;
					Candidate.Placement.ActorClass = Entry.ActorClass;
					Candidate.Placement.DecalMaterial = Entry.DecalMaterial;
					Candidate.Placement.DecalSize = Entry.DecalSize;
					Candidate.Placement.Transform = Entry.LocalTransform * Anchor.Transform;
					Candidate.Placement.Tags = Anchor.Tags;
					if (Category->CategoryTag.IsValid())
					{
						Candidate.Placement.Tags.AddTag(Category->CategoryTag);
					}
					Candidate.Placement.Chunk = Snapshot.GetGrid().CellToChunk(
						Snapshot.GetGrid().WorldToCell(Candidate.Placement.Transform.GetLocation()));
					Candidate.Placement.PlacementSlot = Entry.PlacementSlot;
					Candidate.Placement.Priority = Entry.Priority;
					Candidate.Placement.MinimumSpacing = Entry.MinimumSpacing;
					Candidate.Placement.ClearanceRadius = Entry.ClearanceRadius;
				}
			}
		}
	}

	Candidates.Sort([](const FDecorationCandidate& A, const FDecorationCandidate& B)
	{
		return A.Placement.Priority == B.Placement.Priority
			? DecorationGuidLess(A.Placement.StableId, B.Placement.StableId)
			: A.Placement.Priority > B.Placement.Priority;
	});
	TSet<FString> ClaimedSlots;
	for (const FDecorationCandidate& Candidate : Candidates)
	{
		const FString SlotKey = FString::Printf(
			TEXT("%s|%s|%d"),
			*Candidate.Placement.AnchorId.ToString(EGuidFormats::Digits),
			*Candidate.Slot.ToString(),
			Candidate.SlotIndex);
		if (ClaimedSlots.Contains(SlotKey)
			|| DecorationOverlapsBlockedCell(
				Snapshot,
				Candidate.Placement.Transform.GetLocation(),
				Candidate.Placement.ClearanceRadius,
				Candidate.ClearanceBlockingOccupancyMask))
		{
			continue;
		}
		const bool bSpacingConflict = OutPlacements.ContainsByPredicate([&Candidate](const FDeepLevelCityResolvedDecoration& Existing)
		{
			if (Existing.PlacementSlot != Candidate.Placement.PlacementSlot)
			{
				return false;
			}
			const double RequiredDistance = FMath::Max(Candidate.Placement.MinimumSpacing, Existing.MinimumSpacing);
			return FVector2D::DistSquared(
				FVector2D(Candidate.Placement.Transform.GetLocation()),
				FVector2D(Existing.Transform.GetLocation())) < FMath::Square(RequiredDistance);
		});
		if (bSpacingConflict)
		{
			continue;
		}
		const bool bPhysicalConflict = Candidate.Placement.Output != EDeepLevelCityDecorationOutput::Decal
			&& Candidate.Placement.ClearanceRadius > 0.0
			&& OutPlacements.ContainsByPredicate([&Candidate](const FDeepLevelCityResolvedDecoration& Existing)
			{
				if (Existing.Output == EDeepLevelCityDecorationOutput::Decal || Existing.ClearanceRadius <= 0.0)
				{
					return false;
				}
				const double RequiredDistance = Candidate.Placement.ClearanceRadius + Existing.ClearanceRadius;
				return FVector2D::DistSquared(
					FVector2D(Candidate.Placement.Transform.GetLocation()),
					FVector2D(Existing.Transform.GetLocation())) < FMath::Square(RequiredDistance);
			});
		if (bPhysicalConflict)
		{
			continue;
		}
		ClaimedSlots.Add(SlotKey);
		OutPlacements.Add(Candidate.Placement);
	}

	TSet<FGuid> OverriddenIds;
	for (const FDeepLevelCityDecorationOverride& Override : Overrides)
	{
		if (Override.TransformOffset.ContainsNaN() || Override.SlotIndex < 0)
		{
			OutError = LOCTEXT("InvalidDecorationOverride", "City decoration override has an invalid transform or slot index.");
			return false;
		}
		const FGuid TargetId = Override.Mode == EDeepLevelCityDecorationOverrideMode::Add
			? FDeepLevelCityStableId::MakePlacementId(Override.AnchorId, Override.EntryGuid, Override.SlotIndex)
			: Override.PlacementId;
		if (!TargetId.IsValid() || OverriddenIds.Contains(TargetId))
		{
			OutError = LOCTEXT("DuplicateDecorationOverride", "City decoration overrides require unique valid target IDs.");
			return false;
		}
		OverriddenIds.Add(TargetId);
		const int32 PlacementIndex = OutPlacements.IndexOfByPredicate([&TargetId](const FDeepLevelCityResolvedDecoration& Placement)
		{
			return Placement.StableId == TargetId;
		});
		if (Override.Mode == EDeepLevelCityDecorationOverrideMode::Remove)
		{
			if (PlacementIndex == INDEX_NONE)
			{
				OutError = LOCTEXT("MissingRemovedDecoration", "Remove override targets a decoration placement that no longer exists.");
				return false;
			}
			OutPlacements.RemoveAt(PlacementIndex);
			continue;
		}
		if (Override.Mode == EDeepLevelCityDecorationOverrideMode::Modify)
		{
			if (PlacementIndex == INDEX_NONE)
			{
				OutError = LOCTEXT("MissingModifiedDecoration", "Modify override targets a decoration placement that no longer exists.");
				return false;
			}
			OutPlacements[PlacementIndex].Transform = Override.TransformOffset * OutPlacements[PlacementIndex].Transform;
			OutPlacements[PlacementIndex].Chunk = Snapshot.GetGrid().CellToChunk(
				Snapshot.GetGrid().WorldToCell(OutPlacements[PlacementIndex].Transform.GetLocation()));
			continue;
		}

		if (PlacementIndex != INDEX_NONE)
		{
			OutError = LOCTEXT("ExistingAddedDecoration", "Add override targets a decoration placement that already exists; use Modify instead.");
			return false;
		}
		const FDeepLevelCityAnchor* Anchor = FindDecorationAnchor(Snapshot, Override.AnchorId);
		const UDeepLevelCityDecorationCategory* Category = nullptr;
		const FDeepLevelCityDecorationEntry* Entry = FindDecorationEntry(DecorationSet, Override.EntryGuid, Category);
		if (!Anchor || !Entry || Override.SlotIndex >= Entry->SlotsPerAnchor)
		{
			OutError = LOCTEXT("MissingAddedDecorationSource", "Add override requires an existing anchor, decoration entry, and valid slot index.");
			return false;
		}
		FDeepLevelCityResolvedDecoration& Added = OutPlacements.Emplace_GetRef();
		Added.StableId = TargetId;
		Added.AnchorId = Anchor->StableId;
		Added.EntryGuid = Entry->EntryGuid;
		Added.Output = Entry->Output;
		Added.Mesh = Entry->Mesh;
		Added.ActorClass = Entry->ActorClass;
		Added.DecalMaterial = Entry->DecalMaterial;
		Added.DecalSize = Entry->DecalSize;
		Added.Transform = Override.TransformOffset * Entry->LocalTransform * Anchor->Transform;
		Added.Tags = Anchor->Tags;
		if (Category->CategoryTag.IsValid())
		{
			Added.Tags.AddTag(Category->CategoryTag);
		}
		Added.Chunk = Snapshot.GetGrid().CellToChunk(Snapshot.GetGrid().WorldToCell(Added.Transform.GetLocation()));
		Added.PlacementSlot = Entry->PlacementSlot;
		Added.Priority = Entry->Priority;
		Added.MinimumSpacing = Entry->MinimumSpacing;
		Added.ClearanceRadius = Entry->ClearanceRadius;
	}
	OutPlacements.Sort([](const FDeepLevelCityResolvedDecoration& A, const FDeepLevelCityResolvedDecoration& B)
	{
		return DecorationGuidLess(A.StableId, B.StableId);
	});
	return true;
}

UDeepLevelCityDecorationComponent::UDeepLevelCityDecorationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UDeepLevelCityDecorationComponent::Regenerate(const bool bForceAll, FText& OutError)
{
	LastGenerationError = FText::GetEmpty();
	LastDirtyChunks.Reset();
	LastPlacementCount = 0;
	ADeepLevelCityLayoutActor* Layout = Cast<ADeepLevelCityLayoutActor>(GetOwner());
	if (!Layout || !Layout->DecorationSet)
	{
		OutError = LOCTEXT("MissingDecorationOwner", "City Decoration requires a City Layout owner and Decoration Set.");
		LastGenerationError = OutError;
		return false;
	}
#if WITH_EDITOR
	if (UWorld* World = GetWorld(); World && !World->IsGameWorld())
	{
		Layout->Modify();
	}
#endif
	TSet<FIntPoint> DirtyChunks;
	if (!Layout->RebuildSnapshot(DirtyChunks, OutError))
	{
		LastGenerationError = OutError;
		return false;
	}
	TArray<FDeepLevelCityResolvedDecoration> Placements;
	if (!FDeepLevelCityDecorationResolver::Resolve(
		*Layout->GetSnapshot(),
		*Layout->DecorationSet,
		Layout->DecorationSeed,
		Placements,
		OutError,
		Layout->DecorationOverrides))
	{
		LastGenerationError = OutError;
		return false;
	}
	if (!ValidateResolvedOutputs(Placements, OutError))
	{
		LastGenerationError = OutError;
		return false;
	}
	if (bForceAll)
	{
		for (const TPair<FIntPoint, FDeepLevelCityMaterializedChunk>& Pair : MaterializedChunks)
		{
			DirtyChunks.Add(Pair.Key);
		}
		for (const FDeepLevelCityResolvedDecoration& Placement : Placements)
		{
			DirtyChunks.Add(Placement.Chunk);
		}
	}

	for (const FIntPoint& Chunk : DirtyChunks)
	{
		ClearChunk(Chunk);
		TArray<FDeepLevelCityResolvedDecoration> ChunkPlacements;
		for (const FDeepLevelCityResolvedDecoration& Placement : Placements)
		{
			if (Placement.Chunk == Chunk)
			{
				ChunkPlacements.Add(Placement);
			}
		}
		if (!MaterializeChunk(Chunk, ChunkPlacements, OutError))
		{
			for (const FIntPoint& DirtyChunk : DirtyChunks)
			{
				ClearChunk(DirtyChunk);
			}
			LastGenerationError = OutError;
			return false;
		}
	}
	LastDirtyChunks = DirtyChunks.Array();
	LastDirtyChunks.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
	LastPlacementCount = Placements.Num();
#if WITH_EDITOR
	if (UWorld* World = GetWorld(); World && !World->IsGameWorld())
	{
		Layout->MarkPackageDirty();
	}
#endif
	OutError = FText::GetEmpty();
	return true;
}

void UDeepLevelCityDecorationComponent::RegenerateInEditor()
{
	FText Error;
	if (!Regenerate(true, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("City decoration generation failed: %s"), *Error.ToString());
	}
}

void UDeepLevelCityDecorationComponent::ClearChunk(const FIntPoint& Chunk)
{
	FDeepLevelCityMaterializedChunk Existing;
	if (!MaterializedChunks.RemoveAndCopyValue(Chunk, Existing))
	{
		return;
	}
	for (UActorComponent* Component : Existing.Components)
	{
		if (Component)
		{
			if (AActor* Owner = GetOwner())
			{
				Owner->RemoveInstanceComponent(Component);
			}
			Component->DestroyComponent();
		}
	}
}

void UDeepLevelCityDecorationComponent::ClearAllChunks()
{
	TArray<FIntPoint> Chunks;
	MaterializedChunks.GetKeys(Chunks);
	for (const FIntPoint& Chunk : Chunks)
	{
		ClearChunk(Chunk);
	}
}

bool UDeepLevelCityDecorationComponent::MaterializeChunk(
	const FIntPoint& Chunk,
	const TConstArrayView<FDeepLevelCityResolvedDecoration> Placements,
	FText& OutError)
{
	if (Placements.IsEmpty())
	{
		return true;
	}
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		OutError = LOCTEXT("InvalidDecorationWorld", "City Decoration cannot materialize without a valid owner and world.");
		return false;
	}

	FDeepLevelCityMaterializedChunk& Output = MaterializedChunks.FindOrAdd(Chunk);
	TMap<FSoftObjectPath, UHierarchicalInstancedStaticMeshComponent*> MeshOutputs;
	const EObjectFlags GeneratedFlags = World->IsGameWorld() ? RF_NoFlags : RF_Transactional;
	for (const FDeepLevelCityResolvedDecoration& Placement : Placements)
	{
		if (Placement.Output == EDeepLevelCityDecorationOutput::Mesh)
		{
			UStaticMesh* Mesh = Placement.Mesh.LoadSynchronous();
			if (!Mesh)
			{
				OutError = LOCTEXT("InvalidDecorationMesh", "City Decoration contains an invalid mesh output.");
				return false;
			}
			UHierarchicalInstancedStaticMeshComponent*& HISM = MeshOutputs.FindOrAdd(Placement.Mesh.ToSoftObjectPath());
			if (!HISM)
			{
				HISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner, NAME_None, GeneratedFlags);
				HISM->CreationMethod = EComponentCreationMethod::Instance;
				HISM->SetStaticMesh(Mesh);
				HISM->SetupAttachment(Owner->GetRootComponent());
				Owner->AddInstanceComponent(HISM);
				HISM->RegisterComponent();
				Output.Components.Add(HISM);
			}
			HISM->AddInstance(Placement.Transform, true);
			continue;
		}
		if (Placement.Output == EDeepLevelCityDecorationOutput::Actor)
		{
			UClass* ActorClass = Placement.ActorClass.LoadSynchronous();
			if (!ActorClass)
			{
				OutError = LOCTEXT("InvalidDecorationActor", "City Decoration contains an invalid Actor output.");
				return false;
			}
			UChildActorComponent* ChildActor = NewObject<UChildActorComponent>(Owner, NAME_None, GeneratedFlags);
			ChildActor->CreationMethod = EComponentCreationMethod::Instance;
			ChildActor->SetChildActorClass(ActorClass);
			ChildActor->SetupAttachment(Owner->GetRootComponent());
			Owner->AddInstanceComponent(ChildActor);
			ChildActor->RegisterComponent();
			ChildActor->SetWorldTransform(Placement.Transform);
			if (!ChildActor->GetChildActor())
			{
				OutError = LOCTEXT("DecorationActorSpawnFailed", "City Decoration could not spawn an Actor output.");
				ChildActor->DestroyComponent();
				return false;
			}
			Output.Components.Add(ChildActor);
			continue;
		}

		UMaterialInterface* Material = Placement.DecalMaterial.LoadSynchronous();
		if (!Material)
		{
			OutError = LOCTEXT("InvalidDecorationDecal", "City Decoration contains an invalid Decal output.");
			return false;
		}
		UDecalComponent* Decal = NewObject<UDecalComponent>(Owner, NAME_None, GeneratedFlags);
		Decal->CreationMethod = EComponentCreationMethod::Instance;
		Decal->SetDecalMaterial(Material);
		Decal->DecalSize = Placement.DecalSize;
		Decal->SetupAttachment(Owner->GetRootComponent());
		Owner->AddInstanceComponent(Decal);
		Decal->RegisterComponent();
		Decal->SetWorldTransform(Placement.Transform);
		Output.Components.Add(Decal);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
