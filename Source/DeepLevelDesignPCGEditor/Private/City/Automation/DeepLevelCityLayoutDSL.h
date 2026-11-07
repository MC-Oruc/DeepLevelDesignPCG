#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
struct FDeepLevelPCGAutomationResult;

namespace DeepLevelCityLayoutDSL
{
	FString Describe();
	void Execute(const TSharedRef<FJsonObject>& Root, FDeepLevelPCGAutomationResult& Out);
}
