// Copyright <--\, Inc. All Rights Reserved.
#pragma once
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "ToolsetRegistry/ToolsetImage.h"
#include "ToolsetRegistry/ToolCallAsyncResult.h"
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

UCLASS()
class UDeepLevelPCGToolCall : public UToolCallAsyncResult
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FDeepLevelPCGToolResult Value;
	void Complete(FDeepLevelPCGToolResult&& Result) { MaybeBroadcastSuccessfulCompletion(MoveTemp(Result), Value); }
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
	/** Executes a JSON DSL batch, waits for requested PCG generation, and optionally returns a fresh image. */
	UFUNCTION(meta = (AICallable))
	static UDeepLevelPCGToolCall* Execute(const FString& Request);
};
