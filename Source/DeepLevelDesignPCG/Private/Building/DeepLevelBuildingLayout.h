// Copyright <--\, Inc. All Rights Reserved.

#pragma once

#include "Building/DeepLevelBuildingPCG.h"

struct FDeepLevelBuildingPreparedLayout
{
	FDeepLevelBuildingLinePlan Plan;
	FDeepLevelCityLayoutFragment Fragment;
	TArray<FTransform> Transforms;
	uint32 InputKey = 0;
	int32 RejectedCount = 0;
};

namespace DeepLevelBuildingRoadside
{
	bool BuildPlan(const FDeepLevelCityLayoutSnapshot& Base, const UDeepLevelBuildingPlacementCatalog& Catalog,
		const FGuid& BuildingSource, int32 Seed, double Variety, double CornerPreference,
		EDeepLevelCornerPlacementFlags Corners, TConstArrayView<FBox2D> ExclusionBounds, FDeepLevelBuildingLinePlan& OutPlan,
		int32& OutRejectedCount, FText& OutError);
}

namespace DeepLevelBuildingLayoutGeometry
{
	using FFootprint = TStaticArray<FVector2D, 4>;
	void MakeFootprintCorners(const FDeepLevelBuildingPlacementVolume& Volume, const FTransform& ActorTransform, FFootprint& OutCorners);
	void RasterizeFootprint(const FDeepLevelCityGrid& Grid, const FFootprint& Footprint, TArray<FIntPoint>& OutCells);
	bool FootprintsOverlap(const FFootprint& A, const FFootprint& B);
}
