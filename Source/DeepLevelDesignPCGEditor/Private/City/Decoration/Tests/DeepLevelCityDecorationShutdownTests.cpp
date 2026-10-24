// Copyright <--\, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "City/Decoration/DeepLevelCityBuildingDecorationEditor.h"
#include "City/Decoration/DeepLevelCityDecorationAuthoring.h"
#include "City/Decoration/Tests/DeepLevelCityDecorationPreviewTestBuilding.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationShutdownTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.Shutdown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationShutdownTest::RunTest(const FString&)
{
	if (!TestTrue(TEXT("Slate is initialized for the editor viewport"), FSlateApplication::IsInitialized())) { return false; }
	TStrongObjectPtr<UDeepLevelCityBuildingDecorationProfile> Profile(NewObject<UDeepLevelCityBuildingDecorationProfile>());
	Profile->BuildingClass = ADeepLevelCityDecorationPreviewTestBuilding::StaticClass();
	const FGuid Variant = DeepLevelCityDecorationAuthoring::AddVariant(*Profile);
	FDeepLevelCityBuildingDecorationEntry Entry;
	Entry.Name = TEXT("Shutdown fixture");
	Entry.Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	DeepLevelCityDecorationAuthoring::AddEntry(*Profile, Variant, Entry);

	TSharedPtr<SDeepLevelCityBuildingDecorationViewport> Preview = SNew(SDeepLevelCityBuildingDecorationViewport);
	Preview->Synchronize(Profile->BuildingClass, Profile.Get(), Variant);
	TSharedPtr<FDeepLevelCityBuildingDecorationViewportClient> Client = Preview->GetClient();
	UWorld* World = Preview->GetPreviewScene()->GetWorld();
	const TWeakPtr<FAdvancedPreviewScene> Scene = Preview->GetPreviewScene();
	TWeakObjectPtr<AActor> Building = Preview->GetBuildingActor();
	TWeakObjectPtr<AActor> Decoration = Preview->GetEntryActor(Profile->Variants[0].Entries[0].EntryGuid);
	if (!TestNotNull(TEXT("Preview world exists before shutdown"), World)
		|| !TestTrue(TEXT("Preview actors were spawned"), Building.IsValid() && Decoration.IsValid())) { return false; }
	TestTrue(TEXT("Early editor-exit callback is registered"), FEditorDelegates::OnEditorPreExit.IsBoundToObject(Preview.Get()));
	Preview->ShutdownPreview();
	Preview->ShutdownPreview();
	TestTrue(TEXT("Preview world remains alive during explicit release"), World->GetWorldSettings() != nullptr);
	TestFalse(TEXT("Building actor is released"), Building.IsValid());
	TestFalse(TEXT("Decoration actor is released"), Decoration.IsValid());
	TestFalse(TEXT("Editor-exit callback is unregistered"), FEditorDelegates::OnEditorPreExit.IsBoundToObject(Preview.Get()));
	TestFalse(TEXT("Retained client is no longer realtime"), Client->IsRealtime());
	TestNull(TEXT("Retained client is detached from its scene viewport"), Client->Viewport);
	Preview.Reset();
	TestFalse(TEXT("Widget destruction performs no deferred preview-actor cleanup"), Building.IsValid() || Decoration.IsValid());
	if (TestTrue(TEXT("Retained client keeps its scene owner alive after widget shutdown"), Scene.IsValid()))
	{
		TestTrue(TEXT("Retained client still references its original preview world"), Client->GetWorld() == World);
	}
	Client.Reset();
	TestFalse(TEXT("Last retained client releases the preview scene"), Scene.IsValid());
	return true;
}

#endif
