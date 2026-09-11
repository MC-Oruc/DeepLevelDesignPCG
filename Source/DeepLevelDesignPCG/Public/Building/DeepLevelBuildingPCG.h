// Copyright <--\, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "Components/SplineComponent.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "Data/Registry/PCGGetDataFunctionRegistry.h"
#include "City/DeepLevelCityLayout.h"
#include "DeepLevelBuildingPCG.generated.h"





UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EDeepLevelCornerPlacementFlags : uint8
{
	None = 0,
	Inner = 1 << 0,
	Outer = 1 << 1,
	All = (1 << 0) | (1 << 1) UMETA(Hidden)
};
ENUM_CLASS_FLAGS(EDeepLevelCornerPlacementFlags)

namespace DeepLevelBuildingLineCornerPlacement
{
	inline const FName MetadataAttributeName = TEXT("DeepLevelCornerPlacement");
}





class UDeepLevelBuildingPlacementCatalog;

UCLASS(BlueprintType, ClassGroup = (Procedural))
class DEEPLEVELDESIGNPCG_API UDeepLevelBuildingLinePCGSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override;
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spawner; }
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output")
	FName ActorClassAttribute = TEXT("ActorClass");

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};





/** Authored building line spline and its per-spline generation policy. */
UCLASS(ClassGroup = (Procedural), meta = (BlueprintSpawnableComponent))
class DEEPLEVELDESIGNPCG_API UDeepLevelBuildingLineSplineComponent : public USplineComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deep Level Design PCG|BuildingLine", meta = (DisplayName = "Corner Placement", Bitmask, BitmaskEnum = "/Script/DeepLevelDesignPCG.EDeepLevelCornerPlacementFlags"))
	int32 CornerPlacementMask = static_cast<int32>(EDeepLevelCornerPlacementFlags::Inner);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};





class AActor;

UENUM(BlueprintType)
enum class EDeepLevelBuildingVolumeFace : uint8
{
	PositiveX,
	NegativeX,
	PositiveY,
	NegativeY
};

UENUM(BlueprintType)
enum class EDeepLevelStreetExposureRule : uint8
{
	Required,
	Preferred,
	Neutral,
	Forbidden
};

USTRUCT(BlueprintType)
struct DEEPLEVELDESIGNPCG_API FDeepLevelBuildingFaceExposureRules
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Exposure")
	EDeepLevelStreetExposureRule PositiveX = EDeepLevelStreetExposureRule::Neutral;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Exposure")
	EDeepLevelStreetExposureRule NegativeX = EDeepLevelStreetExposureRule::Neutral;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Exposure")
	EDeepLevelStreetExposureRule PositiveY = EDeepLevelStreetExposureRule::Neutral;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Exposure")
	EDeepLevelStreetExposureRule NegativeY = EDeepLevelStreetExposureRule::Neutral;
};

USTRUCT(BlueprintType)
struct DEEPLEVELDESIGNPCG_API FDeepLevelBuildingPlacementVolume
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Volume")
	FVector Center = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Volume")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Volume", meta = (ClampMin = "1.0", UIMin = "1.0"))
	FVector Extent = FVector(500.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Volume")
	FDeepLevelBuildingFaceExposureRules Exposure;
};

USTRUCT(BlueprintType)
struct DEEPLEVELDESIGNPCG_API FDeepLevelBuildingPlacementDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building", meta = (AllowedClasses = "/Script/Engine.PackedLevelActor"))
	TSoftClassPtr<AActor> BuildingClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	FDeepLevelBuildingPlacementVolume PlacementVolume;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selection", meta = (ClampMin = "0.0", UIMin = "0.0"))
	double SelectionWeight = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calibration")
	bool bCalibrated = false;
};

USTRUCT(BlueprintType)
struct DEEPLEVELDESIGNPCG_API FDeepLevelBuildingSequencePreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset", meta = (ClampMin = "0.0", UIMin = "0.0"))
	double SelectionWeight = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset", meta = (AllowedClasses = "/Script/Engine.PackedLevelActor"))
	TArray<TSoftClassPtr<AActor>> Buildings;
};

/** Single source of truth for spline-based building placement. */
UCLASS(BlueprintType)
class DEEPLEVELDESIGNPCG_API UDeepLevelBuildingPlacementCatalog : public UDataAsset
{
	GENERATED_BODY()

public:
	bool ValidateForGeneration(FText& OutError) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buildings")
	TArray<FDeepLevelBuildingPlacementDefinition> Buildings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presets")
	TArray<FDeepLevelBuildingSequencePreset> Presets;
};




struct DEEPLEVELDESIGNPCG_API FDeepLevelResolvedBuildingGeometry
{
	EDeepLevelBuildingVolumeFace StreetFace = EDeepLevelBuildingVolumeFace::NegativeY;
	double HalfWidth = 0.0;
	double HalfDepth = 0.0;
};

class DEEPLEVELDESIGNPCG_API FDeepLevelBuildingPlacementGeometry
{
public:
	static bool Validate(const FDeepLevelBuildingPlacementDefinition& Definition, FText& OutError);
	static EDeepLevelStreetExposureRule GetExposureRule(const FDeepLevelBuildingFaceExposureRules& Rules, EDeepLevelBuildingVolumeFace Face);
	static bool ResolveStreetFace(const FDeepLevelBuildingPlacementDefinition& Definition, int32 Seed, EDeepLevelBuildingVolumeFace& OutFace);
	static int32 MakeStreetFaceSeed(int32 Seed, int32 SelectionIndex, int32 ModuleKey, int32 ElementIndex);
	static FDeepLevelResolvedBuildingGeometry ResolveGeometry(const FDeepLevelBuildingPlacementDefinition& Definition, EDeepLevelBuildingVolumeFace Face);
	static bool ResizeVolumeFace(FDeepLevelBuildingPlacementVolume& Volume, EDeepLevelBuildingVolumeFace Face, double Distance);
	static FTransform BuildActorTransform(
		const FDeepLevelBuildingPlacementDefinition& Definition,
		EDeepLevelBuildingVolumeFace StreetFace,
		const FVector& SplineLocation,
		const FVector& SplineForward,
		const FVector& SplineRight);
};





class UPCGComponent;
class UDeepLevelBuildingLineSplineComponent;
struct FDeepLevelBuildingLinePlan;

/** Authoring actor for one open or closed building frontage. Buildings spawn on spline-right. */
UCLASS(BlueprintType, ClassGroup = (Procedural))
class DEEPLEVELDESIGNPCG_API ADeepLevelPCGBuildingLineActor : public AActor, public IDeepLevelCityLayoutProvider
{
	GENERATED_BODY()

public:
	ADeepLevelPCGBuildingLineActor();
	virtual void PostLoad() override;
	virtual void PostActorCreated() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deep Level Design PCG|Building Line")
	TObjectPtr<ADeepLevelCityLayoutActor> CityLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deep Level Design PCG|Building Line")
	TSoftObjectPtr<UDeepLevelBuildingPlacementCatalog> Catalog;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deep Level Design PCG|Building Line")
	int32 RandomSeed = 1337;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deep Level Design PCG|Building Line|Variation", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	double VarietyStrength = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deep Level Design PCG|Building Line|Variation", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	double CornerPreference = 1.0;

	bool ResolveCityGrid(FDeepLevelCityGrid& OutGrid, FText& OutError) const;
	virtual bool BuildCityLayoutFragment(
		const FDeepLevelCityGrid& Grid,
		FDeepLevelCityLayoutFragment& OutFragment,
		FText& OutError) const override;
	void NotifyBuildingLayoutChanged();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deep Level Design PCG|PCG|Components")
	TObjectPtr<UDeepLevelBuildingLineSplineComponent> BuildingLine;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deep Level Design PCG|PCG|Components")
	TObjectPtr<UPCGComponent> PCGComponent;

private:
	void EnsureLayoutSourceGuid();
	bool BuildPlan(FDeepLevelBuildingLinePlan& OutPlan, FText& OutError) const;

	UPROPERTY(VisibleAnywhere, Category = "Deep Level Design PCG|Building Line")
	FGuid LayoutSourceGuid;

	UPROPERTY(VisibleAnywhere, Category = "Deep Level Design PCG|Building Line")
	int32 LayoutRevision = 0;
};

struct FDeepLevelBuildingLinePathSample
{
	FVector Location = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	FVector Right = FVector::RightVector;
};

namespace DeepLevelBuildingLinePacking
{
	struct FClearanceShape;
	struct FElementVariant;
	struct FFacadeSegment;
	struct FFootprint;
	struct FModuleVariant;
	struct FSelectionHistory;
}




namespace DeepLevelBuildingLinePacking
{
	class FCatalogModel final
	{
	public:
		static bool Build(
			const UDeepLevelBuildingPlacementCatalog& Catalog,
			TArray<FModuleVariant>& OutVariants,
			int32& OutModuleCount,
			FText& OutError);

		static FSelectionHistory MakeHistory(int32 BuildingCount, int32 ModuleCount);
		static double ExposureScore(EDeepLevelStreetExposureRule Rule);
		static uint8 MakeFaceMask(EDeepLevelBuildingVolumeFace Face);
		static EDeepLevelBuildingVolumeFace GetAdjacentFace(EDeepLevelBuildingVolumeFace PrimaryFace, bool bForwardEnd);
		static bool IsSpanFaceValid(
			const FDeepLevelBuildingPlacementDefinition& Definition,
			EDeepLevelBuildingVolumeFace PrimaryFace);
		static bool IsCornerFaceValid(
			const FDeepLevelBuildingPlacementDefinition& Definition,
			EDeepLevelBuildingVolumeFace PrimaryFace,
			bool bForwardEnd);
		static bool ValidateExposure(
			const FDeepLevelBuildingPlacementDefinition& Definition,
			uint8 ExposedFaceMask);
		static double CornerExposureScore(
			const FDeepLevelBuildingPlacementDefinition& Definition,
			EDeepLevelBuildingVolumeFace PrimaryFace,
			bool bForwardEnd);
		static void GetEligibleFaces(
			const FDeepLevelBuildingPlacementDefinition& Definition,
			TArray<EDeepLevelBuildingVolumeFace>& OutFaces);
		static FElementVariant MakeElementVariant(
			const FDeepLevelBuildingPlacementDefinition& Definition,
			int32 BuildingIndex,
			EDeepLevelBuildingVolumeFace Face);

	private:
		static bool AddModuleVariants(
			const TArray<int32>& BuildingIndices,
			int32 ModuleIndex,
			double Weight,
			const UDeepLevelBuildingPlacementCatalog& Catalog,
			TArray<FModuleVariant>& OutVariants);
	};
}




namespace DeepLevelBuildingLinePacking
{
	class FClearance final
	{
	public:
		static double Cross2D(const FVector2D& A, const FVector2D& B);
		static bool FacadesIntersect(const FFacadeSegment& A, const FFacadeSegment& B);
		static double ProjectRadius(const FFootprint& Footprint, const FVector2D& Axis);
		static bool FootprintsOverlap(const FFootprint& A, const FFootprint& B);
		static bool IntersectsAny(const FClearanceShape& Shape, const TArray<FClearanceShape>& ExistingShapes);
		static FClearanceShape MakeShape(
			const FDeepLevelBuildingLinePathSample& Sample,
			double HalfWidth,
			double HalfDepth,
			double HalfHeight);
	};
}




class UPCGSplineData;

/** Resolves node and per-spline corner placement policy. */
class FDeepLevelBuildingLineCornerPolicy
{
public:
	static bool Resolve(
		const UPCGSplineData& Spline,
		int32 NodeMask,
		EDeepLevelCornerPlacementFlags& OutFlags,
		FText& OutError);
};




class FDeepLevelBuildingLinePath;
class UDeepLevelBuildingPlacementCatalog;
struct FDeepLevelBuildingLinePlan;

/** Internal deterministic optimizer for spline building packing. */
class FDeepLevelBuildingLinePackingSolver
{
public:
	static bool Solve(
		const UDeepLevelBuildingPlacementCatalog& Catalog,
		const FDeepLevelBuildingLinePath& Path,
		int32 Seed,
		double VarietyStrength,
		double CornerPreference,
		EDeepLevelCornerPlacementFlags CornerPlacement,
		FDeepLevelBuildingLinePlan& OutPlan,
		FText& OutError);
};




namespace DeepLevelBuildingLinePacking
{
	constexpr double PackingResolution = 10.0;
	constexpr double IntersectionTolerance = 1.0;
	constexpr int32 MaximumFaceVariantsPerModule = 4;
	constexpr int32 MaximumStatesPerWidth = 4;
	constexpr int32 MaximumLayoutCandidates = 64;
	constexpr int32 MaximumPlacementCount = 512;

	struct FFacadeSegment
	{
		FVector2D Start = FVector2D::ZeroVector;
		FVector2D End = FVector2D::ZeroVector;
	};

	struct FFootprint
	{
		FVector2D Center = FVector2D::ZeroVector;
		FVector2D Forward = FVector2D::UnitX();
		FVector2D Right = FVector2D::UnitY();
		double HalfWidth = 0.0;
		double HalfDepth = 0.0;
		double MinZ = 0.0;
		double MaxZ = 0.0;
	};

	struct FClearanceShape
	{
		FFacadeSegment Facade;
		FFootprint Footprint;
	};

	struct FElementVariant
	{
		int32 BuildingIndex = INDEX_NONE;
		EDeepLevelBuildingVolumeFace Face = EDeepLevelBuildingVolumeFace::NegativeY;
		uint8 ExposedFaceMask = 0;
		double HalfWidth = 0.0;
		double HalfDepth = 0.0;
		double HalfHeight = 0.0;
		double ExposureScore = 1.0;
	};

	struct FModuleVariant
	{
		int32 ModuleIndex = INDEX_NONE;
		double Weight = 1.0;
		double Width = 0.0;
		uint32 Signature = 0;
		TArray<FElementVariant> Elements;
	};

	struct FSelectionHistory
	{
		TArray<double> FirstBuildingDistance;
		TArray<double> LastBuildingDistance;
		TArray<double> FirstModuleDistance;
		TArray<double> LastModuleDistance;
		TArray<EDeepLevelBuildingVolumeFace> LastBuildingFace;
		TBitArray<> HasBuildingFace;
	};

	struct FResolvedElement
	{
		int32 BuildingIndex = INDEX_NONE;
		EDeepLevelBuildingVolumeFace Face = EDeepLevelBuildingVolumeFace::NegativeY;
		uint8 ExposedFaceMask = 0;
		double Distance = 0.0;
		double CoverageStart = 0.0;
		double CoverageEnd = 0.0;
		FDeepLevelBuildingLinePathSample PathSample;
		FClearanceShape Shape;
		bool bCornerPlacement = false;
	};
}




class UPCGPolyLineData;

/** Validated arc-length view over an open or closed PCG polyline. */
class FDeepLevelBuildingLinePath
{
public:
	static bool Build(const UPCGPolyLineData& Data, FDeepLevelBuildingLinePath& OutPath, FText& OutError);

	bool Sample(double Distance, FDeepLevelBuildingLinePathSample& OutSample) const;
	double GetTurnAngleDegrees(double StartDistance, double EndDistance) const;
	void GetHardCornerDistances(double MinimumAngleDegrees, TArray<double>& OutDistances) const;
	bool IsStraight(double AngleToleranceDegrees = 1.0) const;
	bool IsClosed() const { return bClosed; }
	double GetLength() const { return TotalLength; }

private:
	const UPCGPolyLineData* Data = nullptr;
	TArray<double> SegmentStartDistances;
	TArray<double> SegmentLengths;
	double TotalLength = 0.0;
	bool bClosed = false;
};




namespace DeepLevelBuildingLinePCGDataInterop
{
	bool GetDataFromComponent(
		FPCGContext* Context,
		const FPCGGetDataFunctionRegistryParams& Params,
		UActorComponent* Component,
		FPCGGetDataFunctionRegistryOutput& Output);
}




struct DEEPLEVELDESIGNPCG_API FDeepLevelBuildingLinePlacement
{
	TSoftClassPtr<AActor> BuildingClass;
	double Distance = 0.0;
	double CoverageStart = 0.0;
	double CoverageEnd = 0.0;
	EDeepLevelBuildingVolumeFace StreetFace = EDeepLevelBuildingVolumeFace::NegativeY;
	FDeepLevelBuildingLinePathSample PathSample;
	bool bCornerPlacement = false;
};

struct DEEPLEVELDESIGNPCG_API FDeepLevelBuildingLinePlan
{
	TArray<FDeepLevelBuildingLinePlacement> Placements;
	double UsedLength = 0.0;
	double StartOffset = 0.0;
};

/** Deterministic packing policy shared by the PCG element and automation tests. */
class DEEPLEVELDESIGNPCG_API FDeepLevelBuildingLinePlanner
{
public:
	static bool BuildPlan(
		const UDeepLevelBuildingPlacementCatalog& Catalog,
		const FDeepLevelBuildingLinePath& Path,
		int32 Seed,
		double VarietyStrength,
		double CornerPreference,
		EDeepLevelCornerPlacementFlags CornerPlacement,
		FDeepLevelBuildingLinePlan& OutPlan,
		FText& OutError);
};




namespace DeepLevelBuildingLinePacking
{
	class FSpanPacker final
	{
	public:
		static bool Plan(
			const FDeepLevelBuildingLinePath& Path,
			double SpanStart,
			double SpanEnd,
			bool bDistributeSlack,
			const TArray<FModuleVariant>& Variants,
			const UDeepLevelBuildingPlacementCatalog& Catalog,
			int32 Seed,
			int32 SelectionIndex,
			double VarietyStrength,
			bool bHasBuildingAlternatives,
			bool bCloseLoop,
			const FSelectionHistory& InitialHistory,
			TArray<FClearanceShape>& InOutShapes,
			TArray<FResolvedElement>& OutElements,
			FSelectionHistory& OutHistory);
	};
}
