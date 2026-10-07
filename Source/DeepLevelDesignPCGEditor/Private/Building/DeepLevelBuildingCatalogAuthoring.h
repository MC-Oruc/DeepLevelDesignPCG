// Copyright <--\, Inc. All Rights Reserved.
#pragma once
#include "Building/DeepLevelBuildingPCG.h"

/** Shared catalog authoring boundary for Slate and automation. */
namespace DeepLevelBuildingCatalogAuthoring
{
	void Edit(UDeepLevelBuildingPlacementCatalog& Catalog, const FText& Label, TFunctionRef<void()> Mutation);
	bool AutoFit(UClass* BuildingClass, const FRotator& Rotation, FVector& Center, FVector& Extent);
}
