// Copyright <--\, Inc. All Rights Reserved.
#include "Automation/MCP/DeepLevelPCGToolImage.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "IImageWrapperModule.h"
#include "Misc/Base64.h"
#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/ToolsetImage.h"

namespace
{
	TAutoConsoleVariable<int32> MaxLongEdge(TEXT("DeepLevelPCG.MCP.ImageMaxLongEdge"), 1024,
		TEXT("Maximum returned MCP image edge in pixels (128..2048). Preserves aspect ratio; never enlarges images."));
	TAutoConsoleVariable<int32> JpegQuality(TEXT("DeepLevelPCG.MCP.ImageJPEGQuality"), 85,
		TEXT("Returned MCP JPEG quality (1..100)."));
}

bool DeepLevelPCGToolImage::Encode(const TArray<FColor>& Pixels, FIntPoint Size, FToolsetImage& Image, FIntPoint& EncodedSize)
{
	Image = FToolsetImage();
	EncodedSize = FIntPoint::ZeroValue;
	if (Size.X <= 0 || Size.Y <= 0 || int64(Size.X) * Size.Y != Pixels.Num()) { return false; }
	const int32 Limit = FMath::Clamp(MaxLongEdge.GetValueOnGameThread(), 128, 2048);
	const double Ratio = FMath::Min(1.0, double(Limit) / FMath::Max(Size.X, Size.Y));
	const FIntPoint Target(FMath::Max(1, FMath::RoundToInt(Size.X * Ratio)), FMath::Max(1, FMath::RoundToInt(Size.Y * Ratio)));
	TArray<FColor> Resized;
	const TArray<FColor>* Bitmap = &Pixels;
	if (Target != Size)
	{
		FImageUtils::ImageResize(Size.X, Size.Y, Pixels, Target.X, Target.Y, Resized, true);
		Bitmap = &Resized;
	}
	auto& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::JPEG);
	if (!Wrapper.IsValid() || !Wrapper->SetRaw(Bitmap->GetData(), int64(Bitmap->Num()) * sizeof(FColor), Target.X, Target.Y, ERGBFormat::BGRA, 8)) { return false; }
	const TArray64<uint8>& Compressed = Wrapper->GetCompressed(FMath::Clamp(JpegQuality.GetValueOnGameThread(), 1, 100));
	if (Compressed.IsEmpty()) { return false; }
	Image.MimeType = TEXT("image/jpeg");
	Image.Data = FBase64::Encode(Compressed.GetData(), Compressed.Num());
	EncodedSize = Target;
	return true;
}
