// Copyright <--\, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
struct FDeepLevelPCGAutomationResult;
namespace DeepLevelCityDecorationDSL
{
	FString Describe();
	void Execute(const TSharedRef<FJsonObject>& Request, FDeepLevelPCGAutomationResult& Out);
}
