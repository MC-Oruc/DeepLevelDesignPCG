// Copyright <--\, Inc. All Rights Reserved.
#include "Automation/MCP/DeepLevelPCGToolset.h"
#include "Automation/DeepLevelPCGAutomation.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(DeepLevelPCGToolset)

FString UDeepLevelPCGToolset::Describe() { return DeepLevelPCGAutomation::Describe(); }
FDeepLevelPCGToolResult UDeepLevelPCGToolset::Execute(const FString& Request)
{
	auto Result = DeepLevelPCGAutomation::Execute(Request);
	FDeepLevelPCGToolResult Out;
	Out.Success = Result.bSuccess;
	if (!Result.Pixels.IsEmpty() && !Out.Image.SetFromBitmap(Result.Pixels, Result.ImageSize))
	{
		Result.Report->SetStringField(TEXT("captureError"), TEXT("PNG encoding failed; application status is unchanged."));
		Result.Report->SetBoolField(TEXT("captured"), false);
	}
	Out.Report = DeepLevelPCGAutomation::ToJson(Result.Report);
	return Out;
}
