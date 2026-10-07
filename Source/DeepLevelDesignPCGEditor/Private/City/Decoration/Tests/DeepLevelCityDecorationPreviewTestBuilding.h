// Copyright <--\, Inc. All Rights Reserved.
#pragma once
#include "PackedLevelActor/PackedLevelActor.h"
#include "DeepLevelCityDecorationPreviewTestBuilding.generated.h"

UCLASS(HideDropdown, NotBlueprintable, NotPlaceable, Transient)
class ADeepLevelCityDecorationPreviewTestBuilding : public APackedLevelActor
{
	GENERATED_BODY()
public:
	virtual void OnConstruction(const FTransform& Transform) override
	{
		Super::OnConstruction(Transform);
		bPreviewAtConstruction = bIsEditorPreviewActor;
	}
	bool bPreviewAtConstruction = false;
};
