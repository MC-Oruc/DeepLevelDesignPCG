// Copyright <--\, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Automation/MCP/DeepLevelPCGToolImage.h"
#include "HAL/IConsoleManager.h"
#include "IImageWrapperModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/Base64.h"
#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/ToolsetImage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelPCGToolImageTest, "DeepLevelDesignPCG.Editor.Automation.ToolImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelPCGToolImageTest::RunTest(const FString&)
{
	IConsoleVariable* Limit = IConsoleManager::Get().FindConsoleVariable(TEXT("DeepLevelPCG.MCP.ImageMaxLongEdge"));
	const int32 PreviousLimit = Limit->GetInt();
	Limit->Set(1024, ECVF_SetByCode);
	auto& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	for (const FIntPoint Size : {FIntPoint(1600, 900), FIntPoint(900, 1600), FIntPoint(320, 240)})
	{
		TArray<FColor> Pixels;
		Pixels.Init(FColor(128, 64, 32), Size.X * Size.Y);
		FToolsetImage Image;
		FIntPoint EncodedSize;
		TestTrue(TEXT("Encodes image"), DeepLevelPCGToolImage::Encode(Pixels, Size, Image, EncodedSize));
		TestEqual(TEXT("JPEG MIME"), Image.MimeType, FString(TEXT("image/jpeg")));
		const FIntPoint Expected = Size.X > 1024 ? FIntPoint(1024, 576) : Size.Y > 1024 ? FIntPoint(576, 1024) : Size;
		TestEqual(TEXT("Aspect ratio and no upscaling"), EncodedSize, Expected);
		TArray<uint8> Bytes;
		TestTrue(TEXT("Valid base64"), FBase64::Decode(Image.Data, Bytes));
		TestEqual(TEXT("JPEG bytes"), Module.DetectImageFormat(Bytes.GetData(), Bytes.Num()), EImageFormat::JPEG);
		const auto Decoder = Module.CreateImageWrapper(EImageFormat::JPEG);
		TestTrue(TEXT("JPEG decodes"), Decoder->SetCompressed(Bytes.GetData(), Bytes.Num()));
		TestEqual(TEXT("Decoded width"), int32(Decoder->GetWidth()), Expected.X);
		TestEqual(TEXT("Decoded height"), int32(Decoder->GetHeight()), Expected.Y);
	}
	FToolsetImage Image;
	FIntPoint EncodedSize;
	TestFalse(TEXT("Rejects mismatched pixels"), DeepLevelPCGToolImage::Encode({}, FIntPoint(128, 128), Image, EncodedSize));
	Limit->Set(PreviousLimit, ECVF_SetByCode);
	return true;
}
#endif
