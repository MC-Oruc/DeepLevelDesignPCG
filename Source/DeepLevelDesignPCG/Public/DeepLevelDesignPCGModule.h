// Copyright <--\, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

#include "Data/Registry/PCGGetDataFunctionRegistry.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(
	FDeepLevelGenerationFailed,
	const FText&,
	const FText&);

DECLARE_MULTICAST_DELEGATE_TwoParams(
	FDeepLevelGenerationWarning,
	const FText&,
	const FText&);

class DEEPLEVELDESIGNPCG_API FDeepLevelDesignPCGEditorEvents final
{
public:
#if WITH_EDITOR
	static FDeepLevelGenerationFailed& OnGenerationFailed();
	static FDeepLevelGenerationWarning& OnGenerationWarning();
#endif
};

class FDeepLevelDesignPCGModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	static constexpr FPCGGetDataFunctionRegistry::FFunctionHandle InvalidFunctionHandle = MAX_uint64;
	FPCGGetDataFunctionRegistry::FFunctionHandle BuildingLineDataFunctionHandle = InvalidFunctionHandle;
};
