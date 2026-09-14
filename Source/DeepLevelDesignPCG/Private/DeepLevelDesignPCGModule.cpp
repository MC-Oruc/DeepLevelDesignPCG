// Copyright <--\, Inc. All Rights Reserved.

#include "DeepLevelDesignPCGModule.h"

#include "Building/DeepLevelBuildingPCG.h"
#include "PCGModule.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
FDeepLevelGenerationFailed& FDeepLevelDesignPCGEditorEvents::OnGenerationFailed()
{
	static FDeepLevelGenerationFailed Event;
	return Event;
}

FDeepLevelGenerationWarning& FDeepLevelDesignPCGEditorEvents::OnGenerationWarning()
{
	static FDeepLevelGenerationWarning Event;
	return Event;
}
#endif

void FDeepLevelDesignPCGModule::StartupModule()
{
	FModuleManager::LoadModuleChecked<FPCGModule>(TEXT("PCG"));
	BuildingLineDataFunctionHandle = FPCGModule::MutableGetDataFunctionRegistry()
		.RegisterDataFromComponentFunction(&DeepLevelBuildingLinePCGDataInterop::GetDataFromComponent);
}

void FDeepLevelDesignPCGModule::ShutdownModule()
{
	if (FPCGModule::IsPCGModuleLoaded() && BuildingLineDataFunctionHandle != InvalidFunctionHandle)
	{
		FPCGModule::MutableGetDataFunctionRegistry()
			.UnregisterDataFromComponentFunction(BuildingLineDataFunctionHandle);
	}
	BuildingLineDataFunctionHandle = InvalidFunctionHandle;
}

IMPLEMENT_MODULE(FDeepLevelDesignPCGModule, DeepLevelDesignPCG)
