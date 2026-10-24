// Copyright <--\, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DeepLevelBuildingFacadeVisibility.generated.h"

struct FDeepLevelCityBuilding;
struct FDeepLevelCityGrid;
struct FDeepLevelCityCellState;

UENUM(BlueprintType)
enum class EDeepLevelBuildingVolumeFace : uint8
{
	PositiveX,
	NegativeX,
	PositiveY,
	NegativeY
};

USTRUCT(BlueprintType)
struct DEEPLEVELDESIGNPCG_API FDeepLevelBuildingFacadeGeometry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facade")
	FTransform LocalFrame = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facade")
	FVector Extent = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facade")
	uint8 ForbiddenFaces = 0;

	bool IsValid() const;
};

USTRUCT(BlueprintType)
struct DEEPLEVELDESIGNPCG_API FDeepLevelBuildingFacadeSpan
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facade")
	EDeepLevelBuildingVolumeFace Face = EDeepLevelBuildingVolumeFace::PositiveX;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facade")
	double Start = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facade")
	double End = 1.0;
};

namespace DeepLevelBuildingFacades
{
	DEEPLEVELDESIGNPCG_API bool Resolve(
		TArray<FDeepLevelCityBuilding>& Buildings,
		const FDeepLevelCityGrid& Grid,
		const TMap<FIntPoint, FDeepLevelCityCellState>& Cells,
		FText& OutError);

	DEEPLEVELDESIGNPCG_API bool IsPointVisible(
		const FDeepLevelCityBuilding& Building,
		EDeepLevelBuildingVolumeFace Face,
		const FVector& ActorLocalPoint);

	DEEPLEVELDESIGNPCG_API EDeepLevelBuildingVolumeFace FindNearestFace(
		const FDeepLevelBuildingFacadeGeometry& Geometry,
		const FVector& ActorLocalPoint);
}
