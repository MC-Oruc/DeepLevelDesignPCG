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
	struct FFrontageSpline
	{
		FGuid FrontageId;
		TArray<FVector> Points;
		bool bClosed = false;
	};

	bool BuildFrontageSplines(const FDeepLevelCityLayoutSnapshot& Base,
		TArray<FFrontageSpline>& OutSplines, FText& OutError, double FrontageSetback = 0.0);

}

namespace DeepLevelBuildingLayoutGeometry
{
	using FFootprint = TStaticArray<FVector2D, 4>;
	void MakeFootprintCorners(const FDeepLevelBuildingPlacementVolume& Volume, const FTransform& ActorTransform, FFootprint& OutCorners);
	void RasterizeFootprint(const FDeepLevelCityGrid& Grid, const FFootprint& Footprint, TArray<FIntPoint>& OutCells);
	bool FootprintsOverlap(const FFootprint& A, const FFootprint& B);
}
