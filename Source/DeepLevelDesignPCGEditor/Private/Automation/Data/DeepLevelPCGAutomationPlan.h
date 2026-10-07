// Copyright <--\, Inc. All Rights Reserved.
#pragma once
#include "Automation/Data/DeepLevelPCGAutomationResult.h"
#include "UObject/StrongObjectPtr.h"
class UPCGComponent;

/** A validated domain command. Generation is deliberately outside the authoring transaction. */
struct FDeepLevelPCGAutomationPlan
{
	TStrongObjectPtr<UObject> Target;
	TWeakObjectPtr<UPCGComponent> Component;
	bool bRequiresAsync = false;
	double TimeoutSeconds = 120;
	TFunction<void(FDeepLevelPCGAutomationResult&)> Apply;
	TFunction<void()> Generate;
	TFunction<void(FDeepLevelPCGAutomationResult&)> Finish;
};
