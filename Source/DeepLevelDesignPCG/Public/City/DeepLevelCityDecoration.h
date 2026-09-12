// Copyright <--\, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "City/DeepLevelCityLayout.h"
#include "Engine/DataAsset.h"
#include "Components/ActorComponent.h"
#include "DeepLevelCityDecoration.generated.h"

class AActor;
class UMaterialInterface;
class UStaticMesh;

UENUM(BlueprintType)
enum class EDeepLevelCityDecorationOutput : uint8
{
	Mesh,
	Actor,
	Decal
};

USTRUCT(BlueprintType)
struct DEEPLEVELDESIGNPCG_API FDeepLevelCityDecorationEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FGuid EntryGuid;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	EDeepLevelCityDecorationOutput Output = EDeepLevelCityDecorationOutput::Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output", meta = (EditCondition = "Output == EDeepLevelCityDecorationOutput::Mesh", EditConditionHides))
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output", meta = (EditCondition = "Output == EDeepLevelCityDecorationOutput::Actor", EditConditionHides))
	TSoftClassPtr<AActor> ActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output", meta = (EditCondition = "Output == EDeepLevelCityDecorationOutput::Decal", EditConditionHides))
	TSoftObjectPtr<UMaterialInterface> DecalMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output", meta = (ClampMin = "1.0", EditCondition = "Output == EDeepLevelCityDecorationOutput::Decal", EditConditionHides))
	FVector DecalSize = FVector(32.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Matching")
	FGameplayTagContainer RequiredAnchorTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Matching")
	FGameplayTagContainer ExcludedAnchorTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Matching", meta = (Bitmask, BitmaskEnum = "/Script/DeepLevelDesignPCG.EDeepLevelCityOccupancy"))
	int32 RequiredOccupancyMask = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Matching", meta = (Bitmask, BitmaskEnum = "/Script/DeepLevelDesignPCG.EDeepLevelCityOccupancy"))
	int32 ClearanceBlockingOccupancyMask = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	FName PlacementSlot = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "1", UIMin = "1"))
	int32 SlotsPerAnchor = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	double Probability = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "0.0", UIMin = "0.0"))
	double MinimumSpacing = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "0.0", UIMin = "0.0"))
	double ClearanceRadius = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "0.0", UIMin = "0.0"))
	double RequiredAnchorClearanceDepth = 0.0;

	/** Deterministic cadence along an oriented anchor run. One evaluates every anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "1", UIMin = "1"))
	int32 AnchorInterval = 1;

	/** Grid-cell phase shared by entries that should form a furniture group. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "0", UIMin = "0"))
	int32 AnchorPhase = 0;

	/** Offset the reverse-facing segment edge by half an even interval. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	bool bStaggerOppositeEdges = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	FTransform LocalTransform = FTransform::Identity;
};

/** One reusable decoration category such as street furniture, facade signs, or decals. */
UCLASS(BlueprintType)
class DEEPLEVELDESIGNPCG_API UDeepLevelCityDecorationCategory : public UDataAsset
{
	GENERATED_BODY()

public:
	virtual void PostLoad() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Category")
	FGameplayTag CategoryTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Category", meta = (TitleProperty = "PlacementSlot"))
	TArray<FDeepLevelCityDecorationEntry> Entries;

	bool Validate(FText& OutError) const;

private:
	void EnsureEntryGuids(bool bRegenerateAll);
};

/** Composes independently authored categories for one city style. */
UCLASS(BlueprintType)
class DEEPLEVELDESIGNPCG_API UDeepLevelCityDecorationSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decoration")
	TArray<TObjectPtr<UDeepLevelCityDecorationCategory>> Categories;

	bool Validate(FText& OutError) const;
};

struct DEEPLEVELDESIGNPCG_API FDeepLevelCityResolvedDecoration
{
	FGuid StableId;
	FGuid AnchorId;
	FGuid EntryGuid;
	EDeepLevelCityDecorationOutput Output = EDeepLevelCityDecorationOutput::Mesh;
	TSoftObjectPtr<UStaticMesh> Mesh;
	TSoftClassPtr<AActor> ActorClass;
	TSoftObjectPtr<UMaterialInterface> DecalMaterial;
	FVector DecalSize = FVector(32.0);
	FTransform Transform = FTransform::Identity;
	FGameplayTagContainer Tags;
	FIntPoint Chunk = FIntPoint::ZeroValue;
	FName PlacementSlot = NAME_None;
	int32 Priority = 0;
	double MinimumSpacing = 0.0;
	double ClearanceRadius = 0.0;
};

class DEEPLEVELDESIGNPCG_API FDeepLevelCityDecorationResolver
{
public:
	static bool Resolve(
		const FDeepLevelCityLayoutSnapshot& Snapshot,
		const UDeepLevelCityDecorationSet& DecorationSet,
		int32 Seed,
		TArray<FDeepLevelCityResolvedDecoration>& OutPlacements,
		FText& OutError,
		TConstArrayView<FDeepLevelCityDecorationOverride> Overrides = {});
};

USTRUCT()
struct FDeepLevelCityMaterializedChunk
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TObjectPtr<UActorComponent>> Components;
};

/** Applies resolved city decorations through chunk-owned HISM, Actor, and Decal adapters. */
UCLASS(ClassGroup = (Procedural), meta = (BlueprintSpawnableComponent))
class DEEPLEVELDESIGNPCG_API UDeepLevelCityDecorationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDeepLevelCityDecorationComponent();

	UFUNCTION(BlueprintCallable, Category = "Deep Level Design PCG|City Decoration")
	bool Regenerate(bool bForceAll, FText& OutError);

	UFUNCTION(CallInEditor, Category = "Deep Level Design PCG|City Decoration")
	void RegenerateInEditor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deep Level Design PCG|City Decoration|Diagnostics")
	FText LastGenerationError;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deep Level Design PCG|City Decoration|Diagnostics")
	TArray<FIntPoint> LastDirtyChunks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deep Level Design PCG|City Decoration|Diagnostics")
	int32 LastPlacementCount = 0;

private:
	void ClearChunk(const FIntPoint& Chunk);
	void ClearAllChunks();
	bool MaterializeChunk(
		const FIntPoint& Chunk,
		TConstArrayView<FDeepLevelCityResolvedDecoration> Placements,
		FText& OutError);

	UPROPERTY()
	TMap<FIntPoint, FDeepLevelCityMaterializedChunk> MaterializedChunks;
};
