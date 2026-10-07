// Copyright <--\, Inc. All Rights Reserved.
#pragma once
#include "Automation/Data/DeepLevelPCGAutomationPlan.h"
namespace DeepLevelPCGGeneration
{
	void Run(FDeepLevelPCGAutomationPlan&& Plan, FDeepLevelPCGAutomationResult&& Result,
		TFunction<void(FDeepLevelPCGAutomationResult)>&& Completed);
	void Shutdown();
}
