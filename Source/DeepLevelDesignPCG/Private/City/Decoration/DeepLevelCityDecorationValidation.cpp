// Copyright <--\, Inc. All Rights Reserved.
#include "City/Decoration/DeepLevelCityDecorationValidation.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"

#define LOCTEXT_NAMESPACE "DeepLevelCityDecorationValidation"

bool DeepLevelCityDecorationValidation::ValidateTransform(const FTransform& Transform, FText& Error)
{
	Error = FText::GetEmpty();
	if (Transform.IsValid() && Transform.GetScale3D().GetAbs().GetMin() >= MinimumScale) { return true; }
	Error = LOCTEXT("Transform", "Transform must be finite, normalized, and have at least 0.01 absolute scale on each axis.");
	return false;
}

bool DeepLevelCityDecorationValidation::ValidateOutput(EDeepLevelCityDecorationOutput Output,
	const TSoftObjectPtr<UStaticMesh>& Mesh, const TSoftClassPtr<AActor>& ActorClass,
	const TSoftObjectPtr<UMaterialInterface>& Material, const FVector& DecalSize, FText& Error)
{
	Error = FText::GetEmpty();
	switch (Output)
	{
	case EDeepLevelCityDecorationOutput::Mesh:
		if (Mesh.LoadSynchronous()) { return true; }
		Error = LOCTEXT("Mesh", "Choose an existing Static Mesh.");
		break;
	case EDeepLevelCityDecorationOutput::Actor:
		if (UClass* Class = ActorClass.LoadSynchronous())
		{
			if (Class->IsChildOf(AActor::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) { return true; }
		}
		Error = LOCTEXT("Actor", "Choose an existing, concrete actor class.");
		break;
	case EDeepLevelCityDecorationOutput::Decal:
		if (!DecalSize.ContainsNaN() && DecalSize.GetMin() > 0.0)
		{
			if (UMaterialInterface* Loaded = Material.LoadSynchronous())
			{
				if (Loaded->GetMaterial() && Loaded->GetMaterial()->MaterialDomain == MD_DeferredDecal) { return true; }
			}
		}
		Error = LOCTEXT("Decal", "Choose a Deferred Decal material and positive, finite decal dimensions.");
		break;
	default:
		Error = LOCTEXT("Output", "Unknown decoration output type.");
		break;
	}
	return false;
}
#undef LOCTEXT_NAMESPACE
