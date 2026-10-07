// Copyright <--\, Inc. All Rights Reserved.
#include "Building/DeepLevelBuildingCatalogAuthoring.h"
#include "AdvancedPreviewScene.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "ScopedTransaction.h"

void DeepLevelBuildingCatalogAuthoring::Edit(UDeepLevelBuildingPlacementCatalog& Catalog, const FText& Label, TFunctionRef<void()> Mutation)
{
	const FScopedTransaction Transaction(Label);
	Catalog.Modify(); Mutation(); Catalog.PostEditChange(); Catalog.MarkPackageDirty();
}

bool DeepLevelBuildingCatalogAuthoring::AutoFit(UClass* BuildingClass, const FRotator& Rotation, FVector& Center, FVector& Extent)
{
	if (!BuildingClass || !BuildingClass->IsChildOf(AActor::StaticClass())) { return false; }
	FAdvancedPreviewScene Scene{FPreviewScene::ConstructionValues()};
	FActorSpawnParameters Params; Params.ObjectFlags = RF_Transient;
	AActor* Actor = Scene.GetWorld()->SpawnActor<AActor>(BuildingClass, FTransform::Identity, Params);
	if (!Actor) { return false; }
	FBox LocalBox(ForceInit);
	const FQuat InverseRotation = Rotation.Quaternion().Inverse();
	TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
	for (const auto* Component : Components)
	{
		if (!Component || !Component->IsRegistered()) { continue; }
		const FBox Box = Component->Bounds.GetBox();
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			LocalBox += InverseRotation.RotateVector(FVector((Corner & 1) ? Box.Max.X : Box.Min.X,
				(Corner & 2) ? Box.Max.Y : Box.Min.Y, (Corner & 4) ? Box.Max.Z : Box.Min.Z));
		}
	}
	Scene.GetWorld()->DestroyActor(Actor);
	if (!LocalBox.IsValid) { return false; }
	Center = Rotation.Quaternion().RotateVector(LocalBox.GetCenter()); Extent = LocalBox.GetExtent().ComponentMax(FVector(1));
	return true;
}
