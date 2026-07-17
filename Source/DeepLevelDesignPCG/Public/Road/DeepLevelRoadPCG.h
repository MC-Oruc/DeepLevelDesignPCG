// Copyright <--\, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PCGSettings.h"
#include "Components/BoxComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/DataAsset.h"
#include "DeepLevelRoadPCG.generated.h"





class UPCGComponent;
class UBillboardComponent;
class UDeepLevelRoadSplineComponent;
class ADeepLevelRoadNetworkActor;

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
class DEEPLEVELDESIGNPCG_API ADeepLevelRoadNetworkActor : public AActor
{
	GENERATED_BODY()

public:
	ADeepLevelRoadNetworkActor();
	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "@Deep Level Design PCG|Road Network", meta = (ClampMin = "1.0", UIMin = "1.0"))
	double GridSize = 500.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "@Deep Level Design PCG|Road Network")
	TSoftObjectPtr<UDeepLevelRoadTileCatalog> Catalog;

	UDeepLevelRoadSplineComponent* CreateRoadBranch();
	void GetRoadSplineComponents(TArray<UDeepLevelRoadSplineComponent*>& OutSplines) const;
	FVector GetGridOrigin() const;
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
	void OrganizeGeneratedRoadMeshes(UPCGComponent* GeneratedComponent);

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





class UStaticMesh;

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

UENUM(BlueprintType)
enum class EDeepLevelRoadTileKind : uint8
{
	Road,
	Sidewalk
};

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
		const int32 ConnectionCount = FMath::CountBits(static_cast<uint64>(ConnectionMask));
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1.0", UIMin = "1.0"))
	double GridCellSize = 500.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid", meta = (ClampMin = "1", ClampMax = "8", UIMin = "1", UIMax = "8"))
	int32 SidewalkWidthInTiles = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tiles")
	TArray<FDeepLevelRoadTileDefinition> Tiles;
};




class UPCGSplineData;

struct FDeepLevelRoadTilePlacement
{
	TSoftObjectPtr<UStaticMesh> TileMesh;
	FTransform Transform = FTransform::Identity;
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
		const FVector& GridOrigin,
		int32 Seed,
		FDeepLevelRoadNetworkPlan& OutPlan,
		FText& OutError);
};

