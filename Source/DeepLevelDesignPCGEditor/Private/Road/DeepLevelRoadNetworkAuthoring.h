// Copyright <--\, Inc. All Rights Reserved.
#pragma once
#include "Road/DeepLevelRoadPCG.h"

namespace DeepLevelRoadNetworkAuthoring
{
	struct FBranch
	{
		FString Id;
		TWeakObjectPtr<UDeepLevelRoadSplineComponent> Source;
		TArray<FVector> Points;
		bool bGeometryChanged = false;
	};
	struct FState
	{
		TWeakObjectPtr<ADeepLevelCityLayoutActor> City;
		TSoftObjectPtr<UDeepLevelRoadTileCatalog> Catalog;
		FName CollisionProfile;
		TArray<FBranch> Branches;
		TArray<FDeepLevelRoadCellOverride> Overrides;
	};
	FState Read(ADeepLevelRoadNetworkActor& Actor);
	void Apply(ADeepLevelRoadNetworkActor& Actor, FState& State);
}
