// Copyright <--\, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PCGSettings.h"
#include "Components/BoxComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataAsset.h"
#include "City/DeepLevelCityLayout.h"
#include "DeepLevelRoadPCG.generated.h"





class UPCGComponent;
class UBillboardComponent;
class UDeepLevelRoadSplineComponent;
class ADeepLevelRoadNetworkActor;
class UMaterialInterface;
class UStaticMesh;

UENUM(BlueprintType)
enum class EDeepLevelRoadTileKind : uint8
{
	Road,
	Sidewalk
};

UENUM(BlueprintType)
enum class EDeepLevelRoadCellOverrideMode : uint8
{
	Remove,
	Modify,
	Add
};

/** Persistent final-placement edit for one Road Network grid cell. */
USTRUCT(BlueprintType)
struct DEEPLEVELDESIGNPCG_API FDeepLevelRoadCellOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cell")
	FIntPoint GridCell = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cell")
	EDeepLevelRoadCellOverrideMode Mode = EDeepLevelRoadCellOverrideMode::Modify;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "Mode == EDeepLevelRoadCellOverrideMode::Add"))
	EDeepLevelRoadTileKind AddedTileKind = EDeepLevelRoadTileKind::Sidewalk;

	/** Optional for Modify; required for Add. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "Mode != EDeepLevelRoadCellOverrideMode::Remove"))
	TSoftObjectPtr<UStaticMesh> ReplacementMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "Mode != EDeepLevelRoadCellOverrideMode::Remove"))
	bool bOverrideMaterial = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "bOverrideMaterial && Mode != EDeepLevelRoadCellOverrideMode::Remove", EditConditionHides))
	TSoftObjectPtr<UMaterialInterface> MaterialOverride;

	/** Offset in the tile's local axes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform", meta = (EditCondition = "Mode != EDeepLevelRoadCellOverrideMode::Remove"))
	FVector LocalOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform", meta = (EditCondition = "Mode != EDeepLevelRoadCellOverrideMode::Remove"))
	FRotator RotationOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform", meta = (EditCondition = "Mode != EDeepLevelRoadCellOverrideMode::Remove"))
	FVector ScaleMultiplier = FVector::OneVector;
};

enum class EDeepLevelRoadNetworkChange : uint8
{
	Geometry,
	Structure,
	GeneratedComponents
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FDeepLevelRoadNetworkEditorChanged, ADeepLevelRoadNetworkActor&, EDeepLevelRoadNetworkChange);

/** Stable visualization anchor for Road Network authoring. */
UCLASS(ClassGroup = (Procedural))
class DEEPLEVELDESIGNPCG_API UDeepLevelRoadNetworkRootComponent : public UBoxComponent
{
	GENERATED_BODY()
};

/** Owns every spline branch and the PCG generation for one road network. */
UCLASS(BlueprintType, ClassGroup = (Procedural))
class DEEPLEVELDESIGNPCG_API ADeepLevelRoadNetworkActor : public AActor, public IDeepLevelCityLayoutProvider
{
	GENERATED_BODY()

public:
	ADeepLevelRoadNetworkActor();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostLoad() override;
	virtual void PostActorCreated() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "@Deep Level Design PCG|Road Network")
	TObjectPtr<ADeepLevelCityLayoutActor> CityLayout;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "@Deep Level Design PCG|Road Network")
	TSoftObjectPtr<UDeepLevelRoadTileCatalog> Catalog;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "@Deep Level Design PCG|Road Collision")
	FCollisionProfileName RoadMeshCollisionProfile = UCollisionProfile::BlockAll_ProfileName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "@Deep Level Design PCG|Local Overrides", meta = (TitleProperty = "GridCell"))
	TArray<FDeepLevelRoadCellOverride> CellOverrides;

	UDeepLevelRoadSplineComponent* CreateRoadBranch();
	void GetRoadSplineComponents(TArray<UDeepLevelRoadSplineComponent*>& OutSplines) const;
	bool ResolveCityGrid(FDeepLevelCityGrid& OutGrid, FText& OutError) const;
	FVector GetGridOrigin() const;
	double GetGridSize() const;
	virtual bool BuildCityLayoutFragment(
		const FDeepLevelCityGrid& Grid,
		FDeepLevelCityLayoutFragment& OutFragment,
		FText& OutError) const override;
	void NotifyRoadNetworkChanged(EDeepLevelRoadNetworkChange Change = EDeepLevelRoadNetworkChange::Geometry);

#if WITH_EDITOR
	static FDeepLevelRoadNetworkEditorChanged OnRoadNetworkEditorChanged;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;
	virtual void PostEditUndo() override;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "@Deep Level Design PCG|Road Network|Components")
	TObjectPtr<UDeepLevelRoadNetworkRootComponent> RoadNetworkRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "@Deep Level Design PCG|Road Network|Components")
	TObjectPtr<USceneComponent> RoadLines;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "@Deep Level Design PCG|Road Network|Components")
	TObjectPtr<USceneComponent> GeneratedRoadMeshes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "@Deep Level Design PCG|PCG|Components")
	TObjectPtr<UPCGComponent> PCGComponent;

private:
	void EnsureLayoutSourceGuid();
	void OrganizeGeneratedRoadMeshes(UPCGComponent* GeneratedComponent);

	UPROPERTY(VisibleAnywhere, Category = "@Deep Level Design PCG|Road Network")
	FGuid LayoutSourceGuid;

	UPROPERTY(VisibleAnywhere, Category = "@Deep Level Design PCG|Road Network")
	int32 LayoutRevision = 0;

#if WITH_EDITOR
	void GenerateInitialRoadNetwork();
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient)
	TObjectPtr<UBillboardComponent> EditorIcon;
#endif
};





class UDeepLevelRoadTileCatalog;

/** Builds one snapped road graph from every spline owned by its Road Network actor. */
UCLASS(BlueprintType, ClassGroup = (Procedural))
class DEEPLEVELDESIGNPCG_API UDeepLevelRoadNetworkPCGSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override;
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spawner; }
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	int32 RandomSeed = 1337;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output")
	FName MeshAttribute = TEXT("Mesh");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output")
	FName MaterialOverrideAttribute = TEXT("TileMaterialOverride");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output")
	FName TileKindAttribute = TEXT("RoadTileKind");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output")
	FName ConnectionMaskAttribute = TEXT("RoadConnections");

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};





/** One centerline contributing road edges to its owner's road network. */
UCLASS(ClassGroup = (Procedural), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class DEEPLEVELDESIGNPCG_API UDeepLevelRoadSplineComponent : public USplineComponent
{
	GENERATED_BODY()

public:
	UDeepLevelRoadSplineComponent();
	void SetRoadPathFromWorldPoints(const TArray<FVector>& WorldPoints);
	void NormalizePivotToFirstPoint();

#if WITH_EDITOR
	virtual TArray<ESplinePointType::Type> GetEnabledSplinePointTypes() const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditComponentMove(bool bFinished) override;
	virtual void PostEditUndo() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
#endif
};





UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EDeepLevelRoadConnection : uint8
{
	None = 0 UMETA(Hidden),
	PositiveX = 1 << 0,
	PositiveY = 1 << 1,
	NegativeX = 1 << 2,
	NegativeY = 1 << 3
};
ENUM_CLASS_FLAGS(EDeepLevelRoadConnection)

enum class EDeepLevelRoadTileTopology : uint8
{
	Sidewalk,
	DeadEnd,
	Straight,
	Corner,
	TJunction,
	FourWay,
	Invalid
};

USTRUCT(BlueprintType)
struct DEEPLEVELDESIGNPCG_API FDeepLevelRoadTilePlacementVolume
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Volume")
	FVector Center = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Volume")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "1.0", UIMin = "1.0"))
	FVector Extent = FVector(250.0, 250.0, 50.0);
};

USTRUCT(BlueprintType)
struct DEEPLEVELDESIGNPCG_API FDeepLevelRoadTileDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	TSoftObjectPtr<UStaticMesh> TileMesh;

	/** Optional material override exposed to the downstream PCG Static Mesh Spawner. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	TSoftObjectPtr<UMaterialInterface> TileMaterialOverride;

	/** Zero means sidewalk; any connected grid edge means road. Authored through the catalog viewport. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Connectivity", meta = (Bitmask, BitmaskEnum = "/Script/DeepLevelDesignPCG.EDeepLevelRoadConnection"))
	int32 ConnectionMask = 0;

	/** Zero means ordinary road; otherwise exactly one connected edge is the junction approach. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Connectivity", meta = (Bitmask, BitmaskEnum = "/Script/DeepLevelDesignPCG.EDeepLevelRoadConnection"))
	int32 ApproachJunctionDirectionMask = 0;

	EDeepLevelRoadTileKind GetKind() const
	{
		return ConnectionMask == 0 ? EDeepLevelRoadTileKind::Sidewalk : EDeepLevelRoadTileKind::Road;
	}

	bool IsJunctionApproach() const
	{
		return ApproachJunctionDirectionMask != 0;
	}

	EDeepLevelRoadTileTopology GetTopology() const
	{
		constexpr int32 ValidMask = static_cast<int32>(EDeepLevelRoadConnection::PositiveX)
			| static_cast<int32>(EDeepLevelRoadConnection::PositiveY)
			| static_cast<int32>(EDeepLevelRoadConnection::NegativeX)
			| static_cast<int32>(EDeepLevelRoadConnection::NegativeY);
		if ((ConnectionMask & ~ValidMask) != 0)
		{
			return EDeepLevelRoadTileTopology::Invalid;
		}
		const int32 ConnectionCount = FMath::CountBits64(static_cast<uint64>(ConnectionMask));
		switch (ConnectionCount)
		{
		case 0: return EDeepLevelRoadTileTopology::Sidewalk;
		case 1: return EDeepLevelRoadTileTopology::DeadEnd;
		case 2:
			return ConnectionMask == (static_cast<int32>(EDeepLevelRoadConnection::PositiveX) | static_cast<int32>(EDeepLevelRoadConnection::NegativeX))
				|| ConnectionMask == (static_cast<int32>(EDeepLevelRoadConnection::PositiveY) | static_cast<int32>(EDeepLevelRoadConnection::NegativeY))
				? EDeepLevelRoadTileTopology::Straight
				: EDeepLevelRoadTileTopology::Corner;
		case 3: return EDeepLevelRoadTileTopology::TJunction;
		case 4: return EDeepLevelRoadTileTopology::FourWay;
		default: return EDeepLevelRoadTileTopology::Invalid;
		}
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	FDeepLevelRoadTilePlacementVolume PlacementVolume;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selection", meta = (ClampMin = "0.0", UIMin = "0.0"))
	double SelectionWeight = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calibration")
	bool bCalibrated = false;
};

/** Authored 1x1 tile set for deterministic grid-based road networks. */
UCLASS(BlueprintType)
class DEEPLEVELDESIGNPCG_API UDeepLevelRoadTileCatalog : public UDataAsset
{
	GENERATED_BODY()

public:
	bool ValidateForGeneration(FText& OutError) const;
	double GetTileSize() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	TObjectPtr<UDeepLevelCityGridProfile> GridProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1", ClampMax = "8", UIMin = "1", UIMax = "8"))
	int32 SidewalkWidthInTiles = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tiles")
	TArray<FDeepLevelRoadTileDefinition> Tiles;
};




class UPCGSplineData;

struct FDeepLevelRoadTilePlacement
{
	TSoftObjectPtr<UStaticMesh> TileMesh;
	TSoftObjectPtr<UMaterialInterface> TileMaterialOverride;
	FTransform Transform = FTransform::Identity;
	/** World-space support plane used by city surface/edge anchors. */
	FTransform SurfaceTransform = FTransform::Identity;
	FIntPoint GridCell = FIntPoint::ZeroValue;
	EDeepLevelRoadTileKind Kind = EDeepLevelRoadTileKind::Road;
	int32 ConnectionMask = 0;
	bool bJunctionApproach = false;
};

struct FDeepLevelRoadNetworkPlan
{
	TArray<FDeepLevelRoadTilePlacement> Placements;
	int32 RoadCellCount = 0;
	int32 SidewalkCellCount = 0;
};

/** Deterministic 1x1 grid planner shared by the road PCG element and tests. */
class FDeepLevelRoadNetworkPlanner final
{
public:
	static bool BuildPlan(
		const UDeepLevelRoadTileCatalog& Catalog,
		const TArray<const UPCGSplineData*>& Splines,
		const FDeepLevelCityGrid& Grid,
		int32 Seed,
		FDeepLevelRoadNetworkPlan& OutPlan,
		FText& OutError,
		TConstArrayView<FDeepLevelRoadCellOverride> CellOverrides = {});
};
