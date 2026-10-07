// Copyright <--\, Inc. All Rights Reserved.
#include "City/Decoration/DeepLevelCityBuildingDecorationEditor.h"
#include "City/Decoration/DeepLevelCityDecorationAuthoring.h"
#include "AssetRegistry/AssetData.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "EditorViewportCommands.h"
#include "Factories/DataAssetFactory.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "IDetailsView.h"
#include "Misc/PackageName.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "PropertyEditorModule.h"
#include "SAdvancedPreviewDetailsTab.h"
#include "Widgets/Docking/SDockTab.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DeepLevelCityBuildingDecorationEditor)
#define LOCTEXT_NAMESPACE "DeepLevelCityBuildingDecorationEditor"

const FName FDeepLevelCityBuildingDecorationEditor::WorkspaceTab(TEXT("DeepLevelCityBuildingDecorationWorkspace"));
const FName FDeepLevelCityBuildingDecorationEditor::EnvironmentTab(TEXT("DeepLevelCityBuildingDecorationEnvironment"));

FText FDeepLevelCityDecorationSetAssetActions::GetName() const { return LOCTEXT("SetAsset", "DeepLevel City Decoration Set"); }
UClass* FDeepLevelCityDecorationSetAssetActions::GetSupportedClass() const { return UDeepLevelCityDecorationSet::StaticClass(); }

void FDeepLevelCityDecorationSetAssetActions::OpenAssetEditor(const TArray<UObject*>& Objects, TSharedPtr<IToolkitHost> Host)
{
	for (UObject* Object : Objects)
	{
		if (auto* Set = Cast<UDeepLevelCityDecorationSet>(Object))
		{
			MakeShared<FDeepLevelCityBuildingDecorationEditor>()->InitEditor(Host ? EToolkitMode::WorldCentric : EToolkitMode::Standalone, Host, Set);
		}
	}
}

FDeepLevelCityBuildingDecorationEditor::~FDeepLevelCityBuildingDecorationEditor()
{
	if (Document) { Document->Shutdown(); }
}

void FDeepLevelCityBuildingDecorationEditor::InitEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host, UDeepLevelCityDecorationSet* Set)
{
	check(Set);
	Document = MakeUnique<FDeepLevelCityDecorationDocument>();
	Document->Open(Set);
	VariantProxy = NewObject<UDeepLevelCityDecorationVariantProxy>(GetTransientPackage(), NAME_None, RF_Transient);
	EntryProxy = NewObject<UDeepLevelCityDecorationEntryProxy>(GetTransientPackage(), NAME_None, RF_Transient);
	BuildingProxy = NewObject<UDeepLevelCityDecorationBuildingProxy>(GetTransientPackage(), NAME_None, RF_Transient);
	SetProxy = NewObject<UDeepLevelCityDecorationSetProxy>(GetTransientPackage(), NAME_None, RF_Transient);
	FDetailsViewArgs Args;
	Args.bAllowSearch = true;
	Args.bHideSelectionTip = true;
	Args.bAllowMultipleTopLevelObjects = false;
	Inspector = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(Args);
	Inspector->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& Property)
	{
		return Property.Property.GetFName() != GET_MEMBER_NAME_CHECKED(FDeepLevelCityBuildingDecorationEntry, EntryGuid);
	}));
	Inspector->SetIsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateSP(this, &FDeepLevelCityBuildingDecorationEditor::CanEditInspector));
	Inspector->OnFinishedChangingProperties().AddSP(this, &FDeepLevelCityBuildingDecorationEditor::CommitInspector);
	SAssignNew(Preview, SDeepLevelCityBuildingDecorationViewport).Editor(SharedThis(this));
	const auto Layout = FTabManager::NewLayout(TEXT("DeepLevelCityBuildingDecoration_v2"))
		->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
			->Split(FTabManager::NewStack()->SetSizeCoefficient(0.8f)->AddTab(WorkspaceTab, ETabState::OpenedTab)->SetHideTabWell(true))
			->Split(FTabManager::NewStack()->SetSizeCoefficient(0.2f)->AddTab(EnvironmentTab, ETabState::ClosedTab)));
	InitAssetEditor(Mode, Host, TEXT("DeepLevelCityBuildingDecorationEditor"), Layout, true, true, Set);
	Document->OnChanged.AddSP(this, &FDeepLevelCityBuildingDecorationEditor::DocumentChanged);
	DocumentChanged(EDeepLevelCityDecorationChange::Context, {});
	RegenerateMenusAndToolbars();
}

FText FDeepLevelCityBuildingDecorationEditor::GetBaseToolkitName() const { return LOCTEXT("Toolkit", "Building Decoration"); }

FText FDeepLevelCityBuildingDecorationEditor::GetToolkitName() const
{
	// The opened asset names the workspace; additional assets belong to its save scope.
	return GetLabelForObject(GetEditingObjects()[0]);
}

FText FDeepLevelCityBuildingDecorationEditor::GetToolkitToolTipText() const
{
	TArray<FText> AssetToolTips;
	for (const UObject* Asset : GetEditingObjects()) { AssetToolTips.Add(GetToolTipTextForObject(Asset)); }
	return FText::Join(FText::FromString(TEXT("\n")), AssetToolTips);
}

void FDeepLevelCityBuildingDecorationEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{
	FAssetEditorToolkit::RegisterTabSpawners(Manager);
	Manager->RegisterTabSpawner(WorkspaceTab, FOnSpawnTab::CreateSP(this, &FDeepLevelCityBuildingDecorationEditor::SpawnWorkspace))
		.SetDisplayName(LOCTEXT("Workspace", "Building Decoration"));
	Manager->RegisterTabSpawner(EnvironmentTab, FOnSpawnTab::CreateSP(this, &FDeepLevelCityBuildingDecorationEditor::SpawnEnvironment))
		.SetDisplayName(LOCTEXT("Environment", "Preview Environment"));
}

void FDeepLevelCityBuildingDecorationEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{
	Manager->UnregisterTabSpawner(WorkspaceTab);
	Manager->UnregisterTabSpawner(EnvironmentTab);
	FAssetEditorToolkit::UnregisterTabSpawners(Manager);
}

void FDeepLevelCityBuildingDecorationEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(VariantProxy);
	Collector.AddReferencedObject(EntryProxy);
	Collector.AddReferencedObject(BuildingProxy);
	Collector.AddReferencedObject(SetProxy);
}

TSharedRef<SDockTab> FDeepLevelCityBuildingDecorationEditor::SpawnWorkspace(const FSpawnTabArgs&)
{
	return SNew(SDockTab).TabRole(ETabRole::PanelTab)[BuildWorkspace()];
}

TSharedRef<SDockTab> FDeepLevelCityBuildingDecorationEditor::SpawnEnvironment(const FSpawnTabArgs&)
{
	return SNew(SDockTab).TabRole(ETabRole::PanelTab)[SNew(SAdvancedPreviewDetailsTab, Preview->GetPreviewScene())];
}

void FDeepLevelCityBuildingDecorationEditor::DocumentChanged(EDeepLevelCityDecorationChange Change, FGuid Entry)
{
	if (Document->IsDragging() && Change == EDeepLevelCityDecorationChange::Transform)
	{
		if (const auto* Changed = Document->GetEntry(Entry)) { Preview->UpdateEntry(Entry, Changed->LocalTransform); }
		return;
	}
	TGuardValue<bool> Guard(bRefreshing, true);
	OperationError = FText::GetEmpty();
	for (const UObject* Asset : { static_cast<const UObject*>(Document->GetSet()), static_cast<const UObject*>(Document->GetProfile()) })
	{
		if (Asset && !GetEditingObjects().Contains(Asset)) { AddEditingObject(const_cast<UObject*>(Asset)); }
	}
	if (Change == EDeepLevelCityDecorationChange::Context) { bSetSettings = false; }
	if (Change != EDeepLevelCityDecorationChange::Selection && Change != EDeepLevelCityDecorationChange::Transform) { RefreshOutline(); }
	RefreshInspector();
	SynchronizeOutlineSelection();
	if (Change == EDeepLevelCityDecorationChange::Transform)
	{
		if (const auto* Changed = Document->GetEntry(Entry)) { Preview->UpdateEntry(Entry, Changed->LocalTransform); }
	}
	else if (Change != EDeepLevelCityDecorationChange::Selection && Change != EDeepLevelCityDecorationChange::Metadata)
	{
		Preview->Synchronize(Document->GetBuildingClass(), Document->GetProfile(), Document->GetVariantId());
	}
	Preview->SetPlacementGuide(bShowPlacementGuide ? Document->GetBuildingDefinition() : nullptr);
	Preview->GetClient()->Invalidate();
	RefreshIssues();
}

void FDeepLevelCityBuildingDecorationEditor::RefreshInspector()
{
	UObject* Object = nullptr;
	if (bSetSettings && Document->GetSet())
	{
		SetProxy->Categories = Document->GetSet()->Categories;
		SetProxy->BuildingProfiles = Document->GetSet()->BuildingProfiles;
		Object = SetProxy;
	}
	else if (const auto* Entry = Document->GetEntry())
	{
		EntryProxy->Entry = InspectorBaseline = *Entry;
		const auto* Pair = Document->GetSymmetryPair();
		EntryProxy->bHasSymmetry = Pair != nullptr;
		EntryProxy->Symmetry = Pair ? *Pair : FDeepLevelCityDecorationSymmetryPair();
		EntryProxy->Source = Pair ? Document->GetEntry(Pair->First)->Name : NAME_None;
		EntryProxy->Target = Pair ? Document->GetEntry(Pair->Second)->Name : NAME_None;
		Object = EntryProxy;
	}
	else if (const auto* Variant = Document->GetVariant())
	{
		VariantProxy->Name = Variant->Name;
		VariantProxy->SelectionWeight = Variant->SelectionWeight;
		Object = VariantProxy;
	}
	else if (const auto* Definition = Document->GetBuildingDefinition())
	{
		BuildingProxy->BuildingClass = Definition->BuildingClass;
		BuildingProxy->bCalibrated = Definition->bCalibrated;
		BuildingProxy->Center = Definition->PlacementVolume.Center;
		BuildingProxy->Rotation = Definition->PlacementVolume.Rotation;
		BuildingProxy->Extent = Definition->PlacementVolume.Extent;
		Object = BuildingProxy;
	}
	Inspector->SetObject(Object, true);
}

void FDeepLevelCityBuildingDecorationEditor::CommitInspector(const FPropertyChangedEvent& Event)
{
	if (bRefreshing) { return; }
	FText Error;
	if (bSetSettings)
	{
		if (Event.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UDeepLevelCityDecorationSetProxy, Categories)) { Document->SetCategories(SetProxy->Categories); }
	}
	else if (Document->GetEntry())
	{
		const bool bLinkSettings = Event.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UDeepLevelCityDecorationEntryProxy, Symmetry);
		const bool bSuccess = bLinkSettings ? Document->SetSymmetrySettings(EntryProxy->Symmetry, Error)
			: Document->SetEntryFields(EntryProxy->Entry, InspectorBaseline, Error);
		if (!bSuccess)
		{
			OperationError = Error;
			RefreshInspector();
		}
	}
	else if (const auto* Variant = Document->GetVariant())
	{
		if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDeepLevelCityDecorationVariantProxy, Name))
		{
			Document->SetVariantFields(VariantProxy->Name, Variant->SelectionWeight);
		}
		else if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDeepLevelCityDecorationVariantProxy, SelectionWeight))
		{
			Document->SetVariantFields(Variant->Name, VariantProxy->SelectionWeight);
		}
	}
	RefreshIssues();
}

void FDeepLevelCityBuildingDecorationEditor::ChooseSet(const FAssetData& Asset)
{
	Document->Open(Cast<UDeepLevelCityDecorationSet>(Asset.GetAsset()), const_cast<UDeepLevelCityBuildingDecorationProfile*>(Document->GetProfile()));
}
void FDeepLevelCityBuildingDecorationEditor::ChooseCatalog(const FAssetData& Asset) { Document->SetCatalog(Cast<UDeepLevelBuildingPlacementCatalog>(Asset.GetAsset())); }
void FDeepLevelCityBuildingDecorationEditor::AttachProfile(const FAssetData& Asset)
{
	if (!Document->AttachProfile(Cast<UDeepLevelCityBuildingDecorationProfile>(Asset.GetAsset())))
	{
		OperationError = LOCTEXT("CannotAttach", "Profile must match the selected catalog building, with no existing assignment.");
		RefreshIssues();
	}
}

FReply FDeepLevelCityBuildingDecorationEditor::CreateProfile()
{
	if (!Document->CanCreateProfile()) { return FReply::Handled(); }
	auto* Factory = NewObject<UDataAssetFactory>();
	Factory->DataAssetClass = UDeepLevelCityBuildingDecorationProfile::StaticClass();
	const FString Directory = FPackageName::GetLongPackagePath(Document->GetSet()->GetOutermost()->GetName()) / TEXT("Buildings");
	FString BuildingName = Document->GetBuildingClass().GetAssetName();
	BuildingName.RemoveFromEnd(TEXT("_C"));
	const FString Name = TEXT("DA_") + BuildingName + TEXT("_Decoration");
	UObject* Created = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get()
		.CreateAssetWithDialog(Name, Directory, Factory->DataAssetClass, Factory);
	if (Created && !Document->AttachProfile(Cast<UDeepLevelCityBuildingDecorationProfile>(Created), true))
	{
		OperationError = LOCTEXT("CreateFailed", "Created profile could not be attached; the catalog building or assignment changed.");
		RefreshIssues();
	}
	return FReply::Handled();
}

FReply FDeepLevelCityBuildingDecorationEditor::UnlinkProfile()
{
	const auto* Set = Document->GetSet();
	const auto* Profile = Document->GetProfile();
	if (Set && Profile) { Document->RemoveAssignment(Set, Set->BuildingProfiles.IndexOfByKey(Profile), Profile); }
	return FReply::Handled();
}

FReply FDeepLevelCityBuildingDecorationEditor::UnlinkIssue(TSharedPtr<FDeepLevelCityDecorationDocumentIssue> Issue)
{
	if (Issue && !Document->RemoveAssignment(Issue->AssignmentSet.Get(), Issue->AssignmentIndex, Issue->AssignmentProfile.Get()))
	{
		OperationError = LOCTEXT("AssignmentChanged", "This assignment changed; use the current issue row.");
		RefreshIssues();
	}
	return FReply::Handled();
}

FReply FDeepLevelCityBuildingDecorationEditor::ShowSetSettings()
{
	Document->EndDrag();
	Document->SelectEntry({});
	bSetSettings = true;
	RefreshInspector();
	return FReply::Handled();
}

bool FDeepLevelCityBuildingDecorationEditor::AddDroppedAssets(const TArray<FAssetData>& Assets, const FVector& Location, const FVector& Normal)
{
	if (!HasVariant() || Assets.IsEmpty())
	{
		OperationError = LOCTEXT("AssetSelection", "Select a building profile and variant, then drag decorations from the Content Browser onto the building preview.");
		RefreshIssues();
		return false;
	}
	TArray<FDeepLevelCityBuildingDecorationEntry> Definitions;
	for (const auto& Asset : Assets)
	{
		FDeepLevelCityBuildingDecorationEntry Entry;
		if (!DeepLevelCityDecorationAuthoring::MakeEntryFromAsset(Asset.GetAsset(), Entry, OperationError)) { RefreshIssues(); return false; }
		Entry.LocalTransform.SetLocation(Location);
		if (Entry.Output == EDeepLevelCityDecorationOutput::Decal) { Entry.LocalTransform.SetRotation(FRotationMatrix::MakeFromX(-Normal).ToQuat()); }
		Definitions.Add(Entry);
	}
	Document->AddEntries(Definitions);
	return true;
}

void FDeepLevelCityBuildingDecorationEditor::SelectEntry(const FGuid& Id)
{
	bSetSettings = false;
	Document->SelectEntry(Id);
	RefreshInspector();
}

bool FDeepLevelCityBuildingDecorationEditor::IsSurfacePlacementEnabled() const
{
	return GetDefault<ULevelEditorViewportSettings>()->SnapToSurface.bEnabled && Document->CanEdit();
}

void FDeepLevelCityBuildingDecorationEditor::ApplyWidgetDelta(const FVector& Drag, const FRotator& Rotation,
	const FVector& Scale, const FHitResult* SurfaceHit)
{
	const auto* Entry = Document->GetEntry();
	if (!Entry) { return; }
	FTransform Transform = DeepLevelCityDecorationAuthoring::ApplyTransformDelta(Entry->LocalTransform, Drag, Rotation, Scale);
	if (SurfaceHit && !Preview->GetSurfacePlacementTransform(Entry->EntryGuid, Transform,
		SurfaceHit->ImpactPoint, SurfaceHit->ImpactNormal, Transform))
	{
		ReportPreviewError(LOCTEXT("UnsupportedContact", "Surface snapping requires static mesh geometry or a decal."), Entry->EntryGuid);
		return;
	}
	Document->ApplyDragTransform(Transform);
}

void FDeepLevelCityBuildingDecorationEditor::PlaceSelectedOnSurface(const FVector& Location, const FVector& Normal)
{
	const auto* Entry = Document->GetEntry();
	if (!Entry) { return; }
	FTransform Transform;
	if (!Preview->GetSurfacePlacementTransform(Entry->EntryGuid, Entry->LocalTransform, Location, Normal, Transform))
	{
		ReportPreviewError(LOCTEXT("UnsupportedContact", "Surface snapping requires static mesh geometry or a decal."), Entry->EntryGuid);
		return;
	}
	FText Error;
	if (!Document->SetTransform(Transform, Error)) { OperationError = Error; RefreshIssues(); }
}
void FDeepLevelCityBuildingDecorationEditor::SetWidgetMode(UE::Widget::EWidgetMode Mode)
{
	Document->EndDrag();
	Preview->GetClient()->SetWidgetMode(Mode);
	Preview->GetClient()->Invalidate();
}

void FDeepLevelCityBuildingDecorationEditor::BindViewportCommands(FUICommandList& Commands)
{
	const auto Map = [&](const TSharedPtr<FUICommandInfo>& Command, TFunction<void()> Execute, TFunction<bool()> CanExecute)
	{
		Commands.MapAction(Command,
			FExecuteAction::CreateSPLambda(SharedThis(this), [Execute = MoveTemp(Execute)] { Execute(); }),
			FCanExecuteAction::CreateSPLambda(SharedThis(this), [CanExecute = MoveTemp(CanExecute)] { return CanExecute(); }));
	};
	const auto& Generic = FGenericCommands::Get();
	Map(Generic.Duplicate, [this] { if (HasEntry()) { Document->DuplicateEntry(); } else { Document->DuplicateVariant(); } }, [this] { return HasVariant(); });
	Map(Generic.Delete, [this] { if (HasEntry()) { Document->RemoveEntry(); } else { Document->RemoveVariant(); } }, [this] { return HasVariant(); });
	Map(Generic.Copy, [this] { Document->CopyEntry(); }, [this] { return HasEntry(); });
	Map(Generic.Paste, [this] { Document->PasteEntry(); }, [this] { return HasVariant() && Document->HasClipboard(); });
	Map(Generic.Undo, [this] { Document->Undo(); }, [] { return GEditor != nullptr; });
	Map(Generic.Redo, [this] { Document->Redo(); }, [] { return GEditor != nullptr; });
	const auto& Viewport = FEditorViewportCommands::Get();
	Map(Viewport.TranslateMode, [this] { SetWidgetMode(UE::Widget::WM_Translate); }, [this] { return HasEntry(); });
	Map(Viewport.RotateMode, [this] { SetWidgetMode(UE::Widget::WM_Rotate); }, [this] { return HasEntry(); });
	Map(Viewport.ScaleMode, [this] { SetWidgetMode(UE::Widget::WM_Scale); }, [this] { return HasEntry(); });
}

void FDeepLevelCityBuildingDecorationEditor::ReportPreviewError(const FText& Error, FGuid Entry)
{
	PreviewErrors.Add(Entry, Error);
	if (!bRefreshing) { RefreshIssues(); }
}

FString FDeepLevelCityBuildingDecorationEditor::GetSetPath() const { return Document->GetSet() ? Document->GetSet()->GetPathName() : FString(); }
FString FDeepLevelCityBuildingDecorationEditor::GetCatalogPath() const { return Document->GetCatalog() ? Document->GetCatalog()->GetPathName() : FString(); }

#undef LOCTEXT_NAMESPACE
