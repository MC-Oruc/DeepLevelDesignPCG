// Copyright <--\, Inc. All Rights Reserved.
#include "City/Decoration/DeepLevelCityBuildingDecorationEditor.h"
#include "City/Decoration/DeepLevelCityDecorationAuthoring.h"
#include "City/Decoration/DeepLevelCityDecorationValidation.h"
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

const FName FDeepLevelCityBuildingDecorationEditor::WindowsTab(TEXT("DeepLevelCityWindowLighting"));
const FName FDeepLevelCityBuildingDecorationEditor::StreetsTab(TEXT("DeepLevelCityStreetDecoration"));

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

FDeepLevelCityDecorationWorkspace::~FDeepLevelCityDecorationWorkspace()
{
	if (Document) { Document->Shutdown(); }
}

FDeepLevelCityBuildingDecorationEditor::~FDeepLevelCityBuildingDecorationEditor() = default;

void FDeepLevelCityDecorationWorkspace::InitWorkspace(UDeepLevelCityDecorationSet* Set)
{
	check(Set);
	Document = MakeUnique<FDeepLevelCityDecorationDocument>();
	Document->Open(Set);
	VariantProxy = NewObject<UDeepLevelCityDecorationVariantProxy>(GetTransientPackage(), NAME_None, RF_Transient);
	EntryProxy = NewObject<UDeepLevelCityDecorationEntryProxy>(GetTransientPackage(), NAME_None, RF_Transient);
	SelectionProxy = NewObject<UDeepLevelCityDecorationSelectionProxy>(GetTransientPackage(), NAME_None, RF_Transient);
	BuildingProxy = NewObject<UDeepLevelCityDecorationBuildingProxy>(GetTransientPackage(), NAME_None, RF_Transient);
	SetProxy = NewObject<UDeepLevelCityDecorationSetProxy>(GetTransientPackage(), NAME_None, RF_Transient);
	FDetailsViewArgs Args;
	Args.bAllowSearch = true;
	Args.bHideSelectionTip = true;
	Args.bAllowMultipleTopLevelObjects = false;
	Inspector = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(Args);
	Inspector->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([this](const FPropertyAndParent& Property)
	{
		if (Property.Property.GetOwnerClass() == UDeepLevelCityBuildingDecorationProfile::StaticClass())
            return Property.Property.GetFName() == GET_MEMBER_NAME_CHECKED(UDeepLevelCityBuildingDecorationProfile, WindowMaterial)
                || Property.Property.GetFName() == GET_MEMBER_NAME_CHECKED(UDeepLevelCityBuildingDecorationProfile, WindowAppearances)
                || Property.Property.GetFName() == GET_MEMBER_NAME_CHECKED(UDeepLevelCityBuildingDecorationProfile, WindowDefinitions);
        const FName Name = Property.Property.GetFName();
        if (Property.Property.GetOwnerStruct() == FDeepLevelCityBuildingDecorationEntry::StaticStruct())
        {
            if (bWindowLightingMode)
                return Name == GET_MEMBER_NAME_CHECKED(FDeepLevelCityBuildingDecorationEntry,bEnabled)
                    || Name == GET_MEMBER_NAME_CHECKED(FDeepLevelCityBuildingDecorationEntry,WindowColor)
                    || Name == GET_MEMBER_NAME_CHECKED(FDeepLevelCityBuildingDecorationEntry,WindowIntensity)
                    || Name == GET_MEMBER_NAME_CHECKED(FDeepLevelCityBuildingDecorationEntry,WindowMaterialOverride);
            return Name != GET_MEMBER_NAME_CHECKED(FDeepLevelCityBuildingDecorationEntry,EntryGuid)
                && Name != GET_MEMBER_NAME_CHECKED(FDeepLevelCityBuildingDecorationEntry,WindowDefinitionGuid)
                && Name != GET_MEMBER_NAME_CHECKED(FDeepLevelCityBuildingDecorationEntry,WindowRoomGuid);
        }
        return true;
	}));
	Inspector->SetIsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateSP(this, &FDeepLevelCityDecorationWorkspace::CanEditInspector));
	Inspector->OnFinishedChangingProperties().AddSP(this, &FDeepLevelCityDecorationWorkspace::CommitInspector);
	SAssignNew(Preview, SDeepLevelCityBuildingDecorationViewport).Editor(SharedThis(this));
	WorkspaceWidget = BuildWorkspace();
}

void FDeepLevelCityBuildingDecorationEditor::InitEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host, UDeepLevelCityDecorationSet* Set)
{
    Buildings = MakeShared<FDeepLevelCityDecorationWorkspace>();
    Windows = MakeShared<FDeepLevelCityDecorationWorkspace>();
    Buildings->Toolkit = SharedThis(this); Windows->Toolkit = SharedThis(this);
    Buildings->InitWorkspace(Set); Windows->InitWorkspace(Set);
    Windows->Preview->GetPreviewScene()->SetEnvironmentVisibility(false);
    Windows->Preview->GetPreviewScene()->SetLightBrightness(.15f);
    Windows->Preview->GetPreviewScene()->SetSkyBrightness(.08f);

	const auto Layout = FTabManager::NewLayout(TEXT("DeepLevelCityBuildingDecoration_v6"))
		->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
			->Split(FTabManager::NewStack()->SetSizeCoefficient(0.8f)->AddTab(WorkspaceTab, ETabState::OpenedTab)->AddTab(WindowsTab, ETabState::OpenedTab)->AddTab(StreetsTab, ETabState::OpenedTab)->SetForegroundTab(WorkspaceTab)->SetHideTabWell(false))
			->Split(FTabManager::NewStack()->SetSizeCoefficient(0.2f)->AddTab(EnvironmentTab, ETabState::ClosedTab)));
	InitAssetEditor(Mode, Host, TEXT("DeepLevelCityBuildingDecorationEditor"), Layout, true, true, Set);
	Buildings->Document->OnChanged.AddSP(Buildings.Get(), &FDeepLevelCityDecorationWorkspace::DocumentChanged);
    Windows->Document->OnChanged.AddSP(Windows.Get(), &FDeepLevelCityDecorationWorkspace::DocumentChanged);
    Windows->Preview->SetPlacementGuide(Windows->Document->GetBuildingDefinition(), false);
    Windows->SetWindowLightingMode(true);
    Windows->DocumentChanged(EDeepLevelCityDecorationChange::Context, {});
    GetTabManager()->TryInvokeTab(WorkspaceTab);
	Buildings->DocumentChanged(EDeepLevelCityDecorationChange::Context, {});
	RegenerateMenusAndToolbars();
}

FText FDeepLevelCityBuildingDecorationEditor::GetBaseToolkitName() const { return LOCTEXT("Toolkit", "City Decoration"); }

FText FDeepLevelCityBuildingDecorationEditor::GetToolkitName() const
{
	// The opened asset names the workspace; additional assets belong to its save scope.
	return GetLabelForObject(GetEditingObjects()[0]);
}

bool FDeepLevelCityBuildingDecorationEditor::IncludeAssetInRestoreOpenAssetsPrompt(UObject* Asset) const
{
	const auto& Objects = GetEditingObjects();
	return Asset && !Objects.IsEmpty() && Asset == Objects[0];
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
		.SetDisplayName(LOCTEXT("Workspace", "Building Decorations"));
    Manager->RegisterTabSpawner(WindowsTab, FOnSpawnTab::CreateSP(this, &FDeepLevelCityBuildingDecorationEditor::SpawnWindows))
        .SetDisplayName(LOCTEXT("Windows", "Window Lighting"));
	Manager->RegisterTabSpawner(StreetsTab, FOnSpawnTab::CreateSP(this, &FDeepLevelCityBuildingDecorationEditor::SpawnStreets))
		.SetDisplayName(LOCTEXT("Streets", "Streets & Sidewalks"));
	Manager->RegisterTabSpawner(EnvironmentTab, FOnSpawnTab::CreateSP(this, &FDeepLevelCityBuildingDecorationEditor::SpawnEnvironment))
		.SetDisplayName(LOCTEXT("Environment", "Preview Environment"));
}

void FDeepLevelCityBuildingDecorationEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{
	Manager->UnregisterTabSpawner(WorkspaceTab);
    Manager->UnregisterTabSpawner(WindowsTab);
	Manager->UnregisterTabSpawner(EnvironmentTab);
	Manager->UnregisterTabSpawner(StreetsTab);
	FAssetEditorToolkit::UnregisterTabSpawners(Manager);
}

void FDeepLevelCityDecorationWorkspace::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(VariantProxy);
	Collector.AddReferencedObject(EntryProxy);
	Collector.AddReferencedObject(SelectionProxy);
	Collector.AddReferencedObject(BuildingProxy);
	Collector.AddReferencedObject(SetProxy);
}

TSharedRef<SDockTab> FDeepLevelCityBuildingDecorationEditor::SpawnWorkspace(const FSpawnTabArgs&)
{
    return SNew(SDockTab).TabRole(ETabRole::PanelTab).CanEverClose(false)[Buildings->WorkspaceWidget.ToSharedRef()];
}

TSharedRef<SDockTab> FDeepLevelCityBuildingDecorationEditor::SpawnWindows(const FSpawnTabArgs&)
{
    return SNew(SDockTab).TabRole(ETabRole::PanelTab).CanEverClose(false)[Windows->WorkspaceWidget.ToSharedRef()];
}

FReply FDeepLevelCityDecorationWorkspace::ShowWindowSettings()
{
    bWindowSettings = true;
    bSetSettings = false;
    RefreshInspector();
    return FReply::Handled();
}

TSharedRef<SDockTab> FDeepLevelCityBuildingDecorationEditor::SpawnEnvironment(const FSpawnTabArgs&)
{
	return SNew(SDockTab).TabRole(ETabRole::PanelTab)[SNew(SAdvancedPreviewDetailsTab, Buildings->Preview->GetPreviewScene())];
}

void FDeepLevelCityDecorationWorkspace::DocumentChanged(EDeepLevelCityDecorationChange Change, FGuid Entry)
{
	CancelSymmetryPlacement();
	if (Document->IsDragging() && Change == EDeepLevelCityDecorationChange::Transform)
	{
		if (const auto* Variant = Document->GetVariant())
		{
			for (const auto& Changed : Variant->Entries) { Preview->UpdateEntry(Changed.EntryGuid, Changed.LocalTransform); }
		}
		return;
	}
	TGuardValue<bool> Guard(bRefreshing, true);
	OperationError = FText::GetEmpty();
	for (const UObject* Asset : { static_cast<const UObject*>(Document->GetSet()), static_cast<const UObject*>(Document->GetProfile()) })
	{
        if (auto Root = Toolkit.Pin(); Root && Asset && !Root->GetEditingObjects().Contains(Asset)) Root->AddEditingObject(const_cast<UObject*>(Asset));
	}
	if (Change == EDeepLevelCityDecorationChange::Context || Change == EDeepLevelCityDecorationChange::Selection) { bWindowSettings = false; }
    if (Change == EDeepLevelCityDecorationChange::Context) { bSetSettings = false; }
    if (const auto* Selected = Document->GetVariant(); Selected && Selected->SelectionGroup.IsNone()) PreviewBaseVariant = Selected->VariantGuid;
	if (Change != EDeepLevelCityDecorationChange::Selection && Change != EDeepLevelCityDecorationChange::Transform) { RefreshOutline(); }
	RefreshInspector();
	SynchronizeOutlineSelection();
	Preview->SetPlacementGuide(Document->GetBuildingDefinition(), bShowPlacementGuide);
	if (Change == EDeepLevelCityDecorationChange::Transform)
	{
		if (const auto* Variant = Document->GetVariant())
		{
			for (const auto& Changed : Variant->Entries) { Preview->UpdateEntry(Changed.EntryGuid, Changed.LocalTransform); }
		}
	}
	else if (Change != EDeepLevelCityDecorationChange::Selection && Change != EDeepLevelCityDecorationChange::Metadata)
	{
		Preview->Synchronize(Document->GetBuildingClass(), Document->GetProfile(), Document->GetVariantId(), bWindowLightingMode ? PreviewBaseVariant : FGuid());
	}
	Preview->GetClient()->Invalidate();
	RefreshIssues();
}

void FDeepLevelCityDecorationWorkspace::RefreshInspector()
{
	UObject* Object = nullptr;
	if (bWindowSettings && Document->GetProfile())
    { Object = const_cast<UDeepLevelCityBuildingDecorationProfile*>(Document->GetProfile()); }
	else if (bSetSettings && Document->GetSet())
	{
		SetProxy->Categories = Document->GetSet()->Categories;
		SetProxy->BuildingProfiles = Document->GetSet()->BuildingProfiles;
		Object = SetProxy;
	}
	else if (Document->IsSymmetryPivotSelected() || Document->GetSelectedEntryIds().Num() > 1)
	{
		SelectionProxy->DecorationCount = Document->IsSymmetryPivotSelected() ? 2 : Document->GetSelectedEntryIds().Num();
		SelectionProxy->Transform = SelectionBaseline = Document->GetSelectionTransform();
		Object = SelectionProxy;
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
		VariantProxy->SelectionGroup = Variant->SelectionGroup;
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

void FDeepLevelCityDecorationWorkspace::CommitInspector(const FPropertyChangedEvent& Event)
{
	if (bRefreshing || bWindowSettings) { return; }
	FText Error;
	if (bSetSettings)
	{
		if (Event.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UDeepLevelCityDecorationSetProxy, Categories)) { Document->SetCategories(SetProxy->Categories); }
	}
	else if (Document->IsSymmetryPivotSelected() || Document->GetSelectedEntryIds().Num() > 1)
	{
		const FTransform Desired = SelectionProxy->Transform;
		if (DeepLevelCityDecorationValidation::ValidateTransform(Desired, Error))
		{
			Document->ApplySelectionDelta(Desired.GetLocation() - SelectionBaseline.GetLocation(),
				(Desired.GetRotation() * SelectionBaseline.GetRotation().Inverse()).Rotator(),
				Desired.GetScale3D() / SelectionBaseline.GetScale3D() - FVector::OneVector);
			Document->EndDrag();
		}
		else { OperationError = Error; RefreshInspector(); }
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
			Document->SetVariantFields(VariantProxy->Name, Variant->SelectionWeight, Variant->SelectionGroup);
		}
        else if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDeepLevelCityDecorationVariantProxy, SelectionGroup))
        {
            Document->SetVariantFields(Variant->Name, Variant->SelectionWeight, VariantProxy->SelectionGroup);
        }
		else if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDeepLevelCityDecorationVariantProxy, SelectionWeight))
		{
			Document->SetVariantFields(Variant->Name, VariantProxy->SelectionWeight, Variant->SelectionGroup);
		}
	}
	RefreshIssues();
}

void FDeepLevelCityDecorationWorkspace::ChooseSet(const FAssetData& Asset)
{
    if (auto Root = Toolkit.Pin()) Root->ChooseSet(Asset);
}
void FDeepLevelCityBuildingDecorationEditor::ChooseSet(const FAssetData& Asset)
{
    Buildings->Document->Open(Cast<UDeepLevelCityDecorationSet>(Asset.GetAsset()), const_cast<UDeepLevelCityBuildingDecorationProfile*>(Buildings->Document->GetProfile()));
    Windows->Document->Open(Cast<UDeepLevelCityDecorationSet>(Asset.GetAsset()));
    Windows->SetWindowLightingMode(true);
}
void FDeepLevelCityDecorationWorkspace::ChooseCatalog(const FAssetData& Asset) { Document->SetCatalog(Cast<UDeepLevelBuildingPlacementCatalog>(Asset.GetAsset())); }
void FDeepLevelCityDecorationWorkspace::AttachProfile(const FAssetData& Asset)
{
	if (!Document->AttachProfile(Cast<UDeepLevelCityBuildingDecorationProfile>(Asset.GetAsset())))
	{
		OperationError = LOCTEXT("CannotAttach", "Profile must match the selected catalog building, with no existing assignment.");
		RefreshIssues();
	}
}

FReply FDeepLevelCityDecorationWorkspace::CreateProfile()
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

FReply FDeepLevelCityDecorationWorkspace::UnlinkProfile()
{
	const auto* Set = Document->GetSet();
	const auto* Profile = Document->GetProfile();
	if (Set && Profile) { Document->RemoveAssignment(Set, Set->BuildingProfiles.IndexOfByKey(Profile), Profile); }
	return FReply::Handled();
}

FReply FDeepLevelCityDecorationWorkspace::UnlinkIssue(TSharedPtr<FDeepLevelCityDecorationDocumentIssue> Issue)
{
	if (Issue && !Document->RemoveAssignment(Issue->AssignmentSet.Get(), Issue->AssignmentIndex, Issue->AssignmentProfile.Get()))
	{
		OperationError = LOCTEXT("AssignmentChanged", "This assignment changed; use the current issue row.");
		RefreshIssues();
	}
	return FReply::Handled();
}

FReply FDeepLevelCityDecorationWorkspace::ShowSetSettings()
{
	Document->EndDrag();
	Document->SelectEntry({});
	bSetSettings = true;
	RefreshInspector();
	return FReply::Handled();
}

bool FDeepLevelCityDecorationWorkspace::AddDroppedAssets(const TArray<FAssetData>& Assets, const FVector& Location, const FVector& Normal)
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
        if (bWindowLightingMode && Entry.Output == EDeepLevelCityDecorationOutput::Mesh)
        {
            Entry.bWindowLight = true;
            Entry.Rendering.bCastShadow = false; Entry.Rendering.bAffectNavigation = false;
            Entry.Rendering.CollisionProfile = TEXT("NoCollision"); Entry.Rendering.MaxDrawDistance = 16000;
        }
		Entry.LocalTransform.SetLocation(Location);
		if (Entry.Output == EDeepLevelCityDecorationOutput::Decal) { Entry.LocalTransform.SetRotation(FRotationMatrix::MakeFromX(-Normal).ToQuat()); }
		Definitions.Add(Entry);
	}
	Document->AddEntries(Definitions);
	return true;
}

void FDeepLevelCityDecorationWorkspace::SelectEntry(const FGuid& Id, bool bToggle, bool bAdd)
{
	bSetSettings = false;
	if ((bToggle || bAdd) && Id.IsValid())
	{
		auto Ids = Document->GetSelectedEntryIds();
		if (bToggle && Ids.Contains(Id)) { Ids.Remove(Id); }
		else { Ids.Remove(Id); Ids.Add(Id); }
		Document->SelectEntries(Ids);
	}
	else if (!bToggle && !bAdd) { Document->SelectEntry(Id); }
	RefreshInspector();
}

bool FDeepLevelCityDecorationWorkspace::IsSurfacePlacementEnabled() const
{
	return GetDefault<ULevelEditorViewportSettings>()->SnapToSurface.bEnabled && Document->CanEdit()
		&& !Document->IsSymmetryPivotSelected() && Document->GetSelectedEntryIds().Num() == 1;
}

void FDeepLevelCityDecorationWorkspace::ApplyWidgetDelta(const FVector& Drag, const FRotator& Rotation,
	const FVector& Scale, const FHitResult* SurfaceHit)
{
	const auto* Entry = Document->GetEntry();
	if (!Entry) { return; }
	if (Document->IsSymmetryPivotSelected() || Document->GetSelectedEntryIds().Num() > 1)
	{
		const FQuat ScaleBasis = Preview->GetClient()->GetWidgetCoordSystemSpace() == COORD_Local
			? Document->GetSelectionTransform().GetRotation() : FQuat::Identity;
		Document->ApplySelectionDelta(Drag, Rotation, Scale, ScaleBasis);
		return;
	}
	FTransform Transform = DeepLevelCityDecorationAuthoring::ApplyTransformDelta(Entry->LocalTransform, Drag, Rotation, Scale);
	if (SurfaceHit && !Preview->GetSurfacePlacementTransform(Entry->EntryGuid, Transform,
		SurfaceHit->ImpactPoint, SurfaceHit->ImpactNormal, Transform))
	{
		ReportPreviewError(LOCTEXT("UnsupportedContact", "Surface snapping requires static mesh geometry or a decal."), Entry->EntryGuid);
		return;
	}
	Document->ApplyDragTransform(Transform);
}

void FDeepLevelCityDecorationWorkspace::PlaceSelectedOnSurface(const FVector& Location, const FVector& Normal)
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
void FDeepLevelCityDecorationWorkspace::SetWidgetMode(UE::Widget::EWidgetMode Mode)
{
	Document->EndDrag();
	Preview->GetClient()->SetWidgetMode(Mode);
	Preview->GetClient()->Invalidate();
}

void FDeepLevelCityDecorationWorkspace::BindViewportCommands(FUICommandList& Commands)
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
	Map(Generic.SelectAll, [this]
	{
		TArray<FGuid> Ids;
		for (const auto& Entry : Document->GetVariant()->Entries) { Ids.Add(Entry.EntryGuid); }
		Document->SelectEntries(Ids);
	}, [this] { return HasVariant(); });
	Map(Generic.Copy, [this] { Document->CopyEntry(); }, [this] { return HasEntry(); });
	Map(Generic.Paste, [this] { Document->PasteEntry(); }, [this] { return HasVariant() && Document->HasClipboard(); });
	Map(Generic.Undo, [this] { if (IsPlacingSymmetry()) { CancelSymmetryPlacement(); } else { Document->Undo(); } }, [] { return GEditor != nullptr; });
	Map(Generic.Redo, [this] { Document->Redo(); }, [] { return GEditor != nullptr; });
	const auto& Viewport = FEditorViewportCommands::Get();
	Map(Viewport.TranslateMode, [this] { SetWidgetMode(UE::Widget::WM_Translate); }, [this] { return HasEntry(); });
	Map(Viewport.RotateMode, [this] { SetWidgetMode(UE::Widget::WM_Rotate); }, [this] { return HasEntry(); });
	Map(Viewport.ScaleMode, [this] { SetWidgetMode(UE::Widget::WM_Scale); }, [this] { return HasEntry(); });
}

void FDeepLevelCityDecorationWorkspace::ReportPreviewError(const FText& Error, FGuid Entry)
{
	PreviewErrors.Add(Entry, Error);
	if (!bRefreshing) { RefreshIssues(); }
}

FString FDeepLevelCityDecorationWorkspace::GetSetPath() const { return Document->GetSet() ? Document->GetSet()->GetPathName() : FString(); }
FString FDeepLevelCityDecorationWorkspace::GetCatalogPath() const { return Document->GetCatalog() ? Document->GetCatalog()->GetPathName() : FString(); }

#undef LOCTEXT_NAMESPACE
