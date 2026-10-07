// Copyright <--\, Inc. All Rights Reserved.
#include "Road/DeepLevelRoadCatalogAuthoring.h"
#include "Engine/StaticMesh.h"
#include "ScopedTransaction.h"

void DeepLevelRoadCatalogAuthoring::Edit(UDeepLevelRoadTileCatalog& Catalog, const FText& Label, TFunctionRef<void()> Mutation)
{
	const FScopedTransaction Transaction(Label);
	Catalog.Modify(); Mutation(); Catalog.PostEditChange(); Catalog.MarkPackageDirty();
}
bool DeepLevelRoadCatalogAuthoring::AutoFit(UStaticMesh* Mesh, double TileSize, FVector& Center, FVector& Extent)
{
	if (!Mesh || !FMath::IsFinite(TileSize) || TileSize < 2) { return false; }
	const auto Bounds = Mesh->GetBoundingBox();
	if (!Bounds.IsValid) { return false; }
	Center = Bounds.GetCenter(); Extent = FVector(TileSize * 0.5, TileSize * 0.5, FMath::Max(Bounds.GetExtent().Z, 1.0));
	return true;
}
