// Copyright <--\, Inc. All Rights Reserved.

#pragma once

#include "Building/DeepLevelBuildingPCG.h"

namespace DeepLevelBuildingSidewalkInfill
{
	bool BuildAnchors(
		const UDeepLevelBuildingPlacementCatalog& Catalog,
		const FDeepLevelCityGrid& Grid,
		const FGuid& SourceGuid,
		int32 SourceRevision,
		const FDeepLevelBuildingLinePlan& Before,
		const FDeepLevelBuildingLinePlan& After,
		TArray<FDeepLevelCityAnchor>& OutAnchors,
		FText& OutError);
}
