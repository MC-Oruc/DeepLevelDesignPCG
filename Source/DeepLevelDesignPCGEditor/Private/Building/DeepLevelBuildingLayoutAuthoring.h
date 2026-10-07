// Copyright <--\, Inc. All Rights Reserved.
#pragma once
#include "Building/DeepLevelBuildingPCG.h"

namespace DeepLevelBuildingLayoutAuthoring
{
	struct FFrontage
	{
		FGuid Id, ReplacedId;
		EDeepLevelRoadsideFrontageKind Kind = EDeepLevelRoadsideFrontageKind::Add;
		TWeakObjectPtr<UDeepLevelRoadsideFrontageSplineComponent> Source;
		TArray<FVector> Points;
		bool bClosed = false, bExcluded = false;
		bool bGeometryChanged = false;
	};
	struct FState
	{
		TWeakObjectPtr<ADeepLevelCityLayoutActor> City;
		TSoftObjectPtr<UDeepLevelBuildingPlacementCatalog> Catalog;
		int32 Seed = 1337, CornerMask = 1;
		double Variety = 1, CornerPreference = 1, Setback = -200, DepthTolerance = 500, BoundaryMargin = 5;
		TArray<FVector> LinePoints;
		bool bClosed = false;
		bool bLineChanged = false;
		TArray<FFrontage> Frontages;
	};
	FState Read(AActor& Actor);
	void Apply(AActor& Actor, FState& State, bool bRebuildFrontages);
}
