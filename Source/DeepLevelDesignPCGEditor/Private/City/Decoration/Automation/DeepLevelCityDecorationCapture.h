// Copyright <--\, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
class UDeepLevelCityBuildingDecorationProfile;
struct FDeepLevelPCGAutomationResult;
struct FDeepLevelCityDecorationCaptureOptions
{
	FIntPoint Size = FIntPoint(1024, 768);
	TOptional<FTransform> Camera;
	float FOV = 90;
};
namespace DeepLevelCityDecorationCapture
{
	void Capture(const UDeepLevelCityBuildingDecorationProfile& Profile, const FGuid& Variant,
		const FDeepLevelCityDecorationCaptureOptions& Options, FDeepLevelPCGAutomationResult& Out);
}
