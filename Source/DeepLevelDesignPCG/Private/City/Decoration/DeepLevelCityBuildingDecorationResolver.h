// Copyright <--\, Inc. All Rights Reserved.

#pragma once

#include "City/DeepLevelCityDecoration.h"

namespace DeepLevelCityBuildingDecoration
{
	void Resolve(const FDeepLevelCityLayoutSnapshot& Snapshot,
		const UDeepLevelCityDecorationSet& ValidatedSet, int32 Seed,
		TArray<FDeepLevelCityResolvedDecoration>& OutPlacements);
}
