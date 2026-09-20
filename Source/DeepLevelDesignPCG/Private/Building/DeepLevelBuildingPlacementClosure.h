// Copyright <--\, Inc. All Rights Reserved.

#pragma once

#include "Building/DeepLevelBuildingPCG.h"

struct FDeepLevelBuildingFinalClosureStats
{
	int32 PhaseCount = 0;
	int32 MovedPlacementCount = 0;
	double TotalShift = 0.0;
};

class FDeepLevelBuildingPlacementClosure final
{
public:
	static bool ApplyFinal(
		const UDeepLevelBuildingPlacementCatalog& Catalog,
		const TArray<FVector2D>& BlockPolygon,
		double BoundaryMargin,
		FDeepLevelBuildingLinePlan& InOutPlan,
		FDeepLevelBuildingFinalClosureStats& OutStats,
		FText& OutError);
};
