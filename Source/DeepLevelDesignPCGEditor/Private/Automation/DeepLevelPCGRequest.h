// Copyright <--\, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class SEditorViewport;
class AActor;
class UPCGComponent;
struct FDeepLevelPCGAutomationResult;

namespace DeepLevelPCGRequest
{
	bool Fields(const FJsonObject& Object, std::initializer_list<const TCHAR*> Allowed, FString& Error);
	TSharedPtr<FJsonObject> Object(const FJsonValue& Value);
	bool String(const FJsonObject& Object, const TCHAR* Key, FString& Out, FString& Error, bool Required = false);
	bool Number(const FJsonObject& Object, const TCHAR* Key, double& Out, double Min, double Max, FString& Error);
	bool Integer(const FJsonObject& Object, const TCHAR* Key, int32& Out, int32 Min, int32 Max, FString& Error);
	bool Bool(const FJsonObject& Object, const TCHAR* Key, bool& Out, FString& Error);
	bool Vector(const FJsonObject& Object, const TCHAR* Key, FVector& Out, FString& Error);
	bool Points(const FJsonObject& Object, TArray<FVector>& Out, FString& Error);
	TArray<TSharedPtr<FJsonValue>> VectorJson(const FVector& Vector);
	bool Save(UObject& Object, FString& Error);

	struct FOptions
	{
		bool bDryRun = false;
		bool bSave = false;
		bool bCapture = false;
		bool bIncludeState = true;
		double TimeoutSeconds = 120;
		FIntPoint Size = FIntPoint(1024, 768);
		TOptional<FTransform> Camera;
		float FOV = 90;
		bool Read(const FJsonObject& Root, FString& Error);
	};
	void CapturePreview(const TSharedRef<SEditorViewport>& Preview, const FOptions& Options, FDeepLevelPCGAutomationResult& Out);
	void CaptureWorld(AActor& Target, const UPCGComponent& Component, const FOptions& Options, FDeepLevelPCGAutomationResult& Out);
}
