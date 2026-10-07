// Copyright <--\, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** Transport-neutral output shared by automation domain executors. */
struct FDeepLevelPCGAutomationResult
{
	bool bSuccess = false;
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	TArray<FColor> Pixels;
	FIntPoint ImageSize = FIntPoint::ZeroValue;
};
