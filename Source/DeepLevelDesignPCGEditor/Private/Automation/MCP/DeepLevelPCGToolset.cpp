// Copyright <--\, Inc. All Rights Reserved.
#include "Automation/MCP/DeepLevelPCGToolset.h"
#include "Automation/MCP/DeepLevelPCGToolImage.h"
#include "Automation/DeepLevelPCGAutomation.h"
#include "UObject/StrongObjectPtr.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(DeepLevelPCGToolset)

FString UDeepLevelPCGToolset::Describe() { return DeepLevelPCGAutomation::Describe(); }
UDeepLevelPCGToolCall* UDeepLevelPCGToolset::Execute(const FString& Request)
{
	auto* Call = NewObject<UDeepLevelPCGToolCall>();
	DeepLevelPCGAutomation::ExecuteAsync(Request, [Owner = TStrongObjectPtr<UDeepLevelPCGToolCall>(Call)](FDeepLevelPCGAutomationResult Result)
	{
		FDeepLevelPCGToolResult Out; Out.Success = Result.bSuccess;
		FIntPoint EncodedSize;
		if (!Result.Pixels.IsEmpty() && !DeepLevelPCGToolImage::Encode(Result.Pixels, Result.ImageSize, Out.Image, EncodedSize))
		{
			Result.Report->SetStringField(TEXT("captureError"), TEXT("JPEG encoding failed; application status is unchanged."));
			Result.Report->SetBoolField(TEXT("captured"), false);
		}
		else if (!Result.Pixels.IsEmpty())
		{
			const TSharedPtr<FJsonObject>* Camera;
			if (Result.Report->TryGetObjectField(TEXT("camera"), Camera))
			{
				(*Camera)->SetNumberField(TEXT("width"), EncodedSize.X);
				(*Camera)->SetNumberField(TEXT("height"), EncodedSize.Y);
			}
			const TArray<TSharedPtr<FJsonValue>>* Labels;
			if (Result.Report->TryGetArrayField(TEXT("labels"), Labels))
			{
				for (const TSharedPtr<FJsonValue>& Label : *Labels)
				{
					const TSharedPtr<FJsonObject>& Object = Label->AsObject();
					const TArray<TSharedPtr<FJsonValue>>& Pixel = Object->GetArrayField(TEXT("pixel"));
					Object->SetArrayField(TEXT("pixel"), {
						MakeShared<FJsonValueNumber>(Pixel[0]->AsNumber() * EncodedSize.X / Result.ImageSize.X),
						MakeShared<FJsonValueNumber>(Pixel[1]->AsNumber() * EncodedSize.Y / Result.ImageSize.Y)});
				}
			}
		}
		Out.Report = DeepLevelPCGAutomation::ToJson(Result.Report); Owner->Complete(MoveTemp(Out));
	});
	return Call;
}
