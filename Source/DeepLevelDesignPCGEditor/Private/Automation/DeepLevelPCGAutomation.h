// Copyright <--\, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

#include "Automation/Data/DeepLevelPCGAutomationResult.h"

namespace DeepLevelPCGAutomation
{
	FString Describe();
	FDeepLevelPCGAutomationResult Execute(const FString& Request);
	void ExecuteAsync(const FString& Request, TFunction<void(FDeepLevelPCGAutomationResult)>&& Completed);
	FString ToJson(const TSharedRef<FJsonObject>& Object);
}
