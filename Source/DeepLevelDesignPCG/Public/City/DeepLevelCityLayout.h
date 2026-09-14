// Copyright <--\, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "UObject/Interface.h"
#include "DeepLevelCityLayout.generated.h"

namespace DeepLevelCityTags
{
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cell_Road);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cell_Sidewalk);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cell_Building);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Anchor_Road_Surface);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Anchor_Sidewalk_Surface);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Anchor_Sidewalk_Edge);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Anchor_Road_Junction);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Anchor_Road_DeadEnd);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Anchor_Road_Local);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Anchor_Road_Arterial);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Anchor_Building_Facade);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Anchor_Building_Corner);
}

class UDeepLevelCityDecorationSet;
class UDeepLevelCityDecorationComponent;

UENUM(BlueprintType)
enum class EDeepLevelCityDecorationOverrideMode : uint8
{
	Remove,
	Modify,
	Add
};

USTRUCT(BlueprintType)
struct DEEPLEVELDESIGNPCG_API FDeepLevelCityDecorationOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Override")
	EDeepLevelCityDecorationOverrideMode Mode = EDeepLevelCityDecorationOverrideMode::Modify;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Override", meta = (EditCondition = "Mode != EDeepLevelCityDecorationOverrideMode::Add"))
	FGuid PlacementId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Override", meta = (EditCondition = "Mode == EDeepLevelCityDecorationOverrideMode::Add", EditConditionHides))
	FGuid AnchorId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Override", meta = (EditCondition = "Mode == EDeepLevelCityDecorationOverrideMode::Add", EditConditionHides))
	FGuid EntryGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Override", meta = (ClampMin = "0", UIMin = "0", EditCondition = "Mode == EDeepLevelCityDecorationOverrideMode::Add", EditConditionHides))
	int32 SlotIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Override", meta = (EditCondition = "Mode != EDeepLevelCityDecorationOverrideMode::Remove", EditConditionHides))
	FTransform TransformOffset = FTransform::Identity;
};

UCLASS(BlueprintType)
class DEEPLEVELDESIGNPCG_API UDeepLevelCityGridProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1.0", UIMin = "1.0"))
	double TileSize = 500.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1", UIMin = "1"))
	int32 ChunkSizeInCells = 16;

	bool Validate(FText& OutError) const;
};

class USceneComponent;
struct FDeepLevelCityGrid;
class FDeepLevelCityLayoutSnapshot;

/** Authoritative world-space origin and bounds for one flat city grid. */
UCLASS(BlueprintType, ClassGroup = (Procedural))
class DEEPLEVELDESIGNPCG_API ADeepLevelCityLayoutActor : public AActor
{
	GENERATED_BODY()

public:
	ADeepLevelCityLayoutActor();
	virtual void OnConstruction(const FTransform& Transform) override;

	bool ResolveGrid(FDeepLevelCityGrid& OutGrid, FText& OutError) const;
	void RegisterLayoutSource(AActor& Source);
	void UnregisterLayoutSource(AActor& Source);
	void InvalidateSnapshot();
	bool RefreshSnapshot(TSet<FIntPoint>& OutDirtyChunks, FText& OutError);
	TSharedPtr<const FDeepLevelCityLayoutSnapshot> GetSnapshot() const { return bSnapshotCurrent ? Snapshot : nullptr; }
	FSimpleMulticastDelegate OnGridOriginChanged;

	UFUNCTION(CallInEditor, Category = "Deep Level Design PCG|City Decoration")
	void RegenerateDecoration();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deep Level Design PCG|City Grid")
	TObjectPtr<UDeepLevelCityGridProfile> GridProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deep Level Design PCG|Decoration")
	TObjectPtr<UDeepLevelCityDecorationSet> DecorationSet;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deep Level Design PCG|Decoration")
	int32 DecorationSeed = 1337;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deep Level Design PCG|Decoration", meta = (TitleProperty = "Mode"))
	TArray<FDeepLevelCityDecorationOverride> DecorationOverrides;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deep Level Design PCG|City Grid|Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deep Level Design PCG|City Grid|Components")
	TObjectPtr<UDeepLevelCityDecorationComponent> DecorationComponent;

private:
	TSet<TWeakObjectPtr<AActor>> LayoutSources;
	TSharedPtr<const FDeepLevelCityLayoutSnapshot> Snapshot;
	bool bSnapshotCurrent = false;
};

struct DEEPLEVELDESIGNPCG_API FDeepLevelCityGrid
{
	FVector Origin = FVector::ZeroVector;
	double TileSize = 500.0;
	int32 ChunkSizeInCells = 16;
	bool Validate(FText& OutError) const;
	FIntPoint WorldToCell(const FVector& WorldPosition) const;
	FVector CellToWorld(const FIntPoint& Cell) const;
	FIntPoint CellToChunk(const FIntPoint& Cell) const;
};

UENUM(BlueprintType)
enum class EDeepLevelCityAnchorGeometry : uint8
{
	Point,
	Segment,
	Surface,
	Volume
};

UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EDeepLevelCityOccupancy : uint8
{
	None = 0 UMETA(Hidden),
	Road = 1 << 0,
	Sidewalk = 1 << 1,
	Building = 1 << 2,
	Reserved = 1 << 3
};
ENUM_CLASS_FLAGS(EDeepLevelCityOccupancy)

USTRUCT(BlueprintType)
struct DEEPLEVELDESIGNPCG_API FDeepLevelCityCellState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cell")
	FIntPoint Cell = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cell", meta = (Bitmask, BitmaskEnum = "/Script/DeepLevelDesignPCG.EDeepLevelCityOccupancy"))
	int32 OccupancyMask = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cell")
	FGameplayTagContainer Tags;
};

/** Oriented source geometry. X is tangent, Z is surface normal, and Extent is local half-size. */
USTRUCT(BlueprintType)
struct DEEPLEVELDESIGNPCG_API FDeepLevelCityAnchor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FGuid StableId;

	/** Filled by the layout builder from the owning fragment. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FGuid SourceGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	EDeepLevelCityAnchorGeometry Geometry = EDeepLevelCityAnchorGeometry::Point;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	FTransform Transform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	FVector Extent = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ClampMin = "0.0", UIMin = "0.0"))
	double ClearanceDepth = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Classification")
	FGameplayTagContainer Tags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occupancy")
	TArray<FIntPoint> OccupiedCells;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 SourceRevision = 0;
};

USTRUCT()
struct DEEPLEVELDESIGNPCG_API FDeepLevelCityLayoutFragment
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid SourceGuid;

	UPROPERTY()
	int32 SourceRevision = 0;

	UPROPERTY()
	TArray<FDeepLevelCityCellState> Cells;

	UPROPERTY()
	TArray<FDeepLevelCityAnchor> Anchors;
};

class DEEPLEVELDESIGNPCG_API FDeepLevelCityLayoutSnapshot
{
public:
	const FDeepLevelCityGrid& GetGrid() const { return Grid; }
	const TMap<FIntPoint, FDeepLevelCityCellState>& GetCells() const { return Cells; }
	const TArray<FDeepLevelCityAnchor>& GetAnchors() const { return Anchors; }
	const FDeepLevelCityCellState* FindCell(const FIntPoint& Cell) const { return Cells.Find(Cell); }

private:
	friend class FDeepLevelCityLayoutBuilder;
	FDeepLevelCityGrid Grid;
	TMap<FIntPoint, FDeepLevelCityCellState> Cells;
	TArray<FDeepLevelCityAnchor> Anchors;
};

class DEEPLEVELDESIGNPCG_API FDeepLevelCityStableId
{
public:
	static FGuid MakeAnchorId(const FGuid& SourceGuid, FStringView SourceLocalKey, FName AnchorSlot);
	static FGuid MakePlacementId(const FGuid& AnchorId, const FGuid& CategoryEntryGuid, int32 Slot);
};

class DEEPLEVELDESIGNPCG_API FDeepLevelCityLayoutBuilder
{
public:
	static bool Build(
		const FDeepLevelCityGrid& Grid,
		TConstArrayView<FDeepLevelCityLayoutFragment> Fragments,
		TSharedPtr<const FDeepLevelCityLayoutSnapshot>& OutSnapshot,
		FText& OutError);

	static void FindDirtyChunks(
		const FDeepLevelCityLayoutSnapshot* Previous,
		const FDeepLevelCityLayoutSnapshot& Current,
		TSet<FIntPoint>& OutDirtyChunks);
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDeepLevelCityLayoutProvider : public UInterface
{
	GENERATED_BODY()
};

class DEEPLEVELDESIGNPCG_API IDeepLevelCityLayoutProvider
{
	GENERATED_BODY()

public:
	virtual const ADeepLevelCityLayoutActor* GetCityLayoutOwner() const = 0;
	virtual bool BuildCityLayoutFragment(
		const FDeepLevelCityGrid& Grid,
		FDeepLevelCityLayoutFragment& OutFragment,
		FText& OutError) const = 0;
};
