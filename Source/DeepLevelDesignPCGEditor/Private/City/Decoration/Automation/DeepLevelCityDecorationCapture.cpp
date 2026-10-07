// Copyright <--\, Inc. All Rights Reserved.
#include "City/Decoration/Automation/DeepLevelCityDecorationCapture.h"
#include "City/Decoration/DeepLevelCityBuildingDecorationEditor.h"
#include "Automation/Data/DeepLevelPCGAutomationResult.h"
#include "Misc/App.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SceneViewport.h"
#include "RenderingThread.h"
#include "SceneView.h"
#include "UnrealClient.h"

void DeepLevelCityDecorationCapture::Capture(const UDeepLevelCityBuildingDecorationProfile& Profile, const FGuid& Variant,
	const FDeepLevelCityDecorationCaptureOptions& Options, FDeepLevelPCGAutomationResult& Out)
{
	Out.Report->SetBoolField(TEXT("captured"), false);
	if (!FApp::CanEverRender() || !FSlateApplication::IsInitialized())
	{
		Out.Report->SetStringField(TEXT("captureError"), TEXT("Rendering/Slate unavailable; application status is unchanged."));
		return;
	}
	const auto Preview = SNew(SDeepLevelCityBuildingDecorationViewport);
	Preview->SetRenderDirectlyToWindow(false);
	Preview->Synchronize(Profile.BuildingClass, &Profile, Variant);
	const auto Viewport = Preview->GetSceneViewport();
	// This fresh preview has no Slate window; initialize its render-target size directly.
	Viewport->SetInitialSize(Options.Size);
	Preview->Focus(false);
	const auto Client = Preview->GetClient();
	Client->SetRealtime(false);
	Client->ViewFOV = Options.FOV;
	if (Options.Camera.IsSet())
	{
		Client->SetViewLocation(Options.Camera->GetLocation());
		Client->SetViewRotation(Options.Camera->Rotator());
	}
	Client->EngineShowFlags.SetModeWidgets(false);
	Client->EngineShowFlags.SetSelectionOutline(false);
	Client->EngineShowFlags.SetSelection(false);
	Client->EngineShowFlags.SetScreenPercentage(false);
	// Draw the newly synchronized scene, then read its completed framebuffer, never the user's viewport.
	Viewport->Draw();
	FlushRenderingCommands();
	if (!GetViewportScreenShot(Viewport.Get(), Out.Pixels) || Out.Pixels.Num() != Options.Size.X * Options.Size.Y)
	{
		Out.Pixels.Reset();
		Out.Report->SetStringField(TEXT("captureError"), TEXT("Decoration preview readback failed; application status is unchanged."));
		return;
	}
	for (auto& Pixel : Out.Pixels) { Pixel.A = 255; }
	Out.ImageSize = Options.Size;
	Out.Report->SetBoolField(TEXT("captured"), true);
	const auto VectorJson = [](const FVector& V)
	{
		return TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z)};
	};
	auto Camera = MakeShared<FJsonObject>();
	Camera->SetArrayField(TEXT("location"), VectorJson(Client->GetViewLocation()));
	const auto R = Client->GetViewRotation();
	Camera->SetArrayField(TEXT("rotation"), VectorJson(FVector(R.Pitch, R.Yaw, R.Roll)));
	Camera->SetNumberField(TEXT("fov"), Client->ViewFOV);
	Camera->SetNumberField(TEXT("width"), Options.Size.X);
	Camera->SetNumberField(TEXT("height"), Options.Size.Y);
	Out.Report->SetObjectField(TEXT("camera"), Camera);
	FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(Viewport.Get(), Client->GetScene(), Client->EngineShowFlags));
	const FSceneView* View = Client->CalcSceneView(&Family);
	TArray<TSharedPtr<FJsonValue>> Labels;
	const auto* Arrangement = Profile.Variants.FindByPredicate([&](const auto& V) { return V.VariantGuid == Variant; });
	if (View && Arrangement)
	{
		for (const auto& Entry : Arrangement->Entries)
		{
			FVector2D Pixel;
			if (!View->WorldToPixel(Entry.LocalTransform.GetLocation(), Pixel) || Pixel.X < 0 || Pixel.Y < 0 || Pixel.X >= Options.Size.X || Pixel.Y >= Options.Size.Y) { continue; }
			auto Label = MakeShared<FJsonObject>();
			Label->SetStringField(TEXT("id"), Entry.EntryGuid.ToString(EGuidFormats::DigitsWithHyphens));
			Label->SetStringField(TEXT("name"), Entry.Name.ToString());
			Label->SetArrayField(TEXT("pixel"), {MakeShared<FJsonValueNumber>(Pixel.X), MakeShared<FJsonValueNumber>(Pixel.Y)});
			Labels.Add(MakeShared<FJsonValueObject>(Label));
		}
	}
	Out.Report->SetArrayField(TEXT("labels"), Labels);
}
