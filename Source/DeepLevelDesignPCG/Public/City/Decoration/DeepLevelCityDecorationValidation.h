// Copyright <--\, Inc. All Rights Reserved.
#pragma once

#include "City/DeepLevelCityDecoration.h"

namespace DeepLevelCityDecorationValidation
{
	inline constexpr double MinimumScale = 0.01;
	DEEPLEVELDESIGNPCG_API bool ValidateTransform(const FTransform& Transform, FText& Error);
	DEEPLEVELDESIGNPCG_API bool ValidateOutput(EDeepLevelCityDecorationOutput Output,
		const TSoftObjectPtr<UStaticMesh>& Mesh, const TSoftClassPtr<AActor>& ActorClass,
		const TSoftObjectPtr<UMaterialInterface>& Material, const FVector& DecalSize, FText& Error);
}
