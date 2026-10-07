// Copyright <--\, Inc. All Rights Reserved.
#pragma once
#include "Road/DeepLevelRoadPCG.h"
namespace DeepLevelRoadCatalogAuthoring
{
	void Edit(UDeepLevelRoadTileCatalog& Catalog, const FText& Label, TFunctionRef<void()> Mutation);
	bool AutoFit(UStaticMesh* Mesh, double TileSize, FVector& Center, FVector& Extent);
}
