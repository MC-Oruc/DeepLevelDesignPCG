// Copyright <--\, Inc. All Rights Reserved.
#pragma once
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "ToolsetRegistry/ToolsetImage.h"
#include "DeepLevelPCGToolset.generated.h"

USTRUCT()
struct FDeepLevelPCGToolResult
{
	GENERATED_BODY()
	UPROPERTY()
	bool Success = false;
	UPROPERTY()
	FString Report;
	UPROPERTY()
	FToolsetImage Image;
};

/** Batched DeepLevelDesignPCG authoring DSL. Describe once; Execute edits, validates and optionally captures in one call. */
UCLASS()
class UDeepLevelPCGToolset : public UToolsetDefinition
{
	GENERATED_BODY()
public:
	/** Returns the versioned DSL contract, supported operations and examples. */
	UFUNCTION(meta = (AICallable))
	static FString Describe();
	/** Executes a JSON DSL request. capture=true returns the updated decoration preview in this same response. */
	UFUNCTION(meta = (AICallable))
	static FDeepLevelPCGToolResult Execute(const FString& Request);
};
