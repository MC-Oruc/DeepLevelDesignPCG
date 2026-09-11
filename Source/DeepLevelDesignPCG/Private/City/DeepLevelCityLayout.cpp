// Copyright <--\, Inc. All Rights Reserved.

#include "City/DeepLevelCityLayout.h"
#include "City/DeepLevelCityDecoration.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DeepLevelCityLayout)

#include "Components/SceneComponent.h"

namespace DeepLevelCityTags
{
	UE_DEFINE_GAMEPLAY_TAG(Cell_Road, "City.Cell.Road");
	UE_DEFINE_GAMEPLAY_TAG(Cell_Sidewalk, "City.Cell.Sidewalk");
	UE_DEFINE_GAMEPLAY_TAG(Cell_Building, "City.Cell.Building");
	UE_DEFINE_GAMEPLAY_TAG(Anchor_Road_Surface, "City.Anchor.Road.Surface");
	UE_DEFINE_GAMEPLAY_TAG(Anchor_Sidewalk_Surface, "City.Anchor.Sidewalk.Surface");
	UE_DEFINE_GAMEPLAY_TAG(Anchor_Sidewalk_Edge, "City.Anchor.Sidewalk.Edge");
	UE_DEFINE_GAMEPLAY_TAG(Anchor_Road_Junction, "City.Anchor.Road.Junction");
	UE_DEFINE_GAMEPLAY_TAG(Anchor_Road_DeadEnd, "City.Anchor.Road.DeadEnd");
	UE_DEFINE_GAMEPLAY_TAG(Anchor_Road_Local, "City.Anchor.Road.Local");
	UE_DEFINE_GAMEPLAY_TAG(Anchor_Road_Arterial, "City.Anchor.Road.Arterial");
	UE_DEFINE_GAMEPLAY_TAG(Anchor_Building_Facade, "City.Anchor.Building.Facade");
	UE_DEFINE_GAMEPLAY_TAG(Anchor_Building_Corner, "City.Anchor.Building.Corner");
}

#define LOCTEXT_NAMESPACE "DeepLevelCityLayout"

namespace
{
	FString MakeStableKey(const FGuid& Guid, const FStringView LocalKey, const FStringView Slot)
	{
		return FString::Printf(
			TEXT("%s|%.*s|%.*s"),
			*Guid.ToString(EGuidFormats::Digits),
			LocalKey.Len(), LocalKey.GetData(),
			Slot.Len(), Slot.GetData());
	}

	void AddCellChunk(const FDeepLevelCityGrid& Grid, const FIntPoint& Cell, TSet<FIntPoint>& Chunks)
	{
		Chunks.Add(Grid.CellToChunk(Cell));
	}
}

ADeepLevelCityLayoutActor::ADeepLevelCityLayoutActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CityGridRoot"));
	SetRootComponent(SceneRoot);
	DecorationComponent = CreateDefaultSubobject<UDeepLevelCityDecorationComponent>(TEXT("CityDecoration"));
}

bool ADeepLevelCityLayoutActor::ResolveGrid(FDeepLevelCityGrid& OutGrid, FText& OutError) const
{
	OutGrid = {};
	if (!GridProfile)
	{
		OutError = LOCTEXT("MissingCityGridProfile", "City Layout requires a City Grid Profile.");
		return false;
	}
	if (!GridProfile->Validate(OutError))
	{
		return false;
	}
	if (ExtentInCells.X < 1 || ExtentInCells.Y < 1)
	{
		OutError = LOCTEXT("InvalidCityGridBounds", "City Layout grid extents must be at least one cell.");
		return false;
	}
	if (!GetActorRotation().IsNearlyZero() || !GetActorScale3D().Equals(FVector::OneVector))
	{
		OutError = LOCTEXT("TransformedCityGrid", "City Layout supports translation only; rotation and scale must remain unchanged.");
		return false;
	}

	OutGrid.Origin = GetActorLocation();
	OutGrid.TileSize = GridProfile->TileSize;
	OutGrid.ChunkSizeInCells = GridProfile->ChunkSizeInCells;
	OutGrid.ExtentInCells = ExtentInCells;
	return OutGrid.Validate(OutError);
}

bool ADeepLevelCityLayoutActor::RebuildSnapshot(TSet<FIntPoint>& OutDirtyChunks, FText& OutError)
{
	OutDirtyChunks.Reset();
	FDeepLevelCityGrid Grid;
	if (!ResolveGrid(Grid, OutError))
	{
		return false;
	}
	TArray<FDeepLevelCityLayoutFragment> Fragments;
	Fragments.Reserve(LayoutProviders.Num());
	for (AActor* ProviderActor : LayoutProviders)
	{
		const IDeepLevelCityLayoutProvider* Provider = Cast<IDeepLevelCityLayoutProvider>(ProviderActor);
		if (!Provider)
		{
			OutError = LOCTEXT("InvalidLayoutProvider", "City Layout contains an actor that does not provide a city layout fragment.");
			return false;
		}
		FDeepLevelCityLayoutFragment& Fragment = Fragments.Emplace_GetRef();
		if (!Provider->BuildCityLayoutFragment(Grid, Fragment, OutError))
		{
			return false;
		}
	}

	TSharedPtr<const FDeepLevelCityLayoutSnapshot> NewSnapshot;
	if (!FDeepLevelCityLayoutBuilder::Build(Grid, Fragments, NewSnapshot, OutError))
	{
		return false;
	}
	FDeepLevelCityLayoutBuilder::FindDirtyChunks(Snapshot.Get(), *NewSnapshot, OutDirtyChunks);
	Snapshot = MoveTemp(NewSnapshot);
	return true;
}

void ADeepLevelCityLayoutActor::RegenerateDecoration()
{
	DecorationComponent->RegenerateInEditor();
}

bool UDeepLevelCityGridProfile::Validate(FText& OutError) const
{
	FDeepLevelCityGrid Grid;
	Grid.TileSize = TileSize;
	Grid.ChunkSizeInCells = ChunkSizeInCells;
	return Grid.Validate(OutError);
}

bool FDeepLevelCityGrid::Validate(FText& OutError) const
{
	OutError = FText::GetEmpty();
	if (Origin.ContainsNaN() || !FMath::IsFinite(TileSize) || TileSize <= UE_DOUBLE_SMALL_NUMBER)
	{
		OutError = LOCTEXT("InvalidCityGrid", "City Grid requires a finite origin and tile size greater than zero.");
		return false;
	}
	if (ChunkSizeInCells < 1)
	{
		OutError = LOCTEXT("InvalidCityChunk", "City Grid chunk size must be at least one cell.");
		return false;
	}
	if (ExtentInCells.X < 1 || ExtentInCells.Y < 1)
	{
		OutError = LOCTEXT("InvalidCityExtent", "City Grid extents must be at least one cell.");
		return false;
	}
	return true;
}

bool FDeepLevelCityGrid::ContainsCell(const FIntPoint& Cell) const
{
	return FMath::Abs(Cell.X) <= ExtentInCells.X && FMath::Abs(Cell.Y) <= ExtentInCells.Y;
}

FIntPoint FDeepLevelCityGrid::WorldToCell(const FVector& WorldPosition) const
{
	check(TileSize > UE_DOUBLE_SMALL_NUMBER);
	return FIntPoint(
		FMath::RoundToInt((WorldPosition.X - Origin.X) / TileSize),
		FMath::RoundToInt((WorldPosition.Y - Origin.Y) / TileSize));
}

FVector FDeepLevelCityGrid::CellToWorld(const FIntPoint& Cell) const
{
	return Origin + FVector(Cell.X * TileSize, Cell.Y * TileSize, 0.0);
}

FIntPoint FDeepLevelCityGrid::CellToChunk(const FIntPoint& Cell) const
{
	check(ChunkSizeInCells > 0);
	return FIntPoint(
		FMath::FloorToInt(static_cast<double>(Cell.X) / ChunkSizeInCells),
		FMath::FloorToInt(static_cast<double>(Cell.Y) / ChunkSizeInCells));
}

FGuid FDeepLevelCityStableId::MakeAnchorId(
	const FGuid& SourceGuid,
	const FStringView SourceLocalKey,
	const FName AnchorSlot)
{
	return FGuid::NewDeterministicGuid(MakeStableKey(SourceGuid, SourceLocalKey, AnchorSlot.ToString()));
}

FGuid FDeepLevelCityStableId::MakePlacementId(
	const FGuid& AnchorId,
	const FGuid& CategoryEntryGuid,
	const int32 Slot)
{
	return FGuid::NewDeterministicGuid(MakeStableKey(
		AnchorId,
		CategoryEntryGuid.ToString(EGuidFormats::Digits),
		LexToString(Slot)));
}

bool FDeepLevelCityLayoutBuilder::Build(
	const FDeepLevelCityGrid& Grid,
	const TConstArrayView<FDeepLevelCityLayoutFragment> Fragments,
	TSharedPtr<const FDeepLevelCityLayoutSnapshot>& OutSnapshot,
	FText& OutError)
{
	OutSnapshot.Reset();
	if (!Grid.Validate(OutError))
	{
		return false;
	}

	TSharedRef<FDeepLevelCityLayoutSnapshot> Snapshot = MakeShared<FDeepLevelCityLayoutSnapshot>();
	Snapshot->Grid = Grid;
	TSet<FGuid> SourceGuids;
	TSet<FGuid> AnchorIds;
	for (const FDeepLevelCityLayoutFragment& Fragment : Fragments)
	{
		if (!Fragment.SourceGuid.IsValid() || Fragment.SourceRevision < 0 || SourceGuids.Contains(Fragment.SourceGuid))
		{
			OutError = LOCTEXT("InvalidLayoutSource", "City Layout fragments require unique valid source GUIDs and non-negative revisions.");
			return false;
		}
		SourceGuids.Add(Fragment.SourceGuid);

		for (const FDeepLevelCityCellState& Cell : Fragment.Cells)
		{
			if (!Grid.ContainsCell(Cell.Cell))
			{
				OutError = LOCTEXT("CellOutsideCityBounds", "City Layout fragment contains a cell outside the City Layout bounds.");
				return false;
			}
			FDeepLevelCityCellState& Merged = Snapshot->Cells.FindOrAdd(Cell.Cell);
			Merged.Cell = Cell.Cell;
			Merged.OccupancyMask |= Cell.OccupancyMask;
			Merged.Tags.AppendTags(Cell.Tags);
		}

		for (const FDeepLevelCityAnchor& Anchor : Fragment.Anchors)
		{
			if (!Anchor.StableId.IsValid() || AnchorIds.Contains(Anchor.StableId)
				|| Anchor.SourceRevision != Fragment.SourceRevision
				|| Anchor.Transform.ContainsNaN() || Anchor.Extent.ContainsNaN()
				|| Anchor.ClearanceDepth < 0.0 || !FMath::IsFinite(Anchor.ClearanceDepth))
			{
				OutError = LOCTEXT("InvalidCityAnchor", "City Layout contains an invalid, duplicate, or stale anchor.");
				return false;
			}
			if (Anchor.OccupiedCells.ContainsByPredicate([&Grid](const FIntPoint& Cell) { return !Grid.ContainsCell(Cell); }))
			{
				OutError = LOCTEXT("AnchorOutsideCityBounds", "City Layout anchor references a cell outside the City Layout bounds.");
				return false;
			}
			AnchorIds.Add(Anchor.StableId);
			Snapshot->Anchors.Add(Anchor);
		}
	}

	Snapshot->Anchors.Sort([](const FDeepLevelCityAnchor& A, const FDeepLevelCityAnchor& B)
	{
		return A.StableId < B.StableId;
	});
	OutSnapshot = Snapshot;
	OutError = FText::GetEmpty();
	return true;
}

void FDeepLevelCityLayoutBuilder::FindDirtyChunks(
	const FDeepLevelCityLayoutSnapshot* Previous,
	const FDeepLevelCityLayoutSnapshot& Current,
	TSet<FIntPoint>& OutDirtyChunks)
{
	OutDirtyChunks.Reset();
	if (!Previous)
	{
		for (const TPair<FIntPoint, FDeepLevelCityCellState>& Pair : Current.Cells)
		{
			AddCellChunk(Current.Grid, Pair.Key, OutDirtyChunks);
		}
		for (const FDeepLevelCityAnchor& Anchor : Current.Anchors)
		{
			for (const FIntPoint& Cell : Anchor.OccupiedCells)
			{
				AddCellChunk(Current.Grid, Cell, OutDirtyChunks);
			}
		}
		return;
	}

	TSet<FIntPoint> AllCells;
	Previous->Cells.GetKeys(AllCells);
	for (const TPair<FIntPoint, FDeepLevelCityCellState>& Pair : Current.Cells)
	{
		AllCells.Add(Pair.Key);
	}
	for (const FIntPoint& Cell : AllCells)
	{
		const FDeepLevelCityCellState* Old = Previous->FindCell(Cell);
		const FDeepLevelCityCellState* New = Current.FindCell(Cell);
		if (!Old || !New || Old->OccupancyMask != New->OccupancyMask || Old->Tags != New->Tags)
		{
			AddCellChunk(Current.Grid, Cell, OutDirtyChunks);
		}
	}

	TMap<FGuid, const FDeepLevelCityAnchor*> PreviousAnchors;
	for (const FDeepLevelCityAnchor& Anchor : Previous->Anchors)
	{
		PreviousAnchors.Add(Anchor.StableId, &Anchor);
	}
	TSet<FGuid> CurrentAnchorIds;
	for (const FDeepLevelCityAnchor& Anchor : Current.Anchors)
	{
		CurrentAnchorIds.Add(Anchor.StableId);
		const FDeepLevelCityAnchor* const* Old = PreviousAnchors.Find(Anchor.StableId);
		if (!Old || (*Old)->SourceRevision != Anchor.SourceRevision || !(*Old)->Transform.Equals(Anchor.Transform))
		{
			for (const FIntPoint& Cell : Anchor.OccupiedCells)
			{
				AddCellChunk(Current.Grid, Cell, OutDirtyChunks);
			}
			if (Old)
			{
				for (const FIntPoint& Cell : (*Old)->OccupiedCells)
				{
					AddCellChunk(Current.Grid, Cell, OutDirtyChunks);
				}
			}
		}
	}
	for (const TPair<FGuid, const FDeepLevelCityAnchor*>& Pair : PreviousAnchors)
	{
		if (!CurrentAnchorIds.Contains(Pair.Key))
		{
			for (const FIntPoint& Cell : Pair.Value->OccupiedCells)
			{
				AddCellChunk(Current.Grid, Cell, OutDirtyChunks);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
