// Copyright <--\, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "City/Decoration/DeepLevelCityDecorationAuthoring.h"
#include "City/Decoration/DeepLevelCityBuildingDecorationEditor.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetData.h"
#include "City/Decoration/Tests/DeepLevelCityDecorationPreviewTestBuilding.h"
#include "City/Decoration/DeepLevelCityDecorationValidation.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/DecalActor.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Slate/SceneViewport.h"
#include "EngineUtils.h"
#include "SceneView.h"
#include "SceneManagement.h"
#include "RenderingThread.h"
#include "UnrealWidget.h"
#include "EditorViewportCommands.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "IDetailsView.h"
#include "Engine/Blueprint.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

namespace Authoring = DeepLevelCityDecorationAuthoring;

namespace
{
class FDecorationGizmoDrawCollector final : public FSimpleElementCollector
{
public:
	int32 AxisMeshes = 0;
	TRefCountPtr<HHitProxy> CurrentProxy;
	TArray<FDynamicPrimitiveResource*> Resources;
	virtual void RegisterDynamicResource(FDynamicPrimitiveResource* Resource) override
	{
		Resources.Add(Resource);
		ENQUEUE_RENDER_COMMAND(InitDecorationTestResource)([Resource](FRHICommandListImmediate& RHICmdList)
		{
			Resource->InitPrimitiveResource(RHICmdList);
		});
	}
	virtual ~FDecorationGizmoDrawCollector() override
	{
		ENQUEUE_RENDER_COMMAND(ReleaseDecorationTestResources)([Pending = MoveTemp(Resources)](FRHICommandListImmediate&)
		{
			for (FDynamicPrimitiveResource* Resource : Pending) { Resource->ReleasePrimitiveResource(); }
		});
	}
	virtual void SetHitProxy(HHitProxy* Proxy) override { CurrentProxy = Proxy; }
	virtual bool IsHitTesting() override { return true; }
	virtual int32 DrawMesh(const FMeshBatch&) override
	{
		if (CurrentProxy && CurrentProxy->IsA(HWidgetAxis::StaticGetType())) { ++AxisMeshes; }
		return 1;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationEditorIdentityTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.Identity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationEditorIdentityTest::RunTest(const FString&)
{
	TStrongObjectPtr<UDeepLevelCityBuildingDecorationProfile> Profile(NewObject<UDeepLevelCityBuildingDecorationProfile>());
	const FGuid VariantId = Authoring::AddVariant(*Profile);
	FDeepLevelCityBuildingDecorationEntry Definition;
	Definition.Name = TEXT("Fixture");
	Definition.LocalTransform = FTransform(FRotator(10, 30, 0), FVector(30, 40, 200), FVector(2));
	const FGuid EntryId = Authoring::AddEntry(*Profile, VariantId, Definition);
	const FGuid SecondEntryId = Authoring::DuplicateEntry(*Profile, VariantId, EntryId);
	TestTrue(TEXT("Entry duplication gets a separate identity"), SecondEntryId.IsValid() && SecondEntryId != EntryId);
	const FGuid CopyId = Authoring::DuplicateVariant(*Profile, VariantId);
	const auto* Original = Authoring::FindVariant(*Profile, VariantId);
	const auto* Copy = Authoring::FindVariant(*Profile, CopyId);
	if (!TestNotNull(TEXT("Copied arrangement exists"), Copy)) { return false; }
	TestTrue(TEXT("Original entry survives array growth"), Original && Original->Entries[0].EntryGuid == EntryId);
	TestEqual(TEXT("Variant copies all entries"), Copy->Entries.Num(), 2);
	TestTrue(TEXT("Variant copy preserves transforms"), Copy->Entries[0].LocalTransform.Equals(Definition.LocalTransform));
	TSet<FGuid> EntryIds;
	for (const auto& Variant : Profile->Variants)
	{
		for (const auto& Entry : Variant.Entries) { TestFalse(TEXT("Copied arrangements never share entry identity"), EntryIds.Contains(Entry.EntryGuid)); EntryIds.Add(Entry.EntryGuid); }
	}
	TestTrue(TEXT("Removing one decoration succeeds"), Authoring::RemoveEntry(*Profile, VariantId, EntryId));
	TestNotNull(TEXT("Copy remains after removing original entry"), Authoring::FindEntry(*Profile, CopyId, Copy->Entries[0].EntryGuid));
	TestFalse(TEXT("Invalid variant cannot receive entries"), Authoring::AddEntry(*Profile, FGuid(), Definition).IsValid());
	TestTrue(TEXT("Variant removal succeeds"), Authoring::RemoveVariant(*Profile, VariantId));
	TestNotNull(TEXT("Removing original preserves copied variant"), Authoring::FindVariant(*Profile, CopyId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationEditorAssetsTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.AssetsAndTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationEditorAssetsTest::RunTest(const FString&)
{
	FDeepLevelCityBuildingDecorationEntry Entry;
	FText Error;
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	TestTrue(TEXT("Static meshes map to mesh outputs"), Authoring::MakeEntryFromAsset(Mesh, Entry, Error));
	TestTrue(TEXT("Mesh reference is retained"), Entry.Mesh.Get() == Mesh && Entry.Output == EDeepLevelCityDecorationOutput::Mesh);
	TStrongObjectPtr<UBlueprint> Blueprint(NewObject<UBlueprint>());
	Blueprint->GeneratedClass = APointLight::StaticClass();
	TestTrue(TEXT("Light actor Blueprint maps to actor output"), Authoring::MakeEntryFromAsset(Blueprint.Get(), Entry, Error));
	TestTrue(TEXT("Actor class is retained"), Entry.ActorClass.Get() == APointLight::StaticClass() && Entry.Output == EDeepLevelCityDecorationOutput::Actor);
	TestTrue(TEXT("Deferred decals are accepted"), Authoring::MakeEntryFromAsset(UMaterial::GetDefaultMaterial(MD_DeferredDecal), Entry, Error));
	TestTrue(TEXT("Decal output is selected"), Entry.Output == EDeepLevelCityDecorationOutput::Decal);
	TestFalse(TEXT("Surface materials cannot be mistaken for decals"), Authoring::MakeEntryFromAsset(UMaterial::GetDefaultMaterial(MD_Surface), Entry, Error));
	TestFalse(TEXT("Unsupported assets are explicitly rejected"), Authoring::MakeEntryFromAsset(Blueprint->GeneratedClass, Entry, Error));
	const FTransform Original(FRotator(0, 45, 0), FVector(100, 200, 300), FVector(1, -1, 2));
	const FTransform Changed = Authoring::ApplyTransformDelta(Original, FVector(10, 20, 30), FRotator(0, 45, 0), FVector(-1, 1, -2));
	TestEqual(TEXT("Translation remains in building-local space"), Changed.GetLocation(), FVector(110, 220, 330));
	TestTrue(TEXT("Rotation composes with the existing orientation"), Changed.GetRotation().Equals(FRotator(0, 90, 0).Quaternion()));
	TestTrue(TEXT("Gizmo cannot collapse any scale axis to zero"), Changed.GetScale3D().GetAbs().GetMin() >= 0.01);
	TestTrue(TEXT("Mirrored scale retains its sign at the minimum"), Changed.GetScale3D().Y < 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationEditorUndoTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.UndoRedo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationEditorUndoTest::RunTest(const FString&)
{
	if (!TestNotNull(TEXT("Editor transaction system exists"), GEditor)) { return false; }
	TStrongObjectPtr<UDeepLevelCityBuildingDecorationProfile> Profile(NewObject<UDeepLevelCityBuildingDecorationProfile>());
	TStrongObjectPtr<UDeepLevelBuildingPlacementCatalog> Catalog(NewObject<UDeepLevelBuildingPlacementCatalog>());
	TStrongObjectPtr<UDeepLevelCityDecorationSet> Set(NewObject<UDeepLevelCityDecorationSet>());
	Profile->BuildingClass = APackedLevelActor::StaticClass();
	Catalog->Buildings.Emplace_GetRef().BuildingClass = Profile->BuildingClass;
	Set->BuildingCatalog = Catalog.Get();
	Set->BuildingProfiles.Add(Profile.Get());
	FDeepLevelCityDecorationDocument Document;
	Document.Open(Set.Get());
	Document.AddVariant();
	FDeepLevelCityBuildingDecorationEntry Definition;
	Definition.Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Document.AddEntries(MakeArrayView(&Definition, 1));
	const FGuid Entry = Document.GetEntryId();
	const FGuid Variant = Document.GetVariantId();
	FText Error;
	TestTrue(TEXT("Document applies a transform through its transaction path"), Document.SetTransform(FTransform(FVector(10, 20, 30)), Error));
	Document.Undo();
	TestEqual(TEXT("Document undo restores the selected entry"), Document.GetEntry()->LocalTransform.GetLocation(), FVector::ZeroVector);
	Document.Redo();
	TestEqual(TEXT("Document redo restores the transform"), Document.GetEntry()->LocalTransform.GetLocation(), FVector(10, 20, 30));
	TestEqual(TEXT("Undo/redo preserves selected entry identity"), Document.GetEntryId(), Entry);
	TestEqual(TEXT("Undo/redo preserves selected variant identity"), Document.GetVariantId(), Variant);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationEditorRegistrationTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.AssetActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationEditorRegistrationTest::RunTest(const FString&)
{
	const auto Actions = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get()
		.GetAssetTypeActionsForClass(UDeepLevelCityBuildingDecorationProfile::StaticClass()).Pin();
	const auto DataAssetActions = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get()
		.GetAssetTypeActionsForClass(UDataAsset::StaticClass()).Pin();
	TestTrue(TEXT("Profiles inherit the standard Data Asset editor action"),
		Actions.IsValid() && Actions == DataAssetActions);
	const auto SetActions = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get()
		.GetAssetTypeActionsForClass(UDeepLevelCityDecorationSet::StaticClass()).Pin();
	TestTrue(TEXT("City Decoration Set owns the catalog editor entry point"),
		SetActions.IsValid() && SetActions->GetSupportedClass() == UDeepLevelCityDecorationSet::StaticClass());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationViewportConstructionTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.ViewportConstruction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationViewportConstructionTest::RunTest(const FString&)
{
	if (!TestTrue(TEXT("Slate is initialized for the editor viewport"), FSlateApplication::IsInitialized())) { return false; }
	const TSharedRef<SDeepLevelCityBuildingDecorationViewport> Preview = SNew(SDeepLevelCityBuildingDecorationViewport);
	const auto Client = Preview->GetClient();
	if (!TestTrue(TEXT("Viewport client was constructed"), Client.IsValid())) { return false; }
	TestNotNull(TEXT("Scene viewport exists before gizmo initialization"), Client->Viewport);
	TestTrue(TEXT("Empty selection has no transform gizmo"), Client->GetWidgetMode() == UE::Widget::WM_None);
	TestTrue(TEXT("Realtime preview remains enabled"), Client->IsRealtime());
	FDeepLevelBuildingPlacementDefinition Definition;
	Definition.PlacementVolume.Center.Z = 450.0;
	Definition.PlacementVolume.Extent.Z = 300.0;
	Preview->SetPlacementGuide(&Definition, false);
	TestEqual(TEXT("Preview ground follows the calibrated building base even without bounds"), Preview->GetPreviewScene()->GetFloorOffset(), -150.0f);
	Definition.PlacementVolume.Center.Z = -100.0;
	Preview->SetPlacementGuide(&Definition, true);
	TestEqual(TEXT("Preview ground supports a negative actor-local base"), Preview->GetPreviewScene()->GetFloorOffset(), 400.0f);
	Preview->SetPlacementGuide(nullptr, false);
	TestEqual(TEXT("Clearing calibration resets preview ground"), Preview->GetPreviewScene()->GetFloorOffset(), 0.0f);
	return true;
}

namespace
{
	struct FDecorationFixture
	{
		TStrongObjectPtr<UDeepLevelCityDecorationSet> Set{NewObject<UDeepLevelCityDecorationSet>()};
		TStrongObjectPtr<UDeepLevelBuildingPlacementCatalog> Catalog{NewObject<UDeepLevelBuildingPlacementCatalog>()};
		TStrongObjectPtr<UDeepLevelCityBuildingDecorationProfile> Profile{NewObject<UDeepLevelCityBuildingDecorationProfile>()};
		FDeepLevelCityDecorationDocument Document;
		FDecorationFixture()
		{
			Profile->BuildingClass = ADeepLevelCityDecorationPreviewTestBuilding::StaticClass();
			Catalog->Buildings.Emplace_GetRef().BuildingClass = Profile->BuildingClass;
			Catalog->Buildings.Emplace_GetRef().BuildingClass = APackedLevelActor::StaticClass();
			Set->BuildingCatalog = Catalog.Get();
			Set->BuildingProfiles.Add(Profile.Get());
			const FGuid Variant = Authoring::AddVariant(*Profile);
			FDeepLevelCityBuildingDecorationEntry Entry;
			Entry.Name = TEXT("Cube");
			Entry.Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			Authoring::AddEntry(*Profile, Variant, Entry);
			Document.Open(Set.Get());
			Document.SelectVariant(Variant);
			Document.SelectEntry(Profile->Variants[0].Entries[0].EntryGuid);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationDecalPreviewTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.DecalPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationDecalPreviewTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	auto& Entry = Fixture.Profile->Variants[0].Entries[0];
	Entry.Output = EDeepLevelCityDecorationOutput::Decal;
	Entry.Mesh.Reset();
	Entry.DecalMaterial = UMaterial::GetDefaultMaterial(MD_DeferredDecal);
	Entry.DecalSize = FVector(4, 22, 160);
	Entry.LocalTransform = FTransform(FRotator(0, -90, 0), FVector(1290, 851, 380), FVector(1.5, 1, 2));
	const auto Preview = SNew(SDeepLevelCityBuildingDecorationViewport);
	Preview->Synchronize(Fixture.Profile->BuildingClass, Fixture.Profile.Get(), Fixture.Document.GetVariantId());
	auto* Actor = Cast<ADecalActor>(Preview->GetEntryActor(Entry.EntryGuid));
	if (!TestNotNull(TEXT("Decal preview exists"), Actor)) { return false; }
	TestTrue(TEXT("Initial projector pose matches the profile without the actor default rotation"),
		Actor->GetDecal()->GetComponentTransform().Equals(Entry.LocalTransform));
	const FBox InitialBounds = Actor->GetDecal()->Bounds.GetBox();
	Entry.DecalSize = FVector(8, 40, 80);
	Preview->Synchronize(Fixture.Profile->BuildingClass, Fixture.Profile.Get(), Fixture.Document.GetVariantId());
	TestTrue(TEXT("Resize retains the preview actor"), Preview->GetEntryActor(Entry.EntryGuid) == Actor);
	TestEqual(TEXT("Resize updates projector dimensions"), Actor->GetDecal()->DecalSize, Entry.DecalSize);
	TestFalse(TEXT("Resize refreshes culling bounds without moving the projector"),
		Actor->GetDecal()->Bounds.GetBox().Equals(InitialBounds));
	TestTrue(TEXT("Resize preserves the authored pose"), Actor->GetActorTransform().Equals(Entry.LocalTransform));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationCatalogOwnershipTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.CatalogOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationCatalogOwnershipTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	const auto OriginalClass = Fixture.Profile->BuildingClass;
	const FTransform OriginalTransform = Fixture.Profile->Variants[0].Entries[0].LocalTransform;
	TestTrue(TEXT("Mapped catalog profile is editable"), Fixture.Document.CanEdit());
	TestFalse(TEXT("Out-of-catalog actor classes cannot be selected"), Fixture.Document.SelectBuilding(APointLight::StaticClass()));
	TestTrue(TEXT("Undecorated catalog buildings are selectable"), Fixture.Document.SelectBuilding(APackedLevelActor::StaticClass()));
	TestNull(TEXT("A different building does not inherit previous decorations"), Fixture.Document.GetProfile());
	TestEqual(TEXT("Building selection never retargets the original profile"), Fixture.Profile->BuildingClass, OriginalClass);
	TestTrue(TEXT("Previous local layout remains intact"), Fixture.Profile->Variants[0].Entries[0].LocalTransform.Equals(OriginalTransform));
	TestFalse(TEXT("An incompatible existing profile cannot be attached"), Fixture.Document.AttachProfile(Fixture.Profile.Get()));
	TStrongObjectPtr<UDeepLevelCityBuildingDecorationProfile> Created(NewObject<UDeepLevelCityBuildingDecorationProfile>());
	TestTrue(TEXT("Explicit creation initializes a new profile for the selected building"), Fixture.Document.AttachProfile(Created.Get(), true));
	TestTrue(TEXT("Created profile is directly linked to the set"), Fixture.Set->BuildingProfiles.Contains(Created.Get()));
	TestFalse(TEXT("A second profile cannot be attached to the same building"), Fixture.Document.AttachProfile(Created.Get()));
	Fixture.Document.Undo();
	TestNull(TEXT("Undo creation withdraws the set assignment"), Fixture.Document.GetProfile());
	Fixture.Document.Redo();
	TestTrue(TEXT("Redo creation restores the exact assignment"), Fixture.Document.GetProfile() == Created.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationPatchTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.FieldPatches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationPatchTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	const auto Baseline = *Fixture.Document.GetEntry();
	FText Error;
	Fixture.Document.SetTransform(FTransform(FVector(100, 200, 300)), Error);
	auto Desired = Baseline;
	Desired.Name = TEXT("Renamed");
	EDeepLevelCityDecorationChange LastChange = EDeepLevelCityDecorationChange::Context;
	Fixture.Document.OnChanged.AddLambda([&](EDeepLevelCityDecorationChange Change, FGuid) { LastChange = Change; });
	TestTrue(TEXT("Inspector patch succeeds"), Fixture.Document.SetEntryFields(Desired, Baseline, Error));
	TestEqual(TEXT("Name editing does not overwrite a newer transform"), Fixture.Document.GetEntry()->LocalTransform.GetLocation(), FVector(100, 200, 300));
	TestTrue(TEXT("Name-only edit publishes metadata, not output rebuilding"), LastChange == EDeepLevelCityDecorationChange::Metadata);
	const auto Current = *Fixture.Document.GetEntry();
	Desired = Current;
	Desired.Output = EDeepLevelCityDecorationOutput::Actor;
	Desired.ActorClass = APointLight::StaticClass();
	TestTrue(TEXT("Output switch is an explicit document mutation"), Fixture.Document.SetEntryFields(Desired, Current, Error));
	TestTrue(TEXT("Inactive mesh dependency is cleared"), Fixture.Document.GetEntry()->Mesh.IsNull());
	Fixture.Document.Undo();
	TestTrue(TEXT("Undo restores output and dependency together"), Fixture.Document.GetEntry()->Output == EDeepLevelCityDecorationOutput::Mesh && !Fixture.Document.GetEntry()->Mesh.IsNull());
	FTransform Invalid = Fixture.Document.GetEntry()->LocalTransform;
	Invalid.SetScale3D(FVector::ZeroVector);
	TestFalse(TEXT("Typed transforms follow the shared nonzero scale contract"), Fixture.Document.SetTransform(Invalid, Error));
	const auto BeforeFacade = *Fixture.Document.GetEntry();
	Desired = BeforeFacade;
	Desired.bAutoFacade = false;
	Desired.Facades = {EDeepLevelBuildingVolumeFace::PositiveX, EDeepLevelBuildingVolumeFace::NegativeY};
	TestTrue(TEXT("Facade inspector edit uses the document mutation"), Fixture.Document.SetEntryFields(Desired, BeforeFacade, Error));
	TestFalse(TEXT("Facade edit disables automatic assignment"), Fixture.Document.GetEntry()->bAutoFacade);
	TestEqual(TEXT("Facade edit preserves both faces"), Fixture.Document.GetEntry()->Facades.Num(), 2);
	TestTrue(TEXT("Facade edit publishes metadata"), LastChange == EDeepLevelCityDecorationChange::Metadata);
	Fixture.Document.Undo();
	TestTrue(TEXT("Undo restores automatic assignment"), Fixture.Document.GetEntry()->bAutoFacade);
	Fixture.Document.Redo();
	TestTrue(TEXT("Redo restores the exact facade assignment"), Fixture.Document.GetEntry()->Facades == Desired.Facades && !Fixture.Document.GetEntry()->bAutoFacade);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationDragLifecycleTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.DragLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationDragLifecycleTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	int32 Notifications = 0;
	Fixture.Document.OnChanged.AddLambda([&](EDeepLevelCityDecorationChange, FGuid) { ++Notifications; });
	const bool bDirty = Fixture.Profile->GetOutermost()->IsDirty();
	Fixture.Document.ApplyDragTransform(Fixture.Document.GetEntry()->LocalTransform);
	Fixture.Document.EndDrag();
	TestEqual(TEXT("No-op transform emits no mutation or preview notification"), Notifications, 0);
	TestEqual(TEXT("No-op transform preserves package dirty state"), Fixture.Profile->GetOutermost()->IsDirty(), bDirty);
	Fixture.Document.ApplyDragTransform(FTransform(FVector(40, 0, 0)));
	Fixture.Document.ApplyDragTransform(FTransform(FVector(100, 0, 0)));
	Fixture.Document.EndDrag();
	TestEqual(TEXT("One drag commits the final resolved transform"), Fixture.Document.GetEntry()->LocalTransform.GetLocation().X, 100.0);
	Fixture.Document.Undo();
	TestEqual(TEXT("One drag is one undo operation"), Fixture.Document.GetEntry()->LocalTransform.GetLocation().X, 0.0);
	Fixture.Document.ApplyDragTransform(FTransform(FVector(10, 0, 0)));
	const int32 BeforeClose = Notifications;
	Fixture.Document.Shutdown();
	TestEqual(TEXT("Closing an active drag cannot notify or rebuild the preview"), Notifications, BeforeClose);
	TestFalse(TEXT("Closed document rejects further edits"), Fixture.Document.CanEdit());
	TestFalse(TEXT("Closed document cannot change set assignments"), Fixture.Document.RemoveAssignment(Fixture.Set.Get(), 0, Fixture.Profile.Get()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationMirroredRenderingTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.MirroredRendering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationMirroredRenderingTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	auto& Entry = Fixture.Profile->Variants[0].Entries[0];
	Entry.LocalTransform = FTransform(FRotator::ZeroRotator, FVector(2000, 0, 200), FVector(1.5, 1, 1.2));
	const auto Preview = SNew(SDeepLevelCityBuildingDecorationViewport);
	Preview->SetRenderDirectlyToWindow(false);
	Preview->Synchronize(Fixture.Profile->BuildingClass, Fixture.Profile.Get(), Fixture.Document.GetVariantId());
	const auto Viewport = Preview->GetSceneViewport();
	Viewport->SetInitialSize(FIntPoint(256, 256));
	const auto Client = Preview->GetClient();
	Client->SetRealtime(false);
	Client->SetViewMode(VMI_Unlit);
	Client->SetViewLocation(FVector(2000, -350, 200));
	Client->SetViewRotation(FRotator(0, 90, 0));
	Client->ViewFOV = 45;
	Client->EngineShowFlags.SetAntiAliasing(false);
	Client->EngineShowFlags.SetScreenPercentage(false);
	const auto Capture = [&]()
	{
		Viewport->Draw();
		FlushRenderingCommands();
		TArray<FColor> Pixels;
		GetViewportScreenShot(Viewport.Get(), Pixels);
		return Pixels;
	};
	const auto Positive = Capture();
	if (!TestEqual(TEXT("Rendered reference has a complete framebuffer"), Positive.Num(), 256 * 256)) { return false; }
	FTransform Mirrored = Entry.LocalTransform;
	Mirrored.SetScale3D(FVector(1.5, -1, 1.2));
	Entry.LocalTransform = Mirrored;
	Preview->Synchronize(Fixture.Profile->BuildingClass, Fixture.Profile.Get(), Fixture.Document.GetVariantId());
	const auto ViaInspector = Capture();
	const auto SameImage = [](const TArray<FColor>& A, const TArray<FColor>& B)
	{
		if (A.Num() != B.Num()) { return false; }
		int32 Different = 0;
		for (int32 I = 0; I < A.Num(); ++I)
		{
			if (FMath::Abs(int32(A[I].R) - int32(B[I].R)) > 5
				|| FMath::Abs(int32(A[I].G) - int32(B[I].G)) > 5
				|| FMath::Abs(int32(A[I].B) - int32(B[I].B)) > 5) { ++Different; }
		}
		return Different < A.Num() / 100;
	};
	TestTrue(TEXT("Mirroring the symmetric cube keeps its outward surfaces visible"), SameImage(Positive, ViaInspector));
	Mirrored.SetScale3D(FVector(1.5, 1, 1.2));
	Preview->UpdateEntry(Entry.EntryGuid, Mirrored);
	TestTrue(TEXT("Interactive unmirror restores the original rendered surfaces"), SameImage(Positive, Capture()));
	Mirrored.SetScale3D(FVector(1.5, -1, 1.2));
	Preview->UpdateEntry(Entry.EntryGuid, Mirrored);
	TestTrue(TEXT("Interactive remirror preserves outward surfaces"), SameImage(Positive, Capture()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationPreviewSynchronizationTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.PreviewSynchronization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationPreviewSynchronizationTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	const auto Preview = SNew(SDeepLevelCityBuildingDecorationViewport);
	const auto Building = Fixture.Profile->BuildingClass;
	const FGuid Variant = Fixture.Document.GetVariantId();
	const FGuid Entry = Fixture.Document.GetEntryId();
	Preview->Synchronize(Building, Fixture.Profile.Get(), Variant);
	auto* BuildingActor = Cast<ADeepLevelCityDecorationPreviewTestBuilding>(Preview->GetBuildingActor());
	if (!TestNotNull(TEXT("Building preview exists"), BuildingActor)) { return false; }
	TestTrue(TEXT("Construction sees the preview flag before executing"), BuildingActor->bPreviewAtConstruction);
	AActor* Original = Preview->GetEntryActor(Entry);
	if (!TestNotNull(TEXT("Decoration preview exists"), Original)) { return false; }
	Fixture.Document.SetVariantFields(TEXT("Renamed"), 2);
	Preview->Synchronize(Building, Fixture.Profile.Get(), Variant);
	TestTrue(TEXT("Metadata changes preserve the output actor"), Preview->GetEntryActor(Entry) == Original);
	FText Error;
	Fixture.Document.SetTransform(FTransform(FVector(100, 0, 0)), Error);
	Preview->Synchronize(Building, Fixture.Profile.Get(), Variant);
	TestTrue(TEXT("Transform changes preserve the output actor"), Preview->GetEntryActor(Entry) == Original);
	TestEqual(TEXT("Transform is applied to the retained actor"), Original->GetActorLocation(), FVector(100, 0, 0));
	Fixture.Document.RemoveEntry();
	Preview->Synchronize(Building, Fixture.Profile.Get(), Variant);
	TestNull(TEXT("Removing an entry removes only its preview output"), Preview->GetEntryActor(Entry));
	TestTrue(TEXT("Entry editing keeps the building actor"), Preview->GetBuildingActor() == BuildingActor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationSurfaceTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.RenderSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationSurfaceTest::RunTest(const FString&)
{
	const auto Preview = SNew(SDeepLevelCityBuildingDecorationViewport);
	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	Params.bTemporaryEditorActor = true;
	AActor* Building = Preview->GetPreviewScene()->GetWorld()->SpawnActor<AActor>(Params);
	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Building);
	Building->SetRootComponent(Component);
	Building->AddInstanceComponent(Component);
	Component->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->RegisterComponent();
	Component->AddInstance(FTransform(FVector(0, 0, 200)));
	FDeepLevelCityDecorationSurface Surface;
	Surface.Build(Building);
	FHitResult Hit;
	TestTrue(TEXT("Render geometry of packed instances works without gameplay collision"), Surface.Trace(FVector(0, 0, 500), FVector(0, 0, 0), Hit));
	TestTrue(TEXT("Hit identifies the upper render surface"), FMath::IsNearlyEqual(Hit.ImpactPoint.Z, 250.0, 0.1));
	TestTrue(TEXT("Surface normal faces the placement ray"), Hit.ImpactNormal.Z > 0.99);
	TestFalse(TEXT("Empty space never produces an invented surface"), Surface.Trace(FVector(200, 0, 500), FVector(200, 0, 0), Hit));
	FTransform Contact;
	TestTrue(TEXT("Contact uses mesh geometry instead of its offset pivot"), Surface.GetContactTransform(
		FTransform::Identity, FVector(0, 0, 300), FVector::UpVector, Contact));
	TestTrue(TEXT("Offset instance bottom rests exactly on target"), FMath::IsNearlyEqual(Contact.GetLocation().Z, 150.0));
	TestTrue(TEXT("Rotated nonuniform geometry can be placed against a wall"), Surface.GetContactTransform(
		FTransform(FRotator(0, 45, 0), FVector::ZeroVector, FVector(2, 1, 1)), FVector(300, 0, 0), FVector::ForwardVector, Contact));
	TestTrue(TEXT("Wall contact follows actual rotated vertices"), FMath::IsNearlyEqual(Contact.GetLocation().X, 300.0 + 150.0 / FMath::Sqrt(2.0), 0.01));
	Surface.UpdateActorTransform(FTransform(FVector(100, 0, 0)));
	TestFalse(TEXT("Moving an actor removes its old cached hit"), Surface.Trace(FVector(0, 0, 500), FVector(0, 0, 0), Hit));
	TestTrue(TEXT("Moving an actor updates hits without rebuilding vertices"), Surface.Trace(FVector(100, 0, 500), FVector(100, 0, 0), Hit));
	Preview->GetPreviewScene()->GetWorld()->DestroyActor(Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationValidationParityTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.ValidationParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationValidationParityTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	const auto Entry = *Fixture.Document.GetEntry();
	auto Desired = Entry;
	Desired.Output = EDeepLevelCityDecorationOutput::Decal;
	Desired.DecalMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	FText Error;
	Fixture.Document.SetEntryFields(Desired, Entry, Error);
	TestFalse(TEXT("Profile validation rejects a non-decal material"), Fixture.Profile->Validate(Error));
	TestFalse(TEXT("Runtime output validation rejects the same material"), DeepLevelCityDecorationValidation::ValidateOutput(
		Desired.Output, Desired.Mesh, Desired.ActorClass, Desired.DecalMaterial, Desired.DecalSize, Error));
	const auto* Issue = Fixture.Document.GetIssues().FindByPredicate([&](const auto& Item) { return Item.Entry == Entry.EntryGuid; });
	TestTrue(TEXT("Validation identifies the exact variant and entry"), Issue && Issue->Variant == Fixture.Document.GetVariantId());
	Fixture.Profile->Variants[0].Entries.Add(Desired);
	Fixture.Profile->PostEditChange();
	TestFalse(TEXT("Ambiguous identities block authoring instead of changing an arbitrary entry"), Fixture.Document.CanEdit());
	TestEqual(TEXT("Corruption is not silently repaired"), Fixture.Profile->Variants[0].Entries.Last().EntryGuid, Entry.EntryGuid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationAssignmentRepairTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.AssignmentRemoval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationAssignmentRepairTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	Fixture.Set->BuildingProfiles.Add(Fixture.Profile.Get());
	Fixture.Set->BuildingProfiles.Add(nullptr);
	Fixture.Document.Open(Fixture.Set.Get());
	TestFalse(TEXT("Conflicting profiles cannot be silently selected for editing"), Fixture.Document.CanEdit());
	const auto* Issue = Fixture.Document.GetIssues().FindByPredicate([](const auto& Item) { return Item.AssignmentIndex == 1; });
	if (!TestNotNull(TEXT("Conflict identifies its exact assignment"), Issue)) { return false; }
	const auto Conflict = *Issue;
	TestTrue(TEXT("One conflict can be explicitly removed while another issue remains"),
		Fixture.Document.RemoveAssignment(Conflict.AssignmentSet.Get(), Conflict.AssignmentIndex, Conflict.AssignmentProfile.Get()));
	TestTrue(TEXT("The remaining exact building profile becomes editable"), Fixture.Document.CanEdit());
	TestFalse(TEXT("Stale issue rows cannot unlink a different assignment"),
		Fixture.Document.RemoveAssignment(Conflict.AssignmentSet.Get(), Conflict.AssignmentIndex, Conflict.AssignmentProfile.Get()));
	TestTrue(TEXT("A missing assignment is explicitly removable"), Fixture.Document.RemoveAssignment(Fixture.Set.Get(), 1, nullptr));
	TestEqual(TEXT("Only the valid profile remains linked"), Fixture.Set->BuildingProfiles.Num(), 1);
	Fixture.Document.Undo();
	TestEqual(TEXT("Undo restores the removed assignment"), Fixture.Set->BuildingProfiles.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationToolkitIntegrationTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.ToolkitIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationToolkitIntegrationTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	Fixture.Set->SetFlags(RF_Transient);
	Fixture.Profile->SetFlags(RF_Transient);
	TSharedPtr<FDeepLevelCityBuildingDecorationEditor> Toolkit = MakeShared<FDeepLevelCityBuildingDecorationEditor>();
	Toolkit->InitEditor(EToolkitMode::Standalone, {}, Fixture.Set.Get());
	TestEqual(TEXT("Set workspace keeps its opened asset title"), Toolkit->GetToolkitName().ToString(), Fixture.Set->GetName());
	TestTrue(TEXT("Set workspace tooltip includes its editable profile"), Toolkit->GetToolkitToolTipText().ToString().Contains(Fixture.Profile->GetName()));
	const FGuid Entry = Fixture.Profile->Variants[0].Entries[0].EntryGuid;
	Toolkit->Document->SelectVariant(Fixture.Profile->Variants[0].VariantGuid);
	const FVector DropLocation(100, 200, 300);
	const TArray<FAssetData> DroppedAssets{ FAssetData(Fixture.Profile->Variants[0].Entries[0].Mesh.LoadSynchronous()) };
	TestTrue(TEXT("Dropped assets are added through the toolkit document"), Toolkit->AddDroppedAssets(DroppedAssets, DropLocation, FVector::UpVector));
	TestTrue(TEXT("Drop selects the newly created decoration"), Toolkit->GetSelectedEntryId().IsValid() && Toolkit->GetSelectedEntryId() != Entry);
	if (const auto* Dropped = Toolkit->GetSelectedEntry())
	{
		TestEqual(TEXT("Dropped decoration uses its building surface location"), Dropped->LocalTransform.GetLocation(), DropLocation);
		TestTrue(TEXT("Dropped decoration is materialized in the preview"), Toolkit->Preview->GetEntryActor(Dropped->EntryGuid) != nullptr);
	}
	else { AddError(TEXT("Dropped decoration was not selected")); }
	Toolkit->SelectEntry(Entry);
	TestTrue(TEXT("Selection uses exactly one entry inspector"), Toolkit->Inspector->GetSelectedObjects().Num() == 1
		&& Toolkit->Inspector->GetSelectedObjects()[0].Get() == Toolkit->EntryProxy.Get());
	const auto Preview = Toolkit->Preview;
	AActor* Original = Preview->GetEntryActor(Entry);
	Toolkit->EntryProxy->Entry.Name = TEXT("InspectorRename");
	FPropertyChangedEvent Event(FindFProperty<FProperty>(UDeepLevelCityDecorationEntryProxy::StaticClass(), GET_MEMBER_NAME_CHECKED(UDeepLevelCityDecorationEntryProxy, Entry)));
	Toolkit->CommitInspector(Event);
	TestEqual(TEXT("Actual inspector commit goes through the document"), Toolkit->Document->GetEntry()->Name, FName(TEXT("InspectorRename")));
	TestTrue(TEXT("Inspector metadata edits preserve preview actors"), Preview->GetEntryActor(Entry) == Original);
	Toolkit->Document->Undo();
	TestEqual(TEXT("Undo synchronizes the inspector proxy"), Toolkit->EntryProxy->Entry.Name, FName(TEXT("Cube")));
	TestEqual(TEXT("Undo preserves the selected decoration"), Toolkit->GetSelectedEntryId(), Entry);
	Toolkit->OutlineClicked(Toolkit->OutlineItems[0]);
	TestNull(TEXT("Selecting the building clears the variant decoration preview"), Preview->GetEntryActor(Entry));
	TestTrue(TEXT("Building row exposes catalog calibration through the single inspector"),
		Toolkit->Inspector->GetSelectedObjects().Num() == 1 && Toolkit->Inspector->GetSelectedObjects()[0].Get() == Toolkit->BuildingProxy.Get());
	Toolkit->OutlineClicked(Toolkit->OutlineItems[0]->Children[0]);
	TestTrue(TEXT("Variant row exposes only variant settings"), Toolkit->Inspector->GetSelectedObjects()[0].Get() == Toolkit->VariantProxy.Get());
	Toolkit->OutlineClicked(Toolkit->OutlineItems[0]->Children[0]->Children[0]);
	Toolkit->ShowSetSettings();
	Toolkit->OutlineClicked(Toolkit->OutlineItems[0]->Children[0]->Children[0]);
	TestTrue(TEXT("Clicking an existing selection restores its own inspector"), Toolkit->Inspector->GetSelectedObjects()[0].Get() == Toolkit->EntryProxy.Get());
	FHitResult SurfaceHit;
	TestFalse(TEXT("The selected decoration cannot hit its own surface"),
		Preview->TraceSurface(FVector(0, 0, 500), FVector(0, 0, -500), SurfaceHit));
	TestTrue(TEXT("Other decorations are valid target surfaces"),
		Preview->TraceSurface(FVector(100, 200, 600), FVector(100, 200, 0), SurfaceHit));
	TestEqual(TEXT("Decoration target uses render triangles"), SurfaceHit.ImpactPoint.Z, 350.0);
	TGuardValue<float> SurfaceOffset(GetMutableDefault<ULevelEditorViewportSettings>()->SnapToSurface.SnapOffsetExtent, 0.f);
	Toolkit->PlaceSelectedOnSurface(FVector::ZeroVector, FVector::UpVector);
	TestTrue(TEXT("Selected cube is flush above the target plane"), FMath::IsNearlyEqual(Toolkit->GetSelectedEntry()->LocalTransform.GetLocation().Z, 50.0));
	TestEqual(TEXT("Surface placement preserves preview actor and synchronizes position"),
		Preview->GetEntryActor(Entry)->GetActorLocation(), Toolkit->GetSelectedEntry()->LocalTransform.GetLocation());
	Toolkit->Document->Undo();
	const TSharedRef<int32> Notifications = MakeShared<int32>(0);
	Toolkit->Document->OnChanged.AddLambda([Notifications](EDeepLevelCityDecorationChange, FGuid) { ++*Notifications; });
	Toolkit->ApplyWidgetDelta(FVector(20, 0, 0), FRotator::ZeroRotator, FVector::ZeroVector);
	const int32 BeforeClose = *Notifications;
	const TWeakObjectPtr<AActor> ActorBeforeClose = Preview->GetEntryActor(Entry);
	TestTrue(TEXT("The reselected variant has a valid preview actor before closing"), ActorBeforeClose.IsValid());
	Toolkit->CloseWindow(EAssetEditorCloseReason::AssetForceDeleted);
	Toolkit.Reset();
	TestEqual(TEXT("Toolkit destruction with an active drag performs no preview refresh"), *Notifications, BeforeClose);
	TestTrue(TEXT("Closing the editor preserves the actor present immediately before closing"),
		ActorBeforeClose.IsValid() && Preview->GetEntryActor(Entry) == ActorBeforeClose.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationSetSelectionTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.SetSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationSetSelectionTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	FDecorationFixture Other;
	for (UObject* Asset : { static_cast<UObject*>(Fixture.Set.Get()), static_cast<UObject*>(Fixture.Profile.Get()),
		static_cast<UObject*>(Other.Set.Get()), static_cast<UObject*>(Other.Profile.Get()) }) { Asset->SetFlags(RF_Transient); }
	TSharedPtr<FDeepLevelCityBuildingDecorationEditor> Toolkit = MakeShared<FDeepLevelCityBuildingDecorationEditor>();
	Toolkit->InitEditor(EToolkitMode::Standalone, {}, Fixture.Set.Get());
	TestTrue(TEXT("The workspace opens from an explicit City Set"), Toolkit->Document->GetSet() == Fixture.Set.Get());
	TestEqual(TEXT("Set entry registers its editable profile for saving"), Toolkit->GetEditingObjects().Num(), 2);
	const FString Title = Fixture.Set->GetName();
	TestEqual(TEXT("Workspace title names the opened set"), Toolkit->GetToolkitName().ToString(), Title);
	TestTrue(TEXT("Workspace tooltip includes the opened set"), Toolkit->GetToolkitToolTipText().ToString().Contains(Title));

	Toolkit->ChooseSet(FAssetData(Fixture.Set.Get()));
	TestTrue(TEXT("Picker selects the explicit set"), Toolkit->Document->GetSet() == Fixture.Set.Get());
	TestTrue(TEXT("Picker resolves the matching catalog profile"), Toolkit->Document->GetProfile() == Fixture.Profile.Get());
	TestTrue(TEXT("Picker enables valid authoring"), Toolkit->Document->CanEdit());
	TestEqual(TEXT("Set and profile are both registered for editing"), Toolkit->GetEditingObjects().Num(), 2);
	TestEqual(TEXT("Multi-asset title retains the opened set identity"), Toolkit->GetToolkitName().ToString(), Title);
	const FString ToolTip = Toolkit->GetToolkitToolTipText().ToString();
	TestTrue(TEXT("Multi-asset tooltip includes profile and set"), ToolTip.Contains(Title) && ToolTip.Contains(Fixture.Set->GetName()));
	Toolkit->ChooseSet(FAssetData(Fixture.Set.Get()));
	TestEqual(TEXT("Repeated selection does not duplicate save registrations"), Toolkit->GetEditingObjects().Num(), 2);

	Toolkit->ChooseSet(FAssetData(Other.Set.Get()));
	TestTrue(TEXT("Changing set resolves its own profile"), Toolkit->Document->GetProfile() == Other.Profile.Get());
	TestEqual(TEXT("Previously edited assets remain in the save scope"), Toolkit->GetEditingObjects().Num(), 4);
	TestEqual(TEXT("Context changes preserve the workspace identity"), Toolkit->GetToolkitName().ToString(), Title);
	TestTrue(TEXT("Tooltip includes the new context"), Toolkit->GetToolkitToolTipText().ToString().Contains(Other.Set->GetName()));
	Toolkit->ChooseSet(FAssetData());
	TestNull(TEXT("Clearing the picker removes set context"), Toolkit->Document->GetSet());
	TestEqual(TEXT("Clearing the picker preserves registered edited assets"), Toolkit->GetEditingObjects().Num(), 4);
	TestEqual(TEXT("Title remains valid with a cleared picker"), Toolkit->GetToolkitName().ToString(), Title);
	TestTrue(TEXT("Tooltip remains valid with a cleared picker"), Toolkit->GetToolkitToolTipText().ToString().Contains(Title));
	Toolkit->CloseWindow(EAssetEditorCloseReason::AssetForceDeleted);
	Toolkit.Reset();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationPreviewLifetimeTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.PreviewSceneLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationPreviewLifetimeTest::RunTest(const FString&)
{
	if (!TestTrue(TEXT("Slate is initialized for the editor viewport"), FSlateApplication::IsInitialized())) { return false; }
	TSharedPtr<SDeepLevelCityBuildingDecorationViewport> Preview = SNew(SDeepLevelCityBuildingDecorationViewport);
	TSharedPtr<FDeepLevelCityBuildingDecorationViewportClient> Client = Preview->GetClient();
	const TWeakPtr<FAdvancedPreviewScene> Scene = Preview->GetPreviewScene();
	UWorld* World = Client->GetWorld();
	Preview.Reset();
	TestNull(TEXT("Closing the preview detaches its scene viewport from the retained client"), Client->Viewport);
	if (TestTrue(TEXT("A retained viewport client keeps its preview scene alive"), Scene.IsValid()))
	{
		TestTrue(TEXT("Retained client world access remains bound to its own preview scene"), Client->GetWorld() == World);
		TestTrue(TEXT("Retained client scene access remains valid"), Client->GetScene() == World->Scene);
	}
	Client.Reset();
	TestFalse(TEXT("Releasing the last viewport client releases the preview scene"), Scene.IsValid());
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationOutlineTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.Outline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationOutlineTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	Fixture.Set->SetFlags(RF_Transient);
	Fixture.Profile->SetFlags(RF_Transient);
	Fixture.Document.SetVariantFields(TEXT("Facade"), 1);
	TSharedPtr<FDeepLevelCityBuildingDecorationEditor> Toolkit = MakeShared<FDeepLevelCityBuildingDecorationEditor>();
	Toolkit->InitEditor(EToolkitMode::Standalone, {}, Fixture.Set.Get());
	if (!TestEqual(TEXT("Outline roots come exclusively from the building catalog"), Toolkit->OutlineItems.Num(), 2)) { return false; }
	const auto Building = Toolkit->OutlineItems[0];
	if (!TestEqual(TEXT("The linked building exposes its variants"), Building->Children.Num(), 1)) { return false; }
	const auto Variant = Building->Children[0];
	if (!TestEqual(TEXT("Variant exposes decoration children"), Variant->Children.Num(), 1)) { return false; }
	const auto Entry = Variant->Children[0];
	TestTrue(TEXT("A building without a profile has no fabricated children"), Toolkit->OutlineItems[1]->Children.IsEmpty());
	Toolkit->OutlineClicked(Entry);
	TestEqual(TEXT("One nested click selects the variant"), Toolkit->Document->GetVariantId(), Variant->Variant);
	TestEqual(TEXT("One nested click selects the decoration"), Toolkit->GetSelectedEntryId(), Entry->Entry);
	const auto CurrentBuilding = Toolkit->OutlineItems[0];
	const auto CurrentVariant = CurrentBuilding->Children[0];
	TestTrue(TEXT("Preview selection is reflected in the native tree"), Toolkit->Outline->IsItemSelected(CurrentVariant->Children[0]));
	TestTrue(TEXT("Selection expands both ancestors"), Toolkit->Outline->IsItemExpanded(CurrentBuilding) && Toolkit->Outline->IsItemExpanded(CurrentVariant));
	Toolkit->SearchChanged(FText::FromString(TEXT("Cube")));
	TestEqual(TEXT("Child search retains its building ancestor"), Toolkit->OutlineItems.Num(), 1);
	TestEqual(TEXT("Child search retains its variant ancestor"), Toolkit->OutlineItems[0]->Children.Num(), 1);
	TestEqual(TEXT("Filtering never changes document selection"), Toolkit->GetSelectedEntryId(), Entry->Entry);
	Toolkit->SearchChanged(FText::FromString(TEXT("Facade")));
	TestTrue(TEXT("A matching variant retains its decorations"), Toolkit->OutlineItems[0]->Children[0]->Children.Num() == 1);
	Toolkit->SearchChanged(FText::FromString(TEXT("APackedLevelActor")));
	TestEqual(TEXT("Filtering out the selected item does not select a substitute parent"), Toolkit->Outline->GetSelectedItems().Num(), 0);
	Toolkit->SearchChanged(FText::FromString(TEXT("NoSuchDecoration")));
	TestTrue(TEXT("A miss has no invented rows"), Toolkit->OutlineItems.IsEmpty());
	TestEqual(TEXT("A miss preserves the actual selected entry"), Toolkit->GetSelectedEntryId(), Entry->Entry);
	Toolkit->SearchChanged(FText::GetEmpty());
	TestEqual(TEXT("Clearing search restores catalog roots"), Toolkit->OutlineItems.Num(), 2);
	TestTrue(TEXT("Expansion survives a filter rebuild"), Toolkit->Outline->IsItemExpanded(Toolkit->OutlineItems[0]));
	Toolkit->OutlineClicked(Toolkit->OutlineItems[0]->Children[0]);
	TestFalse(TEXT("Variant selection clears decoration selection"), Toolkit->GetSelectedEntryId().IsValid());
	Toolkit->Preview->GetCommandList()->ExecuteAction(FGenericCommands::Get().Duplicate.ToSharedRef());
	TestEqual(TEXT("Native duplicate command duplicates a selected variant"), Fixture.Profile->Variants.Num(), 2);
	Toolkit->Document->Undo();
	TestEqual(TEXT("Variant command is one undoable document edit"), Fixture.Profile->Variants.Num(), 1);
	Fixture.Set->BuildingProfiles.Add(Fixture.Profile.Get());
	Toolkit->Document->Open(Fixture.Set.Get());
	TestTrue(TEXT("Ambiguous profile assignments do not expose guessed variants"), Toolkit->OutlineItems[0]->Children.IsEmpty());
	TestFalse(TEXT("Ambiguous assignments disable authoring"), Toolkit->Document->CanEdit());
	Toolkit->CloseWindow(EAssetEditorCloseReason::AssetForceDeleted);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationSymmetryMathTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.Symmetry.Math",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationSymmetryMathTest::RunTest(const FString&)
{
	FDeepLevelCityDecorationSymmetryPair Pair;
	Pair.PlaneOrigin = FVector(20, -40, 10);
	const FTransform Source(FRotator(15, 35, -20), FVector(100, 200, 300), FVector(2, 3, 4));
	const FTransform Mirror = Pair.GetMirroredTransform(Source);
	TestEqual(TEXT("Reflection uses the captured center rather than actor origin"), Mirror.GetLocation(), FVector(100, -280, 300));
	TestEqual(TEXT("Placement mode preserves readable geometry scale"), Mirror.GetScale3D(), Source.GetScale3D());
	TestTrue(TEXT("Default placement reflection is reversible"), Pair.GetMirroredTransform(Mirror).Equals(Source, 0.0001));
	const FVector Forward = Source.GetRotation().GetAxisX();
	TestTrue(TEXT("Light and decal projection axes reflect correctly"), Mirror.GetRotation().GetAxisX().Equals(FVector(Forward.X, -Forward.Y, Forward.Z), 0.0001));
	Pair.bMirrorGeometry = true;
	const FTransform SignedSource(FRotator(-10, 60, 25), FVector(300, -200, 90), FVector(2, -3, 4));
	for (const FVector& Axis : {FVector::ForwardVector, FVector::RightVector, FVector::UpVector})
	{
		Pair.PlaneNormal = FRotator(20, 30, 10).RotateVector(Axis);
		const FTransform Result = Pair.GetMirroredTransform(SignedSource);
		TestTrue(TEXT("Arbitrary rotated plane remains reversible with signed scale"), Pair.GetMirroredTransform(Result).Equals(SignedSource, 0.0001));
		for (const FVector& Vertex : {FVector(1, 2, 3), FVector(-2, 5, -7), FVector::ZeroVector})
		{
			const FVector Position = SignedSource.TransformPosition(Vertex);
			const FVector Expected = Position - 2.0 * FVector::DotProduct(Position - Pair.PlaneOrigin, Pair.PlaneNormal) * Pair.PlaneNormal;
			TestTrue(TEXT("Mirrored geometry follows reflection exactly without shear"), Result.TransformPosition(Vertex).Equals(Expected, 0.0001));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationSymmetryDocumentTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.Symmetry.Document",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationSymmetryDocumentTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	auto& Document = Fixture.Document;
	FText Error;
	const FTransform Source(FRotator(10, 30, 0), FVector(100, 200, 90), FVector(2, 3, 1));
	Document.SetTransform(Source, Error);
	TestFalse(TEXT("Uncalibrated buildings do not guess a mirror center"), Document.CanCreateSymmetry());
	auto& Definition = Fixture.Catalog->Buildings[0];
	Definition.bCalibrated = true;
	Definition.PlacementVolume.Center = FVector(20, 30, 40);
	Definition.PlacementVolume.Rotation = FRotator(0, 25, 0);
	const FGuid First = Document.GetEntryId();
	Document.CreateSymmetry(EAxis::Y);
	if (!TestNotNull(TEXT("A persisted pair was created"), Document.GetSymmetryPair())) { return false; }
	const FDeepLevelCityDecorationSymmetryPair InitialPair = *Document.GetSymmetryPair();
	const FGuid Second = InitialPair.Second;
	TestEqual(TEXT("Creating symmetry retains the source selection"), Document.GetEntryId(), First);
	TestEqual(TEXT("Linked counterpart is an ordinary decoration entry"), Document.GetVariant()->Entries.Num(), 2);
	TestTrue(TEXT("Captured plane follows the calibrated building axes"), InitialPair.PlaneNormal.Equals(Definition.PlacementVolume.Rotation.RotateVector(FVector::RightVector)));
	TestTrue(TEXT("Counterpart has the mirrored baked transform"), Document.GetEntry(Second)->LocalTransform.Equals(InitialPair.GetMirroredTransform(Source)));
	TestFalse(TEXT("An entry cannot belong to a second pair"), Document.CanCreateSymmetry());
	Document.Undo();
	TestEqual(TEXT("Creation is one undoable edit including metadata"), Document.GetVariant()->Entries.Num(), 1);
	TestNull(TEXT("Undo removes the relation"), Document.GetSymmetryPair());
	Document.Redo();
	TestNotNull(TEXT("Redo restores the relation and identities"), Document.GetSymmetryPair());
	Document.SelectEntry(Second);
	const FTransform BeforeFirst = Document.GetEntry(First)->LocalTransform;
	const FTransform BeforeSecond = Document.GetEntry(Second)->LocalTransform;
	const FTransform Desired(FRotator(0, -45, 10), FVector(300, -400, 120), FVector(1, 2, 3));
	Document.ApplyDragTransform(Desired);
	TestTrue(TEXT("Editing the second side updates the first during the same drag"), Document.GetEntry(First)->LocalTransform.Equals(InitialPair.GetMirroredTransform(Desired), 0.001));
	Document.EndDrag();
	Document.Undo();
	TestTrue(TEXT("One undo restores both sides"), Document.GetEntry(First)->LocalTransform.Equals(BeforeFirst) && Document.GetEntry(Second)->LocalTransform.Equals(BeforeSecond));
	auto GeometrySettings = *Document.GetSymmetryPair();
	GeometrySettings.bMirrorGeometry = true;
	Document.SetSymmetrySettings(GeometrySettings, Error);
	TestTrue(TEXT("Geometry mode reflects the shape using signed scale"), FMath::IsNearlyEqual(Document.GetEntry(Second)->LocalTransform.GetScale3D().Y, -Document.GetEntry(First)->LocalTransform.GetScale3D().Y));
	TestTrue(TEXT("Geometry mode preserves a valid relation"), Document.GetVariant()->HasValidSymmetry());
	Document.UnlinkSymmetry();
	TestNull(TEXT("Unlink removes only authoring metadata"), Document.GetSymmetryPair());
	TestEqual(TEXT("Unlink keeps both decorations"), Document.GetVariant()->Entries.Num(), 2);
	Document.SetTransform(Desired, Error);
	TestTrue(TEXT("Detached editing leaves the other side untouched"), Document.GetEntry(First)->LocalTransform.Equals(BeforeFirst));
	Document.Undo();
	Document.Undo();
	TestNotNull(TEXT("Undo restores the previous linked mode"), Document.GetSymmetryPair());
	Document.DuplicateEntry();
	TestNull(TEXT("Ordinary decoration duplication is independent"), Document.GetSymmetryPair());
	const FGuid OriginalVariant = Document.GetVariantId();
	Document.DuplicateVariant();
	TestEqual(TEXT("Arrangement duplication preserves its pair"), Document.GetVariant()->SymmetryPairs.Num(), 1);
	TestTrue(TEXT("Copied arrangement remaps pair endpoints"), Document.GetVariant()->SymmetryPairs[0].First != First && Document.GetVariant()->HasValidSymmetry());
	const auto CopyPair = Document.GetVariant()->SymmetryPairs[0];
	Document.SelectEntry(CopyPair.Second);
	Document.RemoveEntry();
	TestTrue(TEXT("Deleting one side removes its relation"), Document.GetVariant()->SymmetryPairs.IsEmpty());
	TestNotNull(TEXT("Deleting one side retains the other"), Document.GetEntry(CopyPair.First));
	Document.Undo();
	TestTrue(TEXT("Undo restores a valid copied relation"), Document.GetVariant()->HasValidSymmetry() && Document.GetVariant()->SymmetryPairs.Num() == 1);
	TStrongObjectPtr<UDeepLevelCityBuildingDecorationProfile> Duplicate(DuplicateObject(Fixture.Profile.Get(), GetTransientPackage()));
	TestTrue(TEXT("Whole profile duplication remaps pair identities"), Duplicate->Variants[0].HasValidSymmetry() && Duplicate->Variants[0].SymmetryPairs[0].First != First);
	const auto RoundTrip = [&](bool bFilterEditorData)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, true);
		FObjectAndNameAsStringProxyArchive WriteArchive(Writer, false);
		WriteArchive.SetFilterEditorOnly(bFilterEditorData);
		Fixture.Profile->Serialize(WriteArchive);
		TStrongObjectPtr<UDeepLevelCityBuildingDecorationProfile> Loaded(NewObject<UDeepLevelCityBuildingDecorationProfile>());
		FMemoryReader Reader(Bytes, true);
		FObjectAndNameAsStringProxyArchive ReadArchive(Reader, true);
		ReadArchive.SetFilterEditorOnly(bFilterEditorData);
		Loaded->Serialize(ReadArchive);
		TestFalse(TEXT("In-memory profile serialization succeeds"), WriteArchive.IsError() || ReadArchive.IsError());
		return Loaded;
	};
	const auto Saved = RoundTrip(false);
	TestTrue(TEXT("Authoring serialization retains the linked pair"), Saved->Variants.Num() == 2 && Saved->Variants[0].SymmetryPairs.Num() == 1 && Saved->Variants[0].HasValidSymmetry());
	const auto Cooked = RoundTrip(true);
	TestTrue(TEXT("Cook filtering removes metadata and retains baked outputs"), Cooked->Variants.Num() == 2 && Cooked->Variants[0].SymmetryPairs.IsEmpty()
		&& Cooked->Variants[0].Entries.Num() == Fixture.Profile->Variants[0].Entries.Num()
		&& Cooked->Variants[0].Entries[1].LocalTransform.Equals(Fixture.Profile->Variants[0].Entries[1].LocalTransform));
	Document.SelectVariant(OriginalVariant);
	Fixture.Profile->Variants[0].SymmetryPairs[0].Second = FGuid::NewGuid();
	TestFalse(TEXT("Broken relations block authoring instead of guessing an endpoint"), Document.CanEdit());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationSymmetryPreviewTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.Symmetry.Preview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationSymmetryPreviewTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	Fixture.Set->SetFlags(RF_Transient);
	Fixture.Profile->SetFlags(RF_Transient);
	Fixture.Catalog->Buildings[0].bCalibrated = true;
	TSharedPtr<FDeepLevelCityBuildingDecorationEditor> Toolkit = MakeShared<FDeepLevelCityBuildingDecorationEditor>();
	Toolkit->InitEditor(EToolkitMode::Standalone, {}, Fixture.Set.Get());
	Toolkit->Document->SelectVariant(Fixture.Profile->Variants[0].VariantGuid);
	const FGuid First = Fixture.Profile->Variants[0].Entries[0].EntryGuid;
	Toolkit->SelectEntry(First);
	FText Error;
	Toolkit->Document->SetTransform(FTransform(FRotator(0, 20, 0), FVector(100, 200, 80)), Error);
	Toolkit->Document->CreateSymmetry(EAxis::Y);
	if (!TestNotNull(TEXT("Toolkit creates a linked counterpart"), Toolkit->GetSelectedSymmetryPair())) { return false; }
	const auto Pair = *Toolkit->GetSelectedSymmetryPair();
	AActor* FirstActor = Toolkit->Preview->GetEntryActor(First);
	AActor* SecondActor = Toolkit->Preview->GetEntryActor(Pair.Second);
	if (!TestNotNull(TEXT("Counterpart has a preview output"), SecondActor)) { return false; }
	TestTrue(TEXT("Both outline entries show their link"), Toolkit->OutlineItems[0]->Children[0]->Children[0]->Detail.ToString().Contains(TEXT("Linked"))
		&& Toolkit->OutlineItems[0]->Children[0]->Children[1]->Detail.ToString().Contains(TEXT("Linked")));
	Toolkit->ApplyWidgetDelta(FVector(20, 30, 40), FRotator(0, 15, 0), FVector(0.25));
	TestTrue(TEXT("Counterpart preview updates during gizmo motion"), SecondActor->GetActorTransform().Equals(Pair.GetMirroredTransform(FirstActor->GetActorTransform()), 0.001));
	Toolkit->EndWidgetDrag();
	TestTrue(TEXT("Both actors remain stable across linked edits"), Toolkit->Preview->GetEntryActor(First) == FirstActor && Toolkit->Preview->GetEntryActor(Pair.Second) == SecondActor);
	FHitResult Hit;
	const FVector PartnerPosition = SecondActor->GetActorLocation();
	TestFalse(TEXT("Surface snapping excludes both moving members of the pair"), Toolkit->Preview->TraceSurface(
		PartnerPosition + FVector(0, 0, 500), PartnerPosition - FVector(0, 0, 500), Hit));
	TGuardValue<float> SurfaceOffset(GetMutableDefault<ULevelEditorViewportSettings>()->SnapToSurface.SnapOffsetExtent, 0.f);
	Toolkit->PlaceSelectedOnSurface(FVector(300, 250, 0), FVector::UpVector);
	TestTrue(TEXT("Surface placement updates the linked actor too"), SecondActor->GetActorTransform().Equals(Pair.GetMirroredTransform(FirstActor->GetActorTransform()), 0.001));
	TestTrue(TEXT("Surface placement retains the selected source"), Toolkit->GetSelectedEntryId() == First);
	Toolkit->Document->UnlinkSymmetry();
	TestTrue(TEXT("Unlink keeps existing preview objects"), Toolkit->Preview->GetEntryActor(First) == FirstActor && Toolkit->Preview->GetEntryActor(Pair.Second) == SecondActor);
	const FVector DetachedPosition = SecondActor->GetActorLocation();
	Toolkit->ApplyWidgetDelta(FVector(10, 0, 0), FRotator::ZeroRotator, FVector::ZeroVector);
	Toolkit->EndWidgetDrag();
	TestEqual(TEXT("Detached gizmo motion does not move the other actor"), SecondActor->GetActorLocation(), DetachedPosition);
	Toolkit->CloseWindow(EAssetEditorCloseReason::AssetForceDeleted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationViewportSelectionTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.ViewportSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationViewportSelectionTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	Fixture.Catalog->Buildings[0].PlacementVolume.Exposure.PositiveX = EDeepLevelStreetExposureRule::Required;
	Fixture.Catalog->Buildings[0].PlacementVolume.Exposure.PositiveY = EDeepLevelStreetExposureRule::Required;
	Fixture.Catalog->Buildings[1].PlacementVolume.Exposure.NegativeX = EDeepLevelStreetExposureRule::Required;
	Fixture.Set->SetFlags(RF_Transient);
	Fixture.Profile->SetFlags(RF_Transient);
	const auto Toolkit = MakeShared<FDeepLevelCityBuildingDecorationEditor>();
	Toolkit->InitEditor(EToolkitMode::Standalone, {}, Fixture.Set.Get());
	Toolkit->Document->SelectVariant(Fixture.Profile->Variants[0].VariantGuid);
	const FGuid Entry = Fixture.Profile->Variants[0].Entries[0].EntryGuid;
	const auto Preview = Toolkit->Preview;
	const auto Client = Preview->GetClient();
	AActor* Actor = Preview->GetEntryActor(Entry);
	if (!TestNotNull(TEXT("Selectable decoration exists"), Actor))
	{
		Toolkit->CloseWindow(EAssetEditorCloseReason::AssetForceDeleted);
		return false;
	}
	FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(Client->Viewport, Client->GetScene(), Client->EngineShowFlags));
	FSceneViewInitOptions Options;
	Options.ViewFamily = &Family;
	Options.SetViewRectangle(FIntRect(0, 0, 640, 480));
	FSceneView View(Options);
	TRefCountPtr<HHitProxy> ActorHit = new HActor(Actor, nullptr);
	TGuardValue<bool> SurfaceSnap(GetMutableDefault<ULevelEditorViewportSettings>()->SnapToSurface.bEnabled, true);
	const FTransform Before = Actor->GetActorTransform();
	Toolkit->SelectEntry({});
	Client->ProcessClick(View, ActorHit, EKeys::LeftMouseButton, IE_Pressed, 0, 0);
	TestFalse(TEXT("Press alone does not select before camera/gizmo drag classification"), Toolkit->GetSelectedEntryId().IsValid());
	Client->ProcessClick(View, ActorHit, EKeys::LeftMouseButton, IE_Released, 0, 0);
	TestEqual(TEXT("Native release click selects decoration even with surface snap enabled"), Toolkit->GetSelectedEntryId(), Entry);
	TestTrue(TEXT("Selection synchronizes the details inspector"), Toolkit->Inspector->GetSelectedObjects()[0].Get() == Toolkit->EntryProxy.Get());
	TestTrue(TEXT("Selection never moves decoration to the clicked surface"), Actor->GetActorTransform().Equals(Before));
	TRefCountPtr<HHitProxy> WidgetHit = new HWidgetAxis(EAxisList::X);
	Client->ProcessClick(View, WidgetHit, EKeys::LeftMouseButton, IE_Released, 0, 0);
	TestEqual(TEXT("Clicking gizmo preserves selection"), Toolkit->GetSelectedEntryId(), Entry);
	const auto& Commands = *Preview->GetCommandList();
	Commands.ExecuteAction(FEditorViewportCommands::Get().RotateMode.ToSharedRef());
	TestEqual(TEXT("Selected decoration enables native rotate command"), Client->GetWidgetMode(), UE::Widget::WM_Rotate);
	Commands.ExecuteAction(FEditorViewportCommands::Get().ScaleMode.ToSharedRef());
	TestEqual(TEXT("Selected decoration enables native scale command"), Client->GetWidgetMode(), UE::Widget::WM_Scale);
	Commands.ExecuteAction(FEditorViewportCommands::Get().TranslateMode.ToSharedRef());
	TestEqual(TEXT("Selected decoration enables native translate command"), Client->GetWidgetMode(), UE::Widget::WM_Translate);
	for (const auto Mode : {UE::Widget::WM_Translate, UE::Widget::WM_Rotate, UE::Widget::WM_Scale})
	{
		TestTrue(TEXT("Document selection enables the transform mode without actor selection"), Client->CanSetWidgetMode(Mode));
		Client->SetWidgetMode(Mode);
		FDecorationGizmoDrawCollector DrawCollector;
		DrawCollector.View = &View;
		Client->Draw(&View, &DrawCollector);
		FlushRenderingCommands();
		TestTrue(TEXT("Selected decoration draws clickable native gizmo geometry"), DrawCollector.AxisMeshes > 0);
	}
	Commands.ExecuteAction(FGenericCommands::Get().Duplicate.ToSharedRef());
	TestEqual(TEXT("Viewport duplication edits the owning variant"), Fixture.Profile->Variants[0].Entries.Num(), 2);
	Commands.ExecuteAction(FGenericCommands::Get().Delete.ToSharedRef());
	TestEqual(TEXT("Viewport delete removes the selected duplicate"), Fixture.Profile->Variants[0].Entries.Num(), 1);
	Commands.ExecuteAction(FGenericCommands::Get().Undo.ToSharedRef());
	TestEqual(TEXT("Viewport undo restores the deleted decoration"), Fixture.Profile->Variants[0].Entries.Num(), 2);
	Client->ProcessClick(View, nullptr, EKeys::LeftMouseButton, IE_Released, 0, 0);
	TestFalse(TEXT("Empty left click clears decoration selection"), Toolkit->GetSelectedEntryId().IsValid());
	Client->ProcessClick(View, ActorHit, EKeys::LeftMouseButton, IE_DoubleClick, 0, 0);
	TestEqual(TEXT("Native double click also selects decoration"), Toolkit->GetSelectedEntryId(), Entry);
	TestTrue(TEXT("Viewport and outline share one selection menu"), Toolkit->BuildSelectionMenu().IsValid());
	TestTrue(TEXT("Initial camera shows both required faces"), Client->GetViewRotation().Vector().GetSafeNormal2D().Equals(FVector(-1, -1, 0).GetSafeNormal()));
	const FVector ManualLocation(2500, -1700, 900);
	const FRotator ManualRotation(-12, 137, 0);
	Client->SetViewLocation(ManualLocation);
	Client->SetViewRotation(ManualRotation);
	const FGuid OtherVariant = Authoring::AddVariant(*Fixture.Profile);
	Toolkit->Document->SelectVariant(OtherVariant);
	TestTrue(TEXT("Variant change preserves camera position"), Client->GetViewLocation().Equals(ManualLocation));
	TestTrue(TEXT("Variant change preserves camera rotation"), Client->GetViewRotation().Equals(ManualRotation));
	Toolkit->Document->SelectBuilding(APackedLevelActor::StaticClass());
	TestTrue(TEXT("Different building frames its own required face"), Client->GetViewRotation().Vector().GetSafeNormal2D().Equals(FVector::ForwardVector));
	Toolkit->CloseWindow(EAssetEditorCloseReason::AssetForceDeleted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationFlexibleLinkMathTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.Symmetry.FlexibleMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationFlexibleLinkMathTest::RunTest(const FString&)
{
	FDeepLevelCityDecorationSymmetryPair Pair;
	Pair.PlaneOrigin = FVector(20, 30, -40);
	Pair.PlaneNormal = FRotator(20, 35, 10).RotateVector(FVector::RightVector);
	Pair.LocationOffset = FVector(10, -30, 20);
	Pair.RotationOffset = FRotator(20, 45, -10);
	Pair.ScaleMultiplier = FVector(2, -3, 0.5);
	const FTransform Source(FRotator(80, 170, -40), FVector(100, 200, 300), FVector(2, -4, 3));
	const FTransform Other(FRotator(-30, 15, 60), FVector(-50, -60, 70), FVector(5, 6, 7));
	for (const auto Mode : {EDeepLevelCityDecorationRotationLink::Mirror, EDeepLevelCityDecorationRotationLink::Copy})
	{
		Pair.RotationMode = Mode;
		for (const bool bGeometry : {false, true})
		{
			Pair.bMirrorGeometry = bGeometry;
			for (int32 Mask = 0; Mask < 8; ++Mask)
			{
				Pair.bSyncLocation = (Mask & 1) != 0;
				Pair.bSyncRotation = (Mask & 2) != 0;
				Pair.bSyncScale = (Mask & 4) != 0;
				const FTransform Result = Pair.SynchronizeTransform(Source, Other, true);
				TestTrue(TEXT("All component combinations invert without drift, including signed nonuniform scale"),
					Pair.SynchronizeTransform(Result, Source, false).Equals(Source, 0.0001));
				if (!Pair.bSyncLocation) { TestEqual(TEXT("Unlinked location is independent"), Result.GetLocation(), Other.GetLocation()); }
				if (!Pair.bSyncRotation) { TestTrue(TEXT("Unlinked rotation is independent"), Result.GetRotation().Equals(Other.GetRotation())); }
				if (!Pair.bSyncScale) { TestEqual(TEXT("Unlinked scale is independent"), Result.GetScale3D(), Other.GetScale3D()); }
				if (Pair.bSyncLocation)
				{
					const FVector Delta = Source.GetLocation() - Pair.PlaneOrigin;
					TestTrue(TEXT("Location offset is added in building space after reflection"), Result.GetLocation().Equals(
						Pair.PlaneOrigin + Delta - 2 * FVector::DotProduct(Delta, Pair.PlaneNormal) * Pair.PlaneNormal + Pair.LocationOffset));
				}
				if (Pair.bSyncRotation && Mode == EDeepLevelCityDecorationRotationLink::Copy)
				{
					TestTrue(TEXT("Copy rotation composes the target-local offset without mirroring"), Result.GetRotation().Equals(
						Source.GetRotation() * Pair.RotationOffset.Quaternion(), 0.0001));
				}
			}
		}
	}
	Pair.bSyncLocation = Pair.bSyncRotation = Pair.bSyncScale = true;
	Pair.CaptureOffsets(Source, Other);
	TestTrue(TEXT("Linking existing transforms captures placement without moving either side"), Pair.SynchronizeTransform(Source, Other, true).Equals(Other, 0.0001));
	TestTrue(TEXT("Captured offsets remain invertible"), Pair.SynchronizeTransform(Other, Source, false).Equals(Source, 0.0001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationFlexibleLinkDocumentTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.Symmetry.FlexibleDocument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationFlexibleLinkDocumentTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	auto& Document = Fixture.Document;
	Fixture.Catalog->Buildings[0].bCalibrated = true;
	const FGuid First = Document.GetEntryId();
	FText Error;
	Document.SetTransform(FTransform(FRotator(10, 20, 30), FVector(100, 200, 300), FVector(2, 3, 4)), Error);
	FDeepLevelCityBuildingDecorationEntry Light;
	Light.Name = TEXT("DifferentLight");
	Light.Output = EDeepLevelCityDecorationOutput::Actor;
	Light.ActorClass = APointLight::StaticClass();
	Light.LocalTransform = FTransform(FRotator(40, 50, 60), FVector(-150, -250, 350), FVector(1, 2, 3));
	Document.AddEntries(MakeArrayView(&Light, 1));
	const FGuid Second = Document.GetEntryId();
	const FTransform BeforeFirst = Document.GetEntry(First)->LocalTransform;
	Document.SelectEntry(First);
	TestFalse(TEXT("A link cannot target itself"), Document.CanCreateSymmetry(First));
	TestTrue(TEXT("Different existing output types can be linked"), Document.CreateSymmetry(EAxis::Y, Second));
	TestEqual(TEXT("Link existing never duplicates an entry"), Document.GetVariant()->Entries.Num(), 2);
	TestTrue(TEXT("Link existing preserves source and target placement"), Document.GetEntry(First)->LocalTransform.Equals(BeforeFirst)
		&& Document.GetEntry(Second)->LocalTransform.Equals(Light.LocalTransform));
	TestEqual(TEXT("Target keeps its own actor asset"), Document.GetEntry(Second)->ActorClass, Light.ActorClass);
	Document.Undo();
	TestNull(TEXT("Link existing metadata is one undoable edit"), Document.GetSymmetryPair());
	Document.Redo();
	auto Settings = *Document.GetSymmetryPair();
	Settings.bSyncRotation = Settings.bSyncScale = false;
	Settings.LocationOffset = FVector(10, 20, 30);
	TestTrue(TEXT("Individual transform components are configurable"), Document.SetSymmetrySettings(Settings, Error));
	const FTransform TargetBefore = Document.GetEntry(Second)->LocalTransform;
	FTransform Moved = BeforeFirst;
	Moved.SetLocation(FVector(400, 500, 600));
	Moved.SetRotation(FRotator(15, 70, 20).Quaternion());
	Moved.SetScale3D(FVector(5, 6, 7));
	Document.ApplyDragTransform(Moved);
	Document.EndDrag();
	TestTrue(TEXT("Location-only link retains target rotation and scale"), Document.GetEntry(Second)->LocalTransform.GetRotation().Equals(TargetBefore.GetRotation())
		&& Document.GetEntry(Second)->LocalTransform.GetScale3D().Equals(TargetBefore.GetScale3D()));
	TestTrue(TEXT("Location-only relation validates despite independent rotation and scale"), Document.GetVariant()->HasValidSymmetry());
	Document.SelectEntry(Second);
	const FTransform FirstBeforeReverse = Document.GetEntry(First)->LocalTransform;
	const FTransform SecondBeforeReverse = Document.GetEntry(Second)->LocalTransform;
	FTransform Reverse = SecondBeforeReverse;
	Reverse.AddToTranslation(FVector(20, 30, 40));
	TestTrue(TEXT("Target can drive linked components back to source"), Document.SetTransform(Reverse, Error));
	TestTrue(TEXT("Reverse edits preserve independent source rotation and scale"), Document.GetEntry(First)->LocalTransform.GetRotation().Equals(FirstBeforeReverse.GetRotation())
		&& Document.GetEntry(First)->LocalTransform.GetScale3D().Equals(FirstBeforeReverse.GetScale3D()));
	Document.Undo();
	TestTrue(TEXT("One undo restores both endpoints after offset reverse edit"), Document.GetEntry(First)->LocalTransform.Equals(FirstBeforeReverse)
		&& Document.GetEntry(Second)->LocalTransform.Equals(SecondBeforeReverse));
	Settings = *Document.GetSymmetryPair();
	Settings.bSyncLocation = false;
	Settings.bSyncRotation = true;
	Settings.RotationMode = EDeepLevelCityDecorationRotationLink::Copy;
	Settings.RotationOffset = FRotator(10, 0, 0);
	TestTrue(TEXT("Copy rotation with extra target tilt is configurable from either side"), Document.SetSymmetrySettings(Settings, Error));
	TestTrue(TEXT("Target has source rotation plus target-local tilt"), Document.GetEntry(Second)->LocalTransform.GetRotation().Equals(
		Document.GetEntry(First)->LocalTransform.GetRotation() * Settings.RotationOffset.Quaternion(), 0.0001));
	const auto ValidSettings = *Document.GetSymmetryPair();
	Settings.ScaleMultiplier.X = 0;
	TestFalse(TEXT("Noninvertible scale settings are rejected before mutation"), Document.SetSymmetrySettings(Settings, Error));
	TestEqual(TEXT("Rejected settings preserve the previous multiplier"), Document.GetSymmetryPair()->ScaleMultiplier, ValidSettings.ScaleMultiplier);
	Document.SelectEntry(First);
	Document.DuplicateVariant();
	TestTrue(TEXT("Variant duplication retains masks, modes and offsets with valid remapped endpoints"), Document.GetVariant()->HasValidSymmetry());
	TestEqual(TEXT("Copied link retains copy mode"), Document.GetVariant()->SymmetryPairs[0].RotationMode, EDeepLevelCityDecorationRotationLink::Copy);
	TestEqual(TEXT("Copied link retains target-local tilt"), Document.GetVariant()->SymmetryPairs[0].RotationOffset, FRotator(10, 0, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationFlexibleLinkInspectorTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.Symmetry.FlexibleInspector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationFlexibleLinkInspectorTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	Fixture.Set->SetFlags(RF_Transient);
	Fixture.Profile->SetFlags(RF_Transient);
	Fixture.Catalog->Buildings[0].bCalibrated = true;
	const auto Toolkit = MakeShared<FDeepLevelCityBuildingDecorationEditor>();
	Toolkit->InitEditor(EToolkitMode::Standalone, {}, Fixture.Set.Get());
	Toolkit->Document->SelectVariant(Fixture.Profile->Variants[0].VariantGuid);
	Toolkit->SelectEntry(Fixture.Profile->Variants[0].Entries[0].EntryGuid);
	TestFalse(TEXT("Unlinked decorations have no link settings inspector"), Toolkit->EntryProxy->bHasSymmetry);
	Toolkit->Document->CreateSymmetry(EAxis::Y);
	const auto Pair = *Toolkit->Document->GetSymmetryPair();
	TestTrue(TEXT("Linked decoration exposes the single settings inspector"), Toolkit->EntryProxy->bHasSymmetry);
	Toolkit->SelectEntry(Pair.Second);
	TestEqual(TEXT("Selecting target retains original source identity in inspector"), Toolkit->EntryProxy->Source, Toolkit->Document->GetEntry(Pair.First)->Name);
	AActor* FirstActor = Toolkit->Preview->GetEntryActor(Pair.First);
	AActor* SecondActor = Toolkit->Preview->GetEntryActor(Pair.Second);
	Toolkit->EntryProxy->Symmetry.bSyncLocation = false;
	Toolkit->EntryProxy->Symmetry.RotationMode = EDeepLevelCityDecorationRotationLink::Copy;
	Toolkit->EntryProxy->Symmetry.RotationOffset = FRotator(15, 0, 0);
	FPropertyChangedEvent Event(FindFProperty<FProperty>(UDeepLevelCityDecorationEntryProxy::StaticClass(), GET_MEMBER_NAME_CHECKED(UDeepLevelCityDecorationEntryProxy, Symmetry)));
	Toolkit->CommitInspector(Event);
	TestTrue(TEXT("Native inspector settings commit through document"), !Toolkit->Document->GetSymmetryPair()->bSyncLocation);
	TestTrue(TEXT("Settings edit preserves both preview actors"), Toolkit->Preview->GetEntryActor(Pair.First) == FirstActor && Toolkit->Preview->GetEntryActor(Pair.Second) == SecondActor);
	TestTrue(TEXT("Target preview receives copied rotation and offset"), SecondActor->GetActorQuat().Equals(FirstActor->GetActorQuat() * FRotator(15, 0, 0).Quaternion(), 0.0001));
	Toolkit->Document->Undo();
	TestTrue(TEXT("Undo restores settings inspector and baked transforms"), Toolkit->EntryProxy->Symmetry.bSyncLocation && Toolkit->Document->GetVariant()->HasValidSymmetry());
	Toolkit->CloseWindow(EAssetEditorCloseReason::AssetForceDeleted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationMultiSelectionDocumentTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.MultiSelection.Document",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationMultiSelectionDocumentTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	auto& Document = Fixture.Document;
	FText Error;
	const FGuid First = Document.GetEntryId();
	Document.SetTransform(FTransform(FVector(-100, 0, 50)), Error);
	auto Extra = *Document.GetEntry();
	Extra.LocalTransform.SetLocation(FVector(100, 0, 50));
	Document.AddEntries(MakeArrayView(&Extra, 1));
	const FGuid Second = Document.GetEntryId();
	const TArray<FGuid> Ids{First, Second};
	Document.SelectEntries(Ids);
	TestEqual(TEXT("Selection retains both decorations"), Document.GetSelectedEntryIds().Num(), 2);
	TestEqual(TEXT("Group gizmo uses their centroid"), Document.GetSelectionTransform().GetLocation(), FVector(0, 0, 50));
	Document.ApplySelectionDelta(FVector(30, 20, 10), FRotator(0, 90, 0), FVector(1));
	Document.EndDrag();
	TestTrue(TEXT("Group rotation and scale orbit the shared center"), Document.GetEntry(First)->LocalTransform.GetLocation().Equals(FVector(30, -180, 60), 0.001));
	TestTrue(TEXT("Both entries receive the same group transform"), Document.GetEntry(Second)->LocalTransform.GetLocation().Equals(FVector(30, 220, 60), 0.001));
	Document.Undo();
	TestEqual(TEXT("One undo restores the entire selection"), Document.GetEntry(First)->LocalTransform.GetLocation(), FVector(-100, 0, 50));
	TestEqual(TEXT("Undo also restores its second member"), Document.GetEntry(Second)->LocalTransform.GetLocation(), FVector(100, 0, 50));
	Document.Redo();
	TestTrue(TEXT("Redo restores both moved members"), Document.GetEntry(First)->LocalTransform.GetLocation().Equals(FVector(30, -180, 60), 0.001));
	Document.CopyEntry();
	Document.PasteEntry();
	TestEqual(TEXT("Copy/paste includes every selected decoration"), Document.GetVariant()->Entries.Num(), 4);
	TestEqual(TEXT("Pasted decorations become the complete selection"), Document.GetSelectedEntryIds().Num(), 2);
	Document.RemoveEntry();
	TestEqual(TEXT("Delete removes the entire selected group"), Document.GetVariant()->Entries.Num(), 2);
	Document.Undo();
	TestEqual(TEXT("One undo restores every deleted decoration"), Document.GetVariant()->Entries.Num(), 4);
	Document.SelectEntries(Ids);
	Document.SelectVariant(Document.GetVariantId());
	TestTrue(TEXT("Changing selection to a variant clears group state"), Document.GetSelectedEntryIds().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationSymmetryPivotTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.Symmetry.Pivot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationSymmetryPivotTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	auto& Document = Fixture.Document;
	Fixture.Catalog->Buildings[0].bCalibrated = true;
	FText Error;
	Document.SetTransform(FTransform(FRotator(5, 25, 0), FVector(100, 200, 80), FVector(2, 3, 1)), Error);
	const FGuid First = Document.GetEntryId();
	Document.CreateSymmetry(EAxis::Y);
	auto Settings = *Document.GetSymmetryPair();
	Settings.LocationOffset = FVector(15, 20, 30);
	Settings.RotationOffset = FRotator(0, 15, 0);
	Settings.ScaleMultiplier = FVector(1, 2, 1);
	Document.SetSymmetrySettings(Settings, Error);
	const FGuid Second = Settings.Second;
	const FTransform BeforeFirst = Document.GetEntry(First)->LocalTransform;
	const FTransform BeforeSecond = Document.GetEntry(Second)->LocalTransform;
	Document.SelectSymmetryPivot(First);
	TestTrue(TEXT("Symmetry center is a distinct transform target"), Document.IsSymmetryPivotSelected());
	TestEqual(TEXT("Offset pairs use their actual midpoint, never the building origin"), Document.GetSelectionTransform().GetLocation(),
		(BeforeFirst.GetLocation() + BeforeSecond.GetLocation()) * 0.5);
	const FVector Shift(60, -40, 25);
	Document.ApplySelectionDelta(Shift, FRotator::ZeroRotator, FVector::ZeroVector);
	Document.EndDrag();
	TestEqual(TEXT("Center movement is persisted on the existing pair"), Document.GetSymmetryPair()->PlaneOrigin, Settings.PlaneOrigin + Shift);
	TestTrue(TEXT("Moving center carries the source"), Document.GetEntry(First)->LocalTransform.GetLocation().Equals(BeforeFirst.GetLocation() + Shift));
	TestTrue(TEXT("Moving center carries the offset target"), Document.GetEntry(Second)->LocalTransform.GetLocation().Equals(BeforeSecond.GetLocation() + Shift));
	TestTrue(TEXT("Moving center preserves asymmetric placement offsets"), Document.GetSymmetryPair()->LocationOffset.Equals(Settings.LocationOffset, 0.001));
	Document.Undo();
	TestEqual(TEXT("One undo restores the center"), Document.GetSymmetryPair()->PlaneOrigin, Settings.PlaneOrigin);
	TestTrue(TEXT("One undo restores both endpoints"), Document.GetEntry(First)->LocalTransform.Equals(BeforeFirst) && Document.GetEntry(Second)->LocalTransform.Equals(BeforeSecond));
	Document.Redo();
	const FVector Center = Document.GetSelectionTransform().GetLocation();
	Document.ApplySelectionDelta(FVector::ZeroVector, FRotator(0, 45, 0), FVector(0.5, 0, 0));
	Document.EndDrag();
	TestTrue(TEXT("Rotating/scaling keeps the actual endpoint midpoint fixed"), Document.GetSelectionTransform().GetLocation().Equals(Center, 0.001));
	TestTrue(TEXT("Rotated and scaled offset pair remains valid"), Document.GetVariant()->HasValidSymmetry());
	Document.SelectEntry(Second);
	auto Desired = Document.GetEntry()->LocalTransform;
	Desired.AddToTranslation(FVector(10, 20, 30));
	TestTrue(TEXT("Individual endpoint remains editable after moving the center"), Document.SetTransform(Desired, Error));
	TestTrue(TEXT("Reverse edits still synchronize through the adjusted plane"), Document.GetVariant()->HasValidSymmetry());
	const TArray<FGuid> Ids{First, Second};
	Document.SelectEntries(Ids);
	Document.ApplySelectionDelta(FVector(30, 50, 10), FRotator(0, -20, 0), FVector::ZeroVector);
	Document.EndDrag();
	TestTrue(TEXT("Selecting both endpoints does not apply the link twice"), Document.GetVariant()->HasValidSymmetry());
	Document.DuplicateEntry();
	TestEqual(TEXT("Group duplication preserves complete pairs"), Document.GetVariant()->SymmetryPairs.Num(), 2);
	TestTrue(TEXT("Duplicated pair identities and transforms remain valid"), Document.GetVariant()->HasValidSymmetry());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationMultiSelectionPreviewTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.MultiSelection.Preview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationMultiSelectionPreviewTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	Fixture.Set->SetFlags(RF_Transient);
	Fixture.Profile->SetFlags(RF_Transient);
	Fixture.Catalog->Buildings[0].bCalibrated = true;
	auto Toolkit = MakeShared<FDeepLevelCityBuildingDecorationEditor>();
	Toolkit->InitEditor(EToolkitMode::Standalone, {}, Fixture.Set.Get());
	Toolkit->Document->SelectVariant(Fixture.Profile->Variants[0].VariantGuid);
	const FGuid First = Fixture.Profile->Variants[0].Entries[0].EntryGuid;
	Toolkit->SelectEntry(First);
	Toolkit->Document->CreateSymmetry(EAxis::Y);
	const FGuid Second = Toolkit->GetSelectedSymmetryPair()->Second;
	Toolkit->SelectEntry(Second, true);
	TestEqual(TEXT("Ctrl selection retains two decorations"), Toolkit->GetSelectedEntryIds().Num(), 2);
	TestEqual(TEXT("Outline reflects both viewport selections"), Toolkit->Outline->GetSelectedItems().Num(), 2);
	TestTrue(TEXT("Group inspector owns the displayed transform"), Toolkit->Inspector->GetSelectedObjects()[0].Get() == Toolkit->SelectionProxy);
	Toolkit->ApplyWidgetDelta(FVector(40, 20, 10), FRotator::ZeroRotator, FVector::ZeroVector);
	Toolkit->EndWidgetDrag();
	for (const FGuid& Id : Toolkit->GetSelectedEntryIds())
	{
		TestTrue(TEXT("Both preview actors follow the document"), Toolkit->Preview->GetEntryActor(Id)->GetActorTransform().Equals(Toolkit->GetDecoration(Id)->LocalTransform));
	}
	Toolkit->SelectEntry(Second, true);
	TestEqual(TEXT("Ctrl toggles a selected decoration off"), Toolkit->GetSelectedEntryIds().Num(), 1);
	const auto EntryRow = Toolkit->OutlineItems[0]->Children[0]->Children.FindByPredicate([&](const auto& Row) { return Row->Entry == Second; });
	if (TestNotNull(TEXT("Second decoration has an outline row"), EntryRow))
	{
		Toolkit->Outline->SetItemSelection(*EntryRow, true, ESelectInfo::OnMouseClick);
		TestEqual(TEXT("Outline multi-selection updates document selection"), Toolkit->GetSelectedEntryIds().Num(), 2);
	}
	Toolkit->SelectSymmetryPivot(First);
	TestTrue(TEXT("Symmetry center selects both linked decorations"), Toolkit->GetSelectedEntryIds().Contains(First) && Toolkit->GetSelectedEntryIds().Contains(Second));
	TestTrue(TEXT("Symmetry point selects the center"), Toolkit->IsSymmetryPivotSelected());
	TestEqual(TEXT("Gizmo moves onto the persisted center"), Toolkit->Preview->GetClient()->GetWidgetLocation(), Toolkit->GetSelectedSymmetryPair()->PlaneOrigin);
	Toolkit->ApplyWidgetDelta(FVector(90, 0, 0), FRotator::ZeroRotator, FVector::ZeroVector);
	Toolkit->EndWidgetDrag();
	TestTrue(TEXT("Center drag updates the linked preview atomically"), Toolkit->Preview->GetEntryActor(Second)->GetActorTransform().Equals(Toolkit->GetDecoration(Second)->LocalTransform));
	TestEqual(TEXT("Center inspector counts both linked decorations"), Toolkit->SelectionProxy->DecorationCount, 2);
	Toolkit->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationSymmetryBatchTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.Symmetry.BatchPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationSymmetryBatchTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	auto& Document = Fixture.Document;
	FText Error;
	Document.SetTransform(FTransform(FRotator(0, 25, 0), FVector(100, 200, 80)), Error);
	const FGuid First = Document.GetEntryId();
	auto Extra = *Document.GetEntry();
	Extra.LocalTransform.SetLocation(FVector(100, 200, 180));
	Document.AddEntries(MakeArrayView(&Extra, 1));
	const FGuid Second = Document.GetEntryId();
	const TArray<FGuid> Sources{First, Second};
	Document.SelectEntries(Sources);
	const FVector Center(100, 350, 130), Normal(0, 1, 0);
	const auto Before = *Document.GetVariant();
	const bool bDirty = Fixture.Profile->GetOutermost()->IsDirty();
	TArray<FDeepLevelCityBuildingDecorationEntry> Copies;
	TestTrue(TEXT("Explicit placement works without a guessed calibrated center"), Document.BuildSymmetryPreview(Center, Normal, Copies, Error));
	TestEqual(TEXT("Every selected source has a preview copy"), Copies.Num(), 2);
	TestTrue(TEXT("Preview does not mutate authored entries or links"), FDeepLevelCityBuildingDecorationVariant::StaticStruct()->CompareScriptStruct(&Before, Document.GetVariant(), 0));
	TestEqual(TEXT("Preview preserves package dirty state"), Fixture.Profile->GetOutermost()->IsDirty(), bDirty);
	TestEqual(TEXT("Preview reflects across the supplied plane"), Copies[0].LocalTransform.GetLocation(), FVector(100, 500, 80));
	TestTrue(TEXT("Confirm creates the whole mirror group"), Document.CreateSymmetryAt(Center, Normal, Error));
	TestEqual(TEXT("Group confirmation adds two copies"), Document.GetVariant()->Entries.Num(), 4);
	TestEqual(TEXT("Group confirmation adds two disjoint links"), Document.GetVariant()->SymmetryPairs.Num(), 2);
	TestTrue(TEXT("Batch links are valid"), Document.GetVariant()->HasValidSymmetry());
	FVector AverageCenter = FVector::ZeroVector;
	for (const auto& Pair : Document.GetVariant()->SymmetryPairs) { AverageCenter += Document.GetSymmetryCenter(Pair); }
	TestEqual(TEXT("The chosen center is the group's symmetry center"), AverageCenter / 2.0, Center);
	Document.Undo();
	TestEqual(TEXT("One undo removes the whole batch"), Document.GetVariant()->Entries.Num(), 2);
	TestTrue(TEXT("Undo removes all batch metadata"), Document.GetVariant()->SymmetryPairs.IsEmpty());
	Document.Redo();
	TestTrue(TEXT("Redo restores every valid pair"), Document.GetVariant()->HasValidSymmetry() && Document.GetVariant()->SymmetryPairs.Num() == 2);
	Document.UnlinkSymmetry();
	TestTrue(TEXT("Unlink selection handles all selected pairs"), Document.GetVariant()->SymmetryPairs.IsEmpty());
	TestEqual(TEXT("Unlink preserves all objects"), Document.GetVariant()->Entries.Num(), 4);
	Document.SelectEntries(Sources);
	const auto FirstPose = Document.GetEntry(First)->LocalTransform;
	const auto SecondPose = Document.GetEntry(Second)->LocalTransform;
	TestTrue(TEXT("Two existing selected objects can be linked directly"), Document.LinkSelectedPair());
	TestTrue(TEXT("Linking existing selection preserves both poses"), Document.GetEntry(First)->LocalTransform.Equals(FirstPose) && Document.GetEntry(Second)->LocalTransform.Equals(SecondPose));
	TestEqual(TEXT("Existing pair center is its endpoint midpoint"), Document.GetSelectionTransform().GetLocation(), (FirstPose.GetLocation() + SecondPose.GetLocation()) * 0.5);
	TestTrue(TEXT("Selected existing pair is valid"), Document.GetVariant()->HasValidSymmetry());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationSymmetryPlacementTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.Symmetry.LivePlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationSymmetryPlacementTest::RunTest(const FString&)
{
	auto* Settings = GetMutableDefault<ULevelEditorViewportSettings>();
	const bool bWasGridEnabled = Settings->GridEnabled;
	ON_SCOPE_EXIT { Settings->GridEnabled = bWasGridEnabled; };
	Settings->GridEnabled = false;
	FDecorationFixture Fixture;
	Fixture.Set->SetFlags(RF_Transient);
	Fixture.Profile->SetFlags(RF_Transient);
	auto Toolkit = MakeShared<FDeepLevelCityBuildingDecorationEditor>();
	Toolkit->InitEditor(EToolkitMode::Standalone, {}, Fixture.Set.Get());
	Toolkit->Document->SelectVariant(Fixture.Profile->Variants[0].VariantGuid);
	const FGuid Source = Fixture.Profile->Variants[0].Entries[0].EntryGuid;
	Toolkit->SelectEntry(Source);
	FText Error;
	Toolkit->Document->SetTransform(FTransform(FVector(100, 200, 80)), Error);
	const FVector Center(100, 400, 80);
	const bool bDirty = Fixture.Profile->GetOutermost()->IsDirty();
	Toolkit->BeginSymmetryPlacement();
	TestTrue(TEXT("Auto mirror enters a placement mode"), Toolkit->IsPlacingSymmetry());
	TestTrue(TEXT("Mouse placement builds a ghost before confirmation"), Toolkit->UpdateSymmetryPlacement(Center));
	TestEqual(TEXT("Pending placement has a ghost actor"), Toolkit->Preview->GetSymmetryPreviewCount(), 1);
	TestEqual(TEXT("Auto plane is inferred from cursor and selection"), Toolkit->GetSymmetryPlacementNormal(), FVector::RightVector);
	AActor* Ghost = Toolkit->Preview->GetSymmetryPreviewActor(Source);
	if (TestNotNull(TEXT("Ghost exists in the preview scene"), Ghost))
	{
		TestEqual(TEXT("Ghost is at the reflected location"), Ghost->GetActorLocation(), FVector(100, 600, 80));
		TestTrue(TEXT("Ghost is transient"), Ghost->HasAnyFlags(RF_Transient));
		TestFalse(TEXT("Ghost has no actor collision"), Ghost->GetActorEnableCollision());
		TArray<UStaticMeshComponent*> Components;
		Ghost->GetComponents(Components, true);
		TestTrue(TEXT("Ghost retains its rendered geometry"), !Components.IsEmpty());
		if (!Components.IsEmpty()) { TestTrue(TEXT("Ghost uses translucent preview material"), Components[0]->GetMaterial(0)->GetBlendMode() == BLEND_Translucent); }
	}
	TestEqual(TEXT("Pending ghosts are not authored entries"), Toolkit->Document->GetVariant()->Entries.Num(), 1);
	TestEqual(TEXT("Pending placement does not dirty the profile"), Fixture.Profile->GetOutermost()->IsDirty(), bDirty);
	TestTrue(TEXT("Placement hides the editing gizmo"), Toolkit->Preview->GetClient()->GetWidgetMode() == UE::Widget::WM_None);
	Toolkit->CancelSymmetryPlacement();
	TestEqual(TEXT("Cancel destroys all ghosts"), Toolkit->Preview->GetSymmetryPreviewCount(), 0);
	TestEqual(TEXT("Cancel preserves authored objects"), Toolkit->Document->GetVariant()->Entries.Num(), 1);
	Toolkit->BeginSymmetryPlacement();
	Toolkit->UpdateSymmetryPlacement(Center);
	TestTrue(TEXT("Click confirmation creates the mirror"), Toolkit->CommitSymmetryPlacement());
	TestFalse(TEXT("Confirm leaves placement mode"), Toolkit->IsPlacingSymmetry());
	TestEqual(TEXT("Confirm destroys temporary ghosts"), Toolkit->Preview->GetSymmetryPreviewCount(), 0);
	TestEqual(TEXT("Confirmed placement has its baked mirror"), Toolkit->Document->GetVariant()->Entries.Num(), 2);
	TestEqual(TEXT("Confirmed pivot is exactly between its objects"), Toolkit->Preview->GetClient()->GetWidgetLocation(), Center);
	Toolkit->Document->Undo();
	TestEqual(TEXT("One undo removes the confirmed mirror"), Toolkit->Document->GetVariant()->Entries.Num(), 1);
	Toolkit->SelectEntry(Source);
	Toolkit->BeginSymmetryPlacement();
	Toolkit->UpdateSymmetryPlacement(Center);
	Toolkit->SelectEntry({});
	TestFalse(TEXT("Selection change exits placement"), Toolkit->IsPlacingSymmetry());
	TestEqual(TEXT("Selection change clears ghost actors"), Toolkit->Preview->GetSymmetryPreviewCount(), 0);
	Toolkit->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationGroupScaleSpaceTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.MultiSelection.LocalScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationGroupScaleSpaceTest::RunTest(const FString&)
{
	FDecorationFixture Fixture;
	auto& Document = Fixture.Document;
	const FGuid First = Document.GetEntryId();
	FText Error;
	Document.SetTransform(FTransform(FVector(0, -100, 50)), Error);
	auto Extra = *Document.GetEntry();
	Extra.LocalTransform.SetLocation(FVector(0, 100, 50));
	Document.AddEntries(MakeArrayView(&Extra, 1));
	const TArray<FGuid> Ids{First, Document.GetEntryId()};
	Document.SelectEntries(Ids);
	Document.ApplySelectionDelta(FVector::ZeroVector, FRotator::ZeroRotator, FVector(1, 0, 0), FRotator(0, 90, 0).Quaternion());
	Document.EndDrag();
	TestTrue(TEXT("Local-X scaling expands along the rotated group axis"), Document.GetEntry(First)->LocalTransform.GetLocation().Equals(FVector(0, -200, 50), 0.001));
	TestTrue(TEXT("The opposite member scales about the same centroid"), Document.GetEntry(Ids[1])->LocalTransform.GetLocation().Equals(FVector(0, 200, 50), 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityDecorationSymmetrySnappingTest,
	"DeepLevelDesignPCG.Editor.City.DecorationEditor.Symmetry.AxisAndGridSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityDecorationSymmetrySnappingTest::RunTest(const FString&)
{
	auto* Settings = GetMutableDefault<ULevelEditorViewportSettings>();
	const bool bOldGrid = Settings->GridEnabled;
	TGuardValue<bool> SnapActive(Settings->bSnappingActive, true);
	TGuardValue<bool> DecimalSnap(Settings->bUsePowerOf2SnapSize, false);
	TGuardValue<int32> GridIndex(Settings->CurrentPosGridSize, 0);
	TGuardValue<TArray<float>> GridSizes(Settings->DecimalGridSizes, {50.0f, 100.0f});
	ON_SCOPE_EXIT { Settings->GridEnabled = bOldGrid; };
	Settings->GridEnabled = true;
	FDecorationFixture Fixture;
	Fixture.Set->SetFlags(RF_Transient);
	Fixture.Profile->SetFlags(RF_Transient);
	auto Toolkit = MakeShared<FDeepLevelCityBuildingDecorationEditor>();
	Toolkit->InitEditor(EToolkitMode::Standalone, {}, Fixture.Set.Get());
	Toolkit->Document->SelectVariant(Fixture.Profile->Variants[0].VariantGuid);
	const FGuid Source = Fixture.Profile->Variants[0].Entries[0].EntryGuid;
	Toolkit->SelectEntry(Source);
	const FVector Origin(13, 17, 5);
	FText Error;
	Toolkit->Document->SetTransform(FTransform(Origin), Error);
	const auto Client = Toolkit->Preview->GetClient();
	Client->SetWidgetCoordSystemSpace(COORD_World);
	FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(Client->Viewport, Client->GetScene(), Client->EngineShowFlags));
	FSceneViewInitOptions Options;
	Options.ViewFamily = &Family;
	Options.SetViewRectangle(FIntRect(0, 0, 1000, 1000));
	Options.ViewRotationMatrix = FMatrix::Identity;
	Options.ProjectionMatrix = FMatrix::Identity;
	Options.ProjectionMatrix.M[0][0] = 0.002f;
	Options.ProjectionMatrix.M[1][1] = 0.002f;
	FSceneView View(Options);
	Toolkit->BeginSymmetryPlacement();
	TestTrue(TEXT("Cursor near X can preview"), Toolkit->UpdateSymmetryPlacement(Origin + FVector(173, 8, 0), &View));
	TestTrue(TEXT("A nearby axis is acquired automatically"), Toolkit->GetSymmetrySnappedAxis() == EAxis::X);
	TestTrue(TEXT("Axis attraction also removes off-axis movement"), Toolkit->GetSymmetryPlacementCenter().Equals(Origin + FVector(150, 0, 0), 0.001));
	TestTrue(TEXT("HUD reports the active global grid step"), Toolkit->GetViewportStatus().ToString().Contains(FText::AsNumber(50).ToString()));
	Toolkit->UpdateSymmetryPlacement(Origin + FVector(173, 18, 0), &View);
	TestTrue(TEXT("Release hysteresis keeps an acquired guide stable"), Toolkit->GetSymmetrySnappedAxis() == EAxis::X);
	Toolkit->UpdateSymmetryPlacement(Origin + FVector(173, 37, 0), &View);
	TestTrue(TEXT("Moving away releases axis attraction"), Toolkit->GetSymmetrySnappedAxis() == EAxis::None);
	TestTrue(TEXT("Free movement still follows the grid"), Toolkit->GetSymmetryPlacementCenter().Equals(Origin + FVector(150, 50, 0), 0.001));
	Settings->CurrentPosGridSize = 1;
	Toolkit->UpdateSymmetryPlacement(Origin + FVector(173, 8, 0), &View);
	TestTrue(TEXT("Changing the shared toolbar step changes placement immediately"), Toolkit->GetSymmetryPlacementCenter().Equals(Origin + FVector(200, 0, 0), 0.001));
	Settings->GridEnabled = false;
	Toolkit->UpdateSymmetryPlacement(Origin + FVector(173, 8, 0), &View);
	TestTrue(TEXT("Disabling grid leaves axis attraction available"), Toolkit->GetSymmetryPlacementCenter().Equals(Origin + FVector(173, 0, 0), 0.001));
	Toolkit->UpdateSymmetryPlacement(Origin + FVector(173, 8, 0), &View, true);
	TestTrue(TEXT("Holding Shift bypasses axis attraction"), Toolkit->GetSymmetrySnappedAxis() == EAxis::None);
	TestTrue(TEXT("Free modifier retains the exact unsnapped cursor pose"), Toolkit->GetSymmetryPlacementCenter().Equals(Origin + FVector(173, 8, 0), 0.001));
	Toolkit->ToggleSymmetryPlacementAxis(EAxis::Z);
	Toolkit->UpdateSymmetryPlacement(Origin + FVector(173, 8, 90), &View);
	TestTrue(TEXT("Manual lock projects the center onto Z"), Toolkit->GetSymmetryPlacementCenter().Equals(Origin + FVector(0, 0, 90), 0.001));
	Toolkit->ToggleSymmetryPlacementAxis(EAxis::Z);
	Toolkit->UpdateSymmetryPlacement(Origin + FVector(173, 37, 0), &View);
	TestTrue(TEXT("Repeating the axis key restores automatic/free placement"), Toolkit->GetSymmetrySnappedAxis() == EAxis::None);
	Toolkit->CancelSymmetryPlacement();
	const FQuat Basis = FRotator(0, 45, 0).Quaternion();
	Toolkit->Document->SetTransform(FTransform(Basis, Origin), Error);
	Client->SetWidgetCoordSystemSpace(COORD_Local);
	Settings->GridEnabled = true;
	Settings->CurrentPosGridSize = 0;
	Toolkit->BeginSymmetryPlacement();
	Toolkit->UpdateSymmetryPlacement(Origin + Basis.RotateVector(FVector(173, 8, 0)), &View);
	const FVector LocalCenter = Origin + Basis.RotateVector(FVector(150, 0, 0));
	TestTrue(TEXT("Local guide attraction respects the gizmo basis"), Toolkit->GetSymmetrySnappedAxis() == EAxis::X);
	TestTrue(TEXT("Grid steps follow the rotated local guide"), Toolkit->GetSymmetryPlacementCenter().Equals(LocalCenter, 0.001));
	TestTrue(TEXT("Confirmation keeps the snapped preview"), Toolkit->CommitSymmetryPlacement());
	TestTrue(TEXT("Confirmed pair midpoint matches its snapped center"), Toolkit->GetSelectionTransform().GetLocation().Equals(LocalCenter, 0.001));
	Toolkit->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);
	return true;
}

#endif
