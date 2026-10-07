// Copyright <--\, Inc. All Rights Reserved.
#include "Automation/DeepLevelPCGRequest.h"
#include "Automation/Data/DeepLevelPCGAutomationResult.h"
#include "EditorViewportClient.h"
#include "SEditorViewport.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "Slate/SceneViewport.h"
#include "Misc/PackageName.h"
#include "Misc/App.h"
#include "UObject/SavePackage.h"
#include "RenderingThread.h"
#include "UnrealClient.h"
#include "GameFramework/Actor.h"
#include "PCGComponent.h"
#include "PCGManagedResource.h"

namespace DeepLevelPCGRequest
{
	TSharedPtr<FJsonObject> Object(const FJsonValue& Value) { return Value.Type == EJson::Object ? Value.AsObject() : nullptr; }
	bool Fields(const FJsonObject& Object, std::initializer_list<const TCHAR*> Allowed, FString& Error)
	{
		for (const auto& Pair : Object.Values)
		{
			bool bAllowed = false;
			for (const auto* Name : Allowed) { bAllowed |= Pair.Key == Name; }
			if (!bAllowed) { Error = FString::Printf(TEXT("Unknown field: %s"), *Pair.Key); return false; }
		}
		return true;
	}
	bool String(const FJsonObject& Object, const TCHAR* Key, FString& Out, FString& Error, bool Required)
	{
		if (!Object.HasField(Key) && !Required) { return true; }
		if (!Object.TryGetStringField(Key, Out) || (Required && Out.IsEmpty())) { Error = FString(Key) + TEXT(" must be a nonempty string."); return false; }
		return true;
	}
	bool Number(const FJsonObject& Object, const TCHAR* Key, double& Out, double Min, double Max, FString& Error)
	{
		if (!Object.HasField(Key)) { return true; }
		if (!Object.TryGetNumberField(Key, Out) || !FMath::IsFinite(Out) || Out < Min || Out > Max)
		{ Error = FString(Key) + TEXT(" is outside its numeric range."); return false; }
		return true;
	}
	bool Integer(const FJsonObject& Object, const TCHAR* Key, int32& Out, int32 Min, int32 Max, FString& Error)
	{
		double Value = Out;
		if (!Number(Object, Key, Value, Min, Max, Error)) { return false; }
		if (FMath::FloorToDouble(Value) != Value) { Error = FString(Key) + TEXT(" must be an integer."); return false; }
		Out = static_cast<int32>(Value);
		return true;
	}
	bool Bool(const FJsonObject& Object, const TCHAR* Key, bool& Out, FString& Error)
	{
		if (!Object.HasField(Key)) { return true; }
		if (!Object.TryGetBoolField(Key, Out)) { Error = FString(Key) + TEXT(" must be boolean."); return false; }
		return true;
	}
	bool Vector(const FJsonObject& Object, const TCHAR* Key, FVector& Out, FString& Error)
	{
		if (!Object.HasField(Key)) { return true; }
		const TArray<TSharedPtr<FJsonValue>>* Values;
		if (!Object.TryGetArrayField(Key, Values) || Values->Num() != 3) { Error = FString(Key) + TEXT(" requires three numbers."); return false; }
		for (int32 Index = 0; Index < 3; ++Index)
		{
			double Value;
			if (!(*Values)[Index]->TryGetNumber(Value) || !FMath::IsFinite(Value) || FMath::Abs(Value) > 1e9)
			{ Error = FString(Key) + TEXT(" contains an invalid coordinate."); return false; }
			Out[Index] = Value;
		}
		return true;
	}
	bool Points(const FJsonObject& Object, TArray<FVector>& Out, FString& Error)
	{
		const TArray<TSharedPtr<FJsonValue>>* Points;
		if (!Object.TryGetArrayField(TEXT("points"), Points) || Points->Num() < 2 || Points->Num() > 4096)
		{ Error = TEXT("points requires 2..4096 world-space positions."); return false; }
		Out.Reset();
		for (const auto& Point : *Points)
		{
			if (Point->Type != EJson::Array) { Error = TEXT("Each point must be an array."); return false; }
			FJsonObject Wrapper; Wrapper.SetField(TEXT("point"), Point);
			FVector Value;
			if (!Vector(Wrapper, TEXT("point"), Value, Error)) { return false; }
			Out.Add(Value);
		}
		return true;
	}
	TArray<TSharedPtr<FJsonValue>> VectorJson(const FVector& V)
	{
		return {MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z)};
	}
	bool Save(UObject& Object, FString& Error)
	{
		UPackage* Package = Object.GetPackage();
		FString Filename;
		if (Package->HasAnyFlags(RF_Transient) || !FPackageName::TryConvertLongPackageNameToFilename(
			Package->GetName(), Filename, Package->ContainsMap() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension()))
		{ Error = TEXT("Target has no writable persistent package."); return false; }
		FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, nullptr, *Filename, Args)) { Error = TEXT("Target package save failed."); return false; }
		return true;
	}
	bool FOptions::Read(const FJsonObject& Root, FString& Error)
	{
		if (!Bool(Root, TEXT("dryRun"), bDryRun, Error) || !Bool(Root, TEXT("save"), bSave, Error)
			|| !Bool(Root, TEXT("capture"), bCapture, Error) || !Bool(Root, TEXT("includeState"), bIncludeState, Error)
			|| !Number(Root, TEXT("timeoutSeconds"), TimeoutSeconds, 1, 300, Error)) { return false; }
		if (bDryRun && bSave) { Error = TEXT("dryRun cannot save."); return false; }
		if (!bCapture && (Root.HasField(TEXT("camera")) || Root.HasField(TEXT("captureSize"))))
		{ Error = TEXT("Camera and captureSize require capture=true."); return false; }
		if (Root.HasField(TEXT("captureSize")))
		{
			const TArray<TSharedPtr<FJsonValue>>* Values;
			if (!Root.TryGetArrayField(TEXT("captureSize"), Values) || Values->Num() != 2)
			{ Error = TEXT("captureSize requires two integer sides."); return false; }
			for (int32 Index = 0; Index < 2; ++Index)
			{
				double Side;
				if (!(*Values)[Index]->TryGetNumber(Side) || !FMath::IsFinite(Side) || Side < 128 || Side > 2048 || FMath::FloorToDouble(Side) != Side)
				{ Error = TEXT("Image sides must be integers in 128..2048."); return false; }
				Size[Index] = static_cast<int32>(Side);
			}
		}
		if (Root.HasField(TEXT("camera")))
		{
			const TSharedPtr<FJsonObject>* CameraObject;
			if (!Root.TryGetObjectField(TEXT("camera"), CameraObject)) { Error = TEXT("camera must be an object."); return false; }
			const auto& C = **CameraObject;
			FVector Location, Rotation; double FieldOfView = FOV;
			if (!Fields(C, {TEXT("location"), TEXT("rotation"), TEXT("fov")}, Error)
				|| !C.HasField(TEXT("location")) || !C.HasField(TEXT("rotation")))
			{ if (Error.IsEmpty()) { Error = TEXT("Camera requires location and rotation."); } return false; }
			if (!Vector(C, TEXT("location"), Location, Error) || !Vector(C, TEXT("rotation"), Rotation, Error)
				|| !Number(C, TEXT("fov"), FieldOfView, 10, 150, Error)) { return false; }
			Camera = FTransform(FRotator(Rotation.X, Rotation.Y, Rotation.Z), Location); FOV = FieldOfView;
		}
		return true;
	}
	static void ReadViewport(FSceneViewport& Viewport, FEditorViewportClient& Client, const FOptions& Options, FDeepLevelPCGAutomationResult& Out)
	{
		Out.Report->SetBoolField(TEXT("captured"), false);
		Viewport.SetInitialSize(Options.Size);
		Client.SetRealtime(false); Client.ViewFOV = Options.FOV;
		if (Options.Camera.IsSet()) { Client.SetViewLocation(Options.Camera->GetLocation()); Client.SetViewRotation(Options.Camera->Rotator()); }
		Client.EngineShowFlags.SetModeWidgets(false); Client.EngineShowFlags.SetSelectionOutline(false);
		Client.EngineShowFlags.SetSelection(false); Client.EngineShowFlags.SetScreenPercentage(false);
		Viewport.Draw(); FlushRenderingCommands();
		if (!GetViewportScreenShot(&Viewport, Out.Pixels) || Out.Pixels.Num() != Options.Size.X * Options.Size.Y)
		{ Out.Pixels.Reset(); Out.Report->SetStringField(TEXT("captureError"), TEXT("Fresh viewport readback failed.")); return; }
		for (auto& Pixel : Out.Pixels) { Pixel.A = 255; }
		Out.ImageSize = Options.Size; Out.Report->SetBoolField(TEXT("captured"), true);
		auto Camera = MakeShared<FJsonObject>();
		Camera->SetArrayField(TEXT("location"), VectorJson(Client.GetViewLocation()));
		const auto Rotation = Client.GetViewRotation();
		Camera->SetArrayField(TEXT("rotation"), VectorJson(FVector(Rotation.Pitch, Rotation.Yaw, Rotation.Roll)));
		Camera->SetNumberField(TEXT("fov"), Client.ViewFOV); Camera->SetNumberField(TEXT("width"), Options.Size.X); Camera->SetNumberField(TEXT("height"), Options.Size.Y);
		Out.Report->SetObjectField(TEXT("camera"), Camera);
	}
	static bool CanCapture(FDeepLevelPCGAutomationResult& Out)
	{
		if (FApp::CanEverRender() && FSlateApplication::IsInitialized()) { return true; }
		Out.Report->SetBoolField(TEXT("captured"), false);
		Out.Report->SetStringField(TEXT("captureError"), TEXT("Rendering/Slate unavailable.")); return false;
	}
	void CapturePreview(const TSharedRef<SEditorViewport>& Preview, const FOptions& Options, FDeepLevelPCGAutomationResult& Out)
	{
		if (!CanCapture(Out)) { return; }
		Preview->SetRenderDirectlyToWindow(false);
		ReadViewport(*Preview->GetSceneViewport(), *Preview->GetViewportClient(), Options, Out);
	}
	void CaptureWorld(AActor& Target, const UPCGComponent& Component, const FOptions& Options, FDeepLevelPCGAutomationResult& Out)
	{
		if (!CanCapture(Out)) { return; }
		class FWorldCaptureClient final : public FEditorViewportClient
		{
		public:
			explicit FWorldCaptureClient(UWorld* InWorld) : FEditorViewportClient(nullptr), World(InWorld) {}
			virtual UWorld* GetWorld() const override { return World; }
		private:
			UWorld* World;
		};
		FWorldCaptureClient Client(Target.GetWorld());
		Client.SetViewMode(VMI_Lit);
		const auto Widget = SNew(SViewport).RenderDirectlyToWindow(false);
		TSharedPtr<FSceneViewport> Viewport = MakeShared<FSceneViewport>(&Client, Widget); Widget->SetViewportInterface(Viewport.ToSharedRef());
		Client.Viewport = Viewport.Get();
		FBox Bounds = Target.GetComponentsBoundingBox(true);
		Component.ForEachConstManagedResource([&Bounds](const UPCGManagedResource* Resource)
		{
			if (const auto* Actors = Cast<UPCGManagedActors>(Resource))
			{
				for (const auto& Reference : Actors->GetConstGeneratedActors())
				{
					if (const auto* Actor = Reference.Get()) { Bounds += Actor->GetComponentsBoundingBox(true); }
				}
			}
		});
		const FVector Center = Bounds.IsValid ? Bounds.GetCenter() : Target.GetActorLocation();
		const double Distance = Bounds.IsValid ? FMath::Max(Bounds.GetExtent().Size() * 2.5, 1000.0) : 3000.0;
		const FVector Direction = FVector(-1, -1, 0.7).GetSafeNormal();
		Client.SetViewLocation(Center + Direction * Distance); Client.SetViewRotation((-Direction).Rotation());
		ReadViewport(*Viewport, Client, Options, Out);
		Client.Viewport = nullptr;
	}
}
