// Copyright <--\, Inc. All Rights Reserved.
#pragma once
#include "Automation/Data/DeepLevelPCGAutomationPlan.h"
namespace DeepLevelRoadDSL
{
	FString Describe();
	bool Prepare(const TSharedRef<FJsonObject>& Root, FDeepLevelPCGAutomationPlan& Plan, FDeepLevelPCGAutomationResult& Out);
}
