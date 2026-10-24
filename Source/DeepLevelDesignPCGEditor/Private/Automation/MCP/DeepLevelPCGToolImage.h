// Copyright <--\, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

struct FToolsetImage;

namespace DeepLevelPCGToolImage
{
	bool Encode(const TArray<FColor>& Pixels, FIntPoint Size, FToolsetImage& Image, FIntPoint& EncodedSize);
}
