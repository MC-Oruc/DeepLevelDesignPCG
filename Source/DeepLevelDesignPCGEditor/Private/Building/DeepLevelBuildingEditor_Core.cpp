// Copyright <--\, Inc. All Rights Reserved.

#include "Building/DeepLevelBuildingEditor.h"
#include "Widgets/Layout/SSpacer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DeepLevelBuildingEditor)

// ---- DeepLevelBuildingCatalogAssetTypeActions ----

#include "Building/DeepLevelBuildingPCG.h"

#define LOCTEXT_NAMESPACE "DeepLevelBuildingCatalogAssetTypeActions"

FText FDeepLevelBuildingCatalogAssetTypeActions::GetName() const
{
	return LOCTEXT("Name", "DeepLevel Building Placement Catalog");
}

UClass* FDeepLevelBuildingCatalogAssetTypeActions::GetSupportedClass() const
{
	return UDeepLevelBuildingPlacementCatalog::StaticClass();
}

void FDeepLevelBuildingCatalogAssetTypeActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;
	for (UObject* Object : InObjects)
	{
		if (UDeepLevelBuildingPlacementCatalog* Catalog = Cast<UDeepLevelBuildingPlacementCatalog>(Object))
		{
			MakeShared<FDeepLevelBuildingCatalogEditorToolkit>()->InitEditor(Mode, EditWithinLevelEditor, Catalog);
		}
	}
}

#undef LOCTEXT_NAMESPACE

// ---- DeepLevelBuildingCatalogEditorToolkit ----

#include "AssetRegistry/AssetData.h"
#include "ContentBrowserModule.h"
#include "Engine/Blueprint.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "PackedLevelActor/PackedLevelActor.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "DeepLevelBuildingCatalogEditor"

const FName FDeepLevelBuildingCatalogEditorToolkit::WorkspaceTabId(TEXT("DeepLevelBuildingCatalogWorkspace"));

FDeepLevelBuildingCatalogEditorToolkit::~FDeepLevelBuildingCatalogEditorToolkit()
{
	FaceDragTransaction.Reset();
	BuildingPreviewViewport.Reset();
	PresetPreviewViewport.Reset();
	SectionSwitcher.Reset();
	BuildingList.Reset();
	PresetList.Reset();
	PaletteList.Reset();
	SequenceList.Reset();
	BuildingDetails.Reset();
	PresetDetails.Reset();
	BuildingProxy = nullptr;
	PresetProxy = nullptr;
	Catalog = nullptr;
}

void FDeepLevelBuildingCatalogEditorToolkit::InitEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host, UDeepLevelBuildingPlacementCatalog* InCatalog)
{
	Catalog = InCatalog;
	if (!Catalog) return;
	BuildingProxy = NewObject<UDeepLevelBuildingCalibrationProxy>(GetTransientPackage(), NAME_None, RF_Transient);
	PresetProxy = NewObject<UDeepLevelBuildingPresetProxy>(GetTransientPackage(), NAME_None, RF_Transient);

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = true;
	DetailsArgs.bHideSelectionTip = true;
	BuildingDetails = PropertyEditor.CreateDetailView(DetailsArgs);
	PresetDetails = PropertyEditor.CreateDetailView(DetailsArgs);
	BuildingDetails->SetObject(BuildingProxy);
	PresetDetails->SetObject(PresetProxy);
	BuildingDetails->OnFinishedChangingProperties().AddSP(this, &FDeepLevelBuildingCatalogEditorToolkit::CommitBuildingProxy);
	PresetDetails->OnFinishedChangingProperties().AddSP(this, &FDeepLevelBuildingCatalogEditorToolkit::CommitPresetProxy);

	RefreshLists();
	if (!Catalog->Buildings.IsEmpty()) { SelectedBuilding = 0; LoadBuildingProxy(); }
	if (!Catalog->Presets.IsEmpty()) { SelectedPreset = 0; LoadPresetProxy(); }
	RefreshValidation();

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("DeepLevel_BuildingCatalogEditor_v1"))
		->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split(FTabManager::NewStack()->AddTab(WorkspaceTabId, ETabState::OpenedTab)->SetHideTabWell(true)));
	InitAssetEditor(Mode, Host, TEXT("DeepLevelBuildingCatalogEditor"), Layout, true, true, Catalog);
	RegenerateMenusAndToolbars();
}

void FDeepLevelBuildingCatalogEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
	InTabManager->RegisterTabSpawner(WorkspaceTabId, FOnSpawnTab::CreateSP(this, &FDeepLevelBuildingCatalogEditorToolkit::SpawnWorkspaceTab))
		.SetDisplayName(LOCTEXT("WorkspaceTab", "PLA Calibration & Presets"));
}

void FDeepLevelBuildingCatalogEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(WorkspaceTabId);
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

TSharedRef<SDockTab> FDeepLevelBuildingCatalogEditorToolkit::SpawnWorkspaceTab(const FSpawnTabArgs&)
{
	const TSharedRef<SWidget> RootWidget = BuildRootWidget();
	RefreshPreview();
	return SNew(SDockTab).TabRole(ETabRole::PanelTab)[RootWidget];
}

FText FDeepLevelBuildingCatalogEditorToolkit::GetBaseToolkitName() const { return LOCTEXT("ToolkitName", "PLA Calibration & Presets"); }

void FDeepLevelBuildingCatalogEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Catalog);
	Collector.AddReferencedObject(BuildingProxy);
	Collector.AddReferencedObject(PresetProxy);
}

TSharedRef<SWidget> FDeepLevelBuildingCatalogEditorToolkit::BuildRootWidget()
{
	return SNew(SVerticalBox)
	+ SVerticalBox::Slot().AutoHeight().Padding(6)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("BuildingsTab", "Buildings")).OnClicked(this, &FDeepLevelBuildingCatalogEditorToolkit::SwitchSection, ESection::Buildings)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("PresetsTab", "Presets")).OnClicked(this, &FDeepLevelBuildingCatalogEditorToolkit::SwitchSection, ESection::Presets)]
		+ SHorizontalBox::Slot().FillWidth(1)[SNew(SSpacer)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4)[SNew(STextBlock).Text(this, &FDeepLevelBuildingCatalogEditorToolkit::GetValidationText).ColorAndOpacity(FLinearColor(1.0f, 0.65f, 0.1f))]
	]
	+ SVerticalBox::Slot().FillHeight(1)
	[
		SAssignNew(SectionSwitcher, SWidgetSwitcher)
		+ SWidgetSwitcher::Slot()[BuildBuildingsWidget()]
		+ SWidgetSwitcher::Slot()[BuildPresetsWidget()]
	];
}

TSharedRef<SWidget> FDeepLevelBuildingCatalogEditorToolkit::BuildBuildingsWidget()
{
	SAssignNew(BuildingPreviewViewport, SDeepLevelBuildingCatalogPreviewViewport).Toolkit(SharedThis(this));
	return SNew(SVerticalBox)
	+ SVerticalBox::Slot().AutoHeight().Padding(4)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("AddBuilding", "Add Building")).OnClicked(this, &FDeepLevelBuildingCatalogEditorToolkit::AddBuilding)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("RemoveBuilding", "Remove Building")).OnClicked(this, &FDeepLevelBuildingCatalogEditorToolkit::RemoveBuilding)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("AutoFit", "Auto Fit")).OnClicked(this, &FDeepLevelBuildingCatalogEditorToolkit::AutoFit)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("Reset", "Reset")).OnClicked(this, &FDeepLevelBuildingCatalogEditorToolkit::ResetBuilding)]
	]
	+ SVerticalBox::Slot().FillHeight(1)
	[
		SNew(SSplitter)
		+ SSplitter::Slot().Value(0.2f)
		[
			SAssignNew(BuildingList, SListView<TSharedPtr<int32>>).ListItemsSource(&BuildingItems)
			.OnGenerateRow(this, &FDeepLevelBuildingCatalogEditorToolkit::GenerateBuildingRow)
			.OnSelectionChanged(this, &FDeepLevelBuildingCatalogEditorToolkit::OnBuildingSelectionChanged)
		]
		+ SSplitter::Slot().Value(0.55f)[BuildingPreviewViewport.ToSharedRef()]
		+ SSplitter::Slot().Value(0.25f)[BuildingDetails.ToSharedRef()]
	];
}

TSharedRef<SWidget> FDeepLevelBuildingCatalogEditorToolkit::BuildPresetsWidget()
{
	SAssignNew(PresetPreviewViewport, SDeepLevelBuildingCatalogPreviewViewport).Toolkit(SharedThis(this));
	return SNew(SVerticalBox)
	+ SVerticalBox::Slot().AutoHeight().Padding(4)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("AddPreset", "Add Preset")).OnClicked(this, &FDeepLevelBuildingCatalogEditorToolkit::AddPreset)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("DuplicatePreset", "Duplicate")).OnClicked(this, &FDeepLevelBuildingCatalogEditorToolkit::DuplicatePreset)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("RemovePreset", "Remove")).OnClicked(this, &FDeepLevelBuildingCatalogEditorToolkit::RemovePreset)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(12,2)[SNew(STextBlock).Text(this, &FDeepLevelBuildingCatalogEditorToolkit::GetPresetSummary)]
	]
	+ SVerticalBox::Slot().FillHeight(1)
	[
		SNew(SSplitter)
		+ SSplitter::Slot().Value(0.18f)[SAssignNew(PresetList, SListView<TSharedPtr<int32>>).ListItemsSource(&PresetItems).OnGenerateRow(this, &FDeepLevelBuildingCatalogEditorToolkit::GeneratePresetRow).OnSelectionChanged(this, &FDeepLevelBuildingCatalogEditorToolkit::OnPresetSelectionChanged)]
		+ SSplitter::Slot().Value(0.48f)[PresetPreviewViewport.ToSharedRef()]
		+ SSplitter::Slot().Value(0.34f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(0.35f)[PresetDetails.ToSharedRef()]
			+ SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(STextBlock).Text(LOCTEXT("Sequence", "Ordered Sequence"))]
			+ SVerticalBox::Slot().FillHeight(0.25f)[SAssignNew(SequenceList, SListView<TSharedPtr<int32>>).ListItemsSource(&SequenceItems).OnGenerateRow(this, &FDeepLevelBuildingCatalogEditorToolkit::GenerateSequenceRow).OnSelectionChanged(this, &FDeepLevelBuildingCatalogEditorToolkit::OnSequenceSelectionChanged)]
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("↑"))).OnClicked(this, &FDeepLevelBuildingCatalogEditorToolkit::MovePresetBuilding, -1)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("↓"))).OnClicked(this, &FDeepLevelBuildingCatalogEditorToolkit::MovePresetBuilding, 1)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("RemoveSelected", "Remove Selected")).OnClicked(this, &FDeepLevelBuildingCatalogEditorToolkit::RemovePresetBuilding)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("EditSelectedBuilding", "Edit Building")).OnClicked(this, &FDeepLevelBuildingCatalogEditorToolkit::EditPresetBuilding)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(STextBlock).Text(LOCTEXT("Palette", "Building Palette"))]
			+ SVerticalBox::Slot().FillHeight(0.25f)[SAssignNew(PaletteList, SListView<TSharedPtr<int32>>).ListItemsSource(&PaletteItems).OnGenerateRow(this, &FDeepLevelBuildingCatalogEditorToolkit::GeneratePaletteRow).OnSelectionChanged(this, &FDeepLevelBuildingCatalogEditorToolkit::OnPaletteSelectionChanged)]
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("AddToPreset", "Add to Sequence")).OnClicked(this, &FDeepLevelBuildingCatalogEditorToolkit::AddPaletteBuilding)]
			]
		]
	];
}

TSharedRef<ITableRow> FDeepLevelBuildingCatalogEditorToolkit::GenerateBuildingRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& Owner)
{
	if (!Catalog || !Item.IsValid() || !Catalog->Buildings.IsValidIndex(*Item))
	{
		return SNew(STableRow<TSharedPtr<int32>>, Owner)[SNew(STextBlock).Text(LOCTEXT("InvalidBuildingRow", "<Invalid building>"))];
	}
	const FDeepLevelBuildingPlacementDefinition& Def = Catalog->Buildings[*Item];
	const FString Name = Def.BuildingClass.IsNull() ? TEXT("<Missing Class>") : Def.BuildingClass.GetAssetName();
	return SNew(STableRow<TSharedPtr<int32>>, Owner)[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s %s"), Def.bCalibrated ? TEXT("[OK]") : TEXT("[!]"), *Name)))];
}

TSharedRef<ITableRow> FDeepLevelBuildingCatalogEditorToolkit::GeneratePresetRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& Owner)
{
	if (!Catalog || !Item.IsValid() || !Catalog->Presets.IsValidIndex(*Item))
	{
		return SNew(STableRow<TSharedPtr<int32>>, Owner)[SNew(STextBlock).Text(LOCTEXT("InvalidPresetRow", "<Invalid preset>"))];
	}
	return SNew(STableRow<TSharedPtr<int32>>, Owner)[SNew(STextBlock).Text(FText::FromName(Catalog->Presets[*Item].Name))];
}

TSharedRef<ITableRow> FDeepLevelBuildingCatalogEditorToolkit::GeneratePaletteRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& Owner)
{
	if (!Catalog || !Item.IsValid() || !Catalog->Buildings.IsValidIndex(*Item))
	{
		return SNew(STableRow<TSharedPtr<int32>>, Owner)[SNew(STextBlock).Text(LOCTEXT("InvalidPaletteRow", "<Invalid building>"))];
	}
	const FDeepLevelBuildingPlacementDefinition& Def = Catalog->Buildings[*Item];
	return SNew(STableRow<TSharedPtr<int32>>, Owner)[SNew(STextBlock).Text(FText::FromString(Def.BuildingClass.GetAssetName())).ColorAndOpacity(Def.bCalibrated ? FLinearColor::White : FLinearColor(1,0.65f,0.1f))];
}

TSharedRef<ITableRow> FDeepLevelBuildingCatalogEditorToolkit::GenerateSequenceRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& Owner)
{
	FString Label(TEXT("<Missing>"));
	if (Catalog && Item.IsValid() && Catalog->Presets.IsValidIndex(SelectedPreset) && Catalog->Presets[SelectedPreset].Buildings.IsValidIndex(*Item))
	{
		Label = FString::Printf(TEXT("%02d  %s"), *Item + 1, *Catalog->Presets[SelectedPreset].Buildings[*Item].GetAssetName());
	}
	return SNew(STableRow<TSharedPtr<int32>>, Owner)[SNew(STextBlock).Text(FText::FromString(Label))];
}

void FDeepLevelBuildingCatalogEditorToolkit::RefreshLists()
{
	if (!Catalog) return;
	BuildingItems.Reset(); PaletteItems.Reset(); PresetItems.Reset();
	for (int32 Index = 0; Index < Catalog->Buildings.Num(); ++Index) { BuildingItems.Add(MakeShared<int32>(Index)); PaletteItems.Add(MakeShared<int32>(Index)); }
	for (int32 Index = 0; Index < Catalog->Presets.Num(); ++Index) { PresetItems.Add(MakeShared<int32>(Index)); }
	if (BuildingList) BuildingList->RequestListRefresh();
	if (PresetList) PresetList->RequestListRefresh();
	if (PaletteList) PaletteList->RequestListRefresh();
	RefreshSequenceItems();
}

void FDeepLevelBuildingCatalogEditorToolkit::RefreshSequenceItems()
{
	SequenceItems.Reset();
	if (!Catalog) return;
	if (Catalog->Presets.IsValidIndex(SelectedPreset))
	{
		for (int32 Index=0; Index<Catalog->Presets[SelectedPreset].Buildings.Num(); ++Index) SequenceItems.Add(MakeShared<int32>(Index));
	}
	if (SequenceList) SequenceList->RequestListRefresh();
}

void FDeepLevelBuildingCatalogEditorToolkit::SelectBuilding(int32 Index) { SelectedBuilding = Index; LoadBuildingProxy(); RefreshPreview(); }
void FDeepLevelBuildingCatalogEditorToolkit::SelectPreset(int32 Index) { SelectedPreset = Index; LoadPresetProxy(); RefreshPreview(); }

void FDeepLevelBuildingCatalogEditorToolkit::OnBuildingSelectionChanged(TSharedPtr<int32> Item, ESelectInfo::Type)
{
	if (Item.IsValid() && Catalog && Catalog->Buildings.IsValidIndex(*Item)) SelectBuilding(*Item);
}

void FDeepLevelBuildingCatalogEditorToolkit::OnPresetSelectionChanged(TSharedPtr<int32> Item, ESelectInfo::Type)
{
	if (Item.IsValid() && Catalog && Catalog->Presets.IsValidIndex(*Item)) SelectPreset(*Item);
}

void FDeepLevelBuildingCatalogEditorToolkit::OnSequenceSelectionChanged(TSharedPtr<int32> Item, ESelectInfo::Type)
{
	SelectedPresetBuilding = Item.IsValid() ? *Item : INDEX_NONE;
}

void FDeepLevelBuildingCatalogEditorToolkit::OnPaletteSelectionChanged(TSharedPtr<int32> Item, ESelectInfo::Type)
{
	SelectedPaletteBuilding = Item.IsValid() ? *Item : INDEX_NONE;
}

void FDeepLevelBuildingCatalogEditorToolkit::LoadBuildingProxy()
{
	if (!Catalog || !BuildingProxy || !BuildingDetails || !Catalog->Buildings.IsValidIndex(SelectedBuilding)) return;
	bLoadingProxy = true;
	const auto& D = Catalog->Buildings[SelectedBuilding];
	BuildingProxy->BuildingClass=D.BuildingClass;
	BuildingProxy->VolumeCenter=D.PlacementVolume.Center;
	BuildingProxy->VolumeRotation=D.PlacementVolume.Rotation;
	BuildingProxy->VolumeExtent=D.PlacementVolume.Extent;
	BuildingProxy->Exposure=D.PlacementVolume.Exposure;
	BuildingProxy->SelectionWeight=D.SelectionWeight;
	BuildingProxy->bCalibrated=D.bCalibrated;
	const FDeepLevelResolvedBuildingGeometry Geometry=FDeepLevelBuildingPlacementGeometry::ResolveGeometry(D, BuildingProxy->PreviewStreetFace);
	BuildingProxy->Width=Geometry.HalfWidth*2.0;
	BuildingProxy->Depth=Geometry.HalfDepth*2.0;
	BuildingProxy->Height=D.PlacementVolume.Extent.Z*2.0;
	BuildingProxy->ActorToVolumePivot=D.PlacementVolume.Center;
	BuildingDetails->ForceRefresh(); bLoadingProxy=false;
}

void FDeepLevelBuildingCatalogEditorToolkit::LoadPresetProxy()
{
	if (!Catalog || !PresetProxy || !PresetDetails || !Catalog->Presets.IsValidIndex(SelectedPreset)) return;
	bLoadingProxy=true; const auto& P=Catalog->Presets[SelectedPreset]; PresetProxy->Name=P.Name; PresetProxy->SelectionWeight=P.SelectionWeight; PresetProxy->Buildings=P.Buildings; PresetDetails->ForceRefresh(); bLoadingProxy=false; RefreshSequenceItems();
}

void FDeepLevelBuildingCatalogEditorToolkit::CommitBuildingProxy(const FPropertyChangedEvent& Event)
{
	if (bLoadingProxy || !Catalog->Buildings.IsValidIndex(SelectedBuilding)) return;
	if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDeepLevelBuildingCalibrationProxy, PreviewStreetFace)
		|| Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDeepLevelBuildingCalibrationProxy, SideGuides)
		|| Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDeepLevelBuildingCalibrationProxy, GroundGuide)) { RefreshPreview(); return; }
	const FName PropertyName = Event.GetPropertyName();
	const FName MemberPropertyName = Event.GetMemberPropertyName();
	const auto IsChangedProperty = [PropertyName, MemberPropertyName](const FName Candidate)
	{
		return PropertyName == Candidate || MemberPropertyName == Candidate;
	};
	const bool bCalibrationGeometryChanged =
		IsChangedProperty(GET_MEMBER_NAME_CHECKED(UDeepLevelBuildingCalibrationProxy, VolumeCenter))
		|| IsChangedProperty(GET_MEMBER_NAME_CHECKED(UDeepLevelBuildingCalibrationProxy, VolumeRotation))
		|| IsChangedProperty(GET_MEMBER_NAME_CHECKED(UDeepLevelBuildingCalibrationProxy, VolumeExtent));
	ModifyCatalog(LOCTEXT("EditBuildingTx", "Edit Building Calibration"), [this, bCalibrationGeometryChanged]
	{
		auto& Definition = Catalog->Buildings[SelectedBuilding];
		Definition.PlacementVolume.Center = BuildingProxy->VolumeCenter;
		Definition.PlacementVolume.Rotation = BuildingProxy->VolumeRotation;
		Definition.PlacementVolume.Extent = BuildingProxy->VolumeExtent.ComponentMax(FVector(1.0));
		Definition.PlacementVolume.Exposure = BuildingProxy->Exposure;
		Definition.SelectionWeight = BuildingProxy->SelectionWeight;
		Definition.bCalibrated = bCalibrationGeometryChanged ? true : BuildingProxy->bCalibrated;
	});
	LoadBuildingProxy();
	RefreshPreview();
	RefreshLists();
}

void FDeepLevelBuildingCatalogEditorToolkit::CommitPresetProxy(const FPropertyChangedEvent& Event)
{
	if (bLoadingProxy || !Catalog->Presets.IsValidIndex(SelectedPreset)) return;
	if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDeepLevelBuildingPresetProxy, PreviewSeed))
	{
		RefreshPreview();
		return;
	}
	ModifyCatalog(LOCTEXT("EditPresetTx", "Edit Building Preset"), [this]{ auto& P=Catalog->Presets[SelectedPreset]; P.Name=PresetProxy->Name; P.SelectionWeight=PresetProxy->SelectionWeight; P.Buildings=PresetProxy->Buildings; }); RefreshPreview(); RefreshLists();
}

void FDeepLevelBuildingCatalogEditorToolkit::ModifyCatalog(const FText& Text, TFunctionRef<void()> Mutation)
{
	if (!Catalog) return;
	FScopedTransaction Transaction(Text);
	Catalog->Modify();
	Mutation();
	Catalog->PostEditChange();
	Catalog->MarkPackageDirty();
	RefreshValidation();
}

FReply FDeepLevelBuildingCatalogEditorToolkit::SwitchSection(ESection NewSection) { Section=NewSection; if(SectionSwitcher) SectionSwitcher->SetActiveWidgetIndex(NewSection==ESection::Buildings?0:1); RefreshPreview(); return FReply::Handled(); }

FReply FDeepLevelBuildingCatalogEditorToolkit::AddBuilding()
{
	if (!Catalog || !BuildingPreviewViewport)
	{
		return FReply::Handled();
	}

	FOpenAssetDialogConfig DialogConfig;
	DialogConfig.DialogTitleOverride = LOCTEXT("AddBuildingDialogTitle", "Select Packed Level Actor Blueprints");
	DialogConfig.DefaultPath = TEXT("/Game");
	DialogConfig.AssetClassNames.Add(UBlueprint::StaticClass()->GetClassPathName());
	DialogConfig.bAllowMultipleSelection = true;

	FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	const TArray<FAssetData> SelectedAssets = ContentBrowser.Get().CreateModalOpenAssetDialog(DialogConfig);
	if (SelectedAssets.IsEmpty())
	{
		return FReply::Handled();
	}

	struct FPendingBuilding
	{
		TSoftClassPtr<AActor> BuildingClass;
		FVector Center = FVector::ZeroVector;
		FVector Extent = FVector::ZeroVector;
	};

	TArray<FPendingBuilding> PendingBuildings;
	for (const FAssetData& AssetData : SelectedAssets)
	{
		const UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
		UClass* BuildingClass = Blueprint ? Blueprint->GeneratedClass : nullptr;
		if (!BuildingClass || !BuildingClass->IsChildOf(APackedLevelActor::StaticClass()))
		{
			continue;
		}

		const FSoftObjectPath ClassPath(BuildingClass);
		const bool bAlreadyExists = Catalog->Buildings.ContainsByPredicate([&ClassPath](const FDeepLevelBuildingPlacementDefinition& Definition)
		{
			return Definition.BuildingClass.ToSoftObjectPath() == ClassPath;
		});
		if (bAlreadyExists)
		{
			continue;
		}

		FVector Center;
		FVector Extent;
		if (!BuildingPreviewViewport->AutoFitVolume(BuildingClass, FRotator::ZeroRotator, Center, Extent))
		{
			continue;
		}

		FPendingBuilding& Pending = PendingBuildings.Emplace_GetRef();
		Pending.BuildingClass = BuildingClass;
		Pending.Center = Center;
		Pending.Extent = Extent;
	}

	if (PendingBuildings.IsEmpty())
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("NoValidBuildingSelected", "No new Packed Level Actor Blueprint could be added. The selected assets were invalid, already present, or could not be auto-fitted."));
		return FReply::Handled();
	}

	const int32 FirstNewBuildingIndex = Catalog->Buildings.Num();
	ModifyCatalog(LOCTEXT("AddBuildingsTx", "Add Packed Level Actor Buildings"), [this, &PendingBuildings]
	{
		for (const FPendingBuilding& Pending : PendingBuildings)
		{
			FDeepLevelBuildingPlacementDefinition& Definition = Catalog->Buildings.Emplace_GetRef();
			Definition.BuildingClass = Pending.BuildingClass;
			Definition.PlacementVolume.Center = Pending.Center;
			Definition.PlacementVolume.Extent = Pending.Extent;
			Definition.bCalibrated = true;
		}
	});

	SelectedBuilding = FirstNewBuildingIndex;
	RefreshLists();
	LoadBuildingProxy();
	if (BuildingList && BuildingItems.IsValidIndex(SelectedBuilding))
	{
		BuildingList->SetSelection(BuildingItems[SelectedBuilding]);
	}
	RefreshPreview();
	return FReply::Handled();
}

FReply FDeepLevelBuildingCatalogEditorToolkit::RemoveBuilding()
{
	if (!Catalog || !Catalog->Buildings.IsValidIndex(SelectedBuilding))
	{
		return FReply::Handled();
	}

	const TSoftClassPtr<AActor> BuildingClass = Catalog->Buildings[SelectedBuilding].BuildingClass;
	for (const FDeepLevelBuildingSequencePreset& Preset : Catalog->Presets)
	{
		if (Preset.Buildings.Contains(BuildingClass))
		{
			FMessageDialog::Open(
				EAppMsgType::Ok,
				FText::Format(
					LOCTEXT("BuildingUsedByPreset", "Cannot remove this building because preset '{0}' references it."),
					FText::FromName(Preset.Name)));
			return FReply::Handled();
		}
	}

	if (FMessageDialog::Open(
		EAppMsgType::YesNo,
		FText::Format(
			LOCTEXT("ConfirmRemoveBuilding", "Remove '{0}' from the building catalog?"),
			FText::FromString(BuildingClass.GetAssetName()))) != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	ModifyCatalog(LOCTEXT("RemoveBuildingTx", "Remove Building From Catalog"), [this]
	{
		Catalog->Buildings.RemoveAt(SelectedBuilding);
	});
	SelectedBuilding = Catalog->Buildings.IsEmpty()
		? INDEX_NONE
		: FMath::Clamp(SelectedBuilding, 0, Catalog->Buildings.Num() - 1);
	RefreshLists();
	if (SelectedBuilding != INDEX_NONE)
	{
		LoadBuildingProxy();
		if (BuildingList && BuildingItems.IsValidIndex(SelectedBuilding))
		{
			BuildingList->SetSelection(BuildingItems[SelectedBuilding]);
		}
	}
	else if (BuildingProxy && BuildingDetails)
	{
		bLoadingProxy = true;
		BuildingProxy->BuildingClass.Reset();
		BuildingProxy->VolumeCenter = FVector::ZeroVector;
		BuildingProxy->VolumeRotation = FRotator::ZeroRotator;
		BuildingProxy->VolumeExtent = FVector(500.0);
		BuildingProxy->Exposure = FDeepLevelBuildingFaceExposureRules();
		BuildingProxy->Width = 0.0;
		BuildingProxy->Depth = 0.0;
		BuildingProxy->Height = 0.0;
		BuildingProxy->ActorToVolumePivot = FVector::ZeroVector;
		BuildingProxy->SelectionWeight = 1.0;
		BuildingProxy->bCalibrated = false;
		BuildingDetails->ForceRefresh();
		bLoadingProxy = false;
	}
	RefreshPreview();
	return FReply::Handled();
}

FReply FDeepLevelBuildingCatalogEditorToolkit::AutoFit()
{
	if (!Catalog->Buildings.IsValidIndex(SelectedBuilding) || !BuildingPreviewViewport) return FReply::Handled();
	auto& D=Catalog->Buildings[SelectedBuilding]; FVector Center; FVector Extent;
	if (BuildingPreviewViewport->AutoFitVolume(D.BuildingClass.LoadSynchronous(), D.PlacementVolume.Rotation, Center, Extent)) { ModifyCatalog(LOCTEXT("AutoFitTx", "Auto Fit Building"), [&]{ D.PlacementVolume.Center=Center; D.PlacementVolume.Extent=Extent; D.bCalibrated=true; }); LoadBuildingProxy(); RefreshLists(); RefreshPreview(); }
	return FReply::Handled();
}

FReply FDeepLevelBuildingCatalogEditorToolkit::ResetBuilding()
{
	if (!Catalog->Buildings.IsValidIndex(SelectedBuilding)) return FReply::Handled(); auto& D=Catalog->Buildings[SelectedBuilding];
	ModifyCatalog(LOCTEXT("ResetTx", "Reset Building Calibration"), [&]{ D.PlacementVolume=FDeepLevelBuildingPlacementVolume(); D.bCalibrated=false; }); LoadBuildingProxy(); RefreshLists(); RefreshPreview(); return FReply::Handled();
}

FReply FDeepLevelBuildingCatalogEditorToolkit::AddPreset() { ModifyCatalog(LOCTEXT("AddPresetTx", "Add Building Preset"), [this]{ auto& P=Catalog->Presets.Emplace_GetRef(); P.Name=FName(*FString::Printf(TEXT("Preset_%d"), Catalog->Presets.Num())); }); SelectedPreset=Catalog->Presets.Num()-1; RefreshLists(); LoadPresetProxy(); RefreshPreview(); return FReply::Handled(); }
FReply FDeepLevelBuildingCatalogEditorToolkit::DuplicatePreset() { if(Catalog->Presets.IsValidIndex(SelectedPreset)){ const auto Copy=Catalog->Presets[SelectedPreset]; ModifyCatalog(LOCTEXT("DuplicatePresetTx", "Duplicate Building Preset"), [&]{ auto& P=Catalog->Presets.Add_GetRef(Copy); P.Name=FName(*(P.Name.ToString()+TEXT("_Copy"))); }); SelectedPreset=Catalog->Presets.Num()-1; RefreshLists(); LoadPresetProxy(); RefreshPreview(); } return FReply::Handled(); }
FReply FDeepLevelBuildingCatalogEditorToolkit::RemovePreset() { if(Catalog->Presets.IsValidIndex(SelectedPreset)){ ModifyCatalog(LOCTEXT("RemovePresetTx", "Remove Building Preset"), [this]{ Catalog->Presets.RemoveAt(SelectedPreset); }); SelectedPreset=Catalog->Presets.IsEmpty()?INDEX_NONE:FMath::Clamp(SelectedPreset,0,Catalog->Presets.Num()-1); if (SelectedPreset == INDEX_NONE) { PresetProxy->Name = NAME_None; PresetProxy->SelectionWeight = 1.0f; PresetProxy->Buildings.Reset(); PresetDetails->ForceRefresh(); } else { LoadPresetProxy(); } RefreshLists(); RefreshPreview(); } return FReply::Handled(); }
FReply FDeepLevelBuildingCatalogEditorToolkit::AddPaletteBuilding() { if(Catalog->Presets.IsValidIndex(SelectedPreset)&&Catalog->Buildings.IsValidIndex(SelectedPaletteBuilding)){ ModifyCatalog(LOCTEXT("AddSequenceTx", "Add Building To Preset"), [this]{ Catalog->Presets[SelectedPreset].Buildings.Add(Catalog->Buildings[SelectedPaletteBuilding].BuildingClass); }); LoadPresetProxy(); RefreshPreview(); } return FReply::Handled(); }
FReply FDeepLevelBuildingCatalogEditorToolkit::RemovePresetBuilding() { if(Catalog->Presets.IsValidIndex(SelectedPreset)&&Catalog->Presets[SelectedPreset].Buildings.IsValidIndex(SelectedPresetBuilding)){ ModifyCatalog(LOCTEXT("RemoveSequenceTx", "Remove Building From Preset"), [this]{ Catalog->Presets[SelectedPreset].Buildings.RemoveAt(SelectedPresetBuilding); }); SelectedPresetBuilding=INDEX_NONE; LoadPresetProxy(); RefreshPreview(); } return FReply::Handled(); }
FReply FDeepLevelBuildingCatalogEditorToolkit::MovePresetBuilding(int32 Delta) { if(Catalog->Presets.IsValidIndex(SelectedPreset)){ auto& Items=Catalog->Presets[SelectedPreset].Buildings; const int32 Target=SelectedPresetBuilding+Delta; if(Items.IsValidIndex(SelectedPresetBuilding)&&Items.IsValidIndex(Target)){ ModifyCatalog(LOCTEXT("MoveSequenceTx", "Reorder Building Preset"), [&]{ Items.Swap(SelectedPresetBuilding,Target); }); SelectedPresetBuilding=Target; LoadPresetProxy(); RefreshPreview(); } } return FReply::Handled(); }
FReply FDeepLevelBuildingCatalogEditorToolkit::EditPresetBuilding() { if(Catalog->Presets.IsValidIndex(SelectedPreset)&&Catalog->Presets[SelectedPreset].Buildings.IsValidIndex(SelectedPresetBuilding)){ const auto Class=Catalog->Presets[SelectedPreset].Buildings[SelectedPresetBuilding]; const int32 Index=Catalog->Buildings.IndexOfByPredicate([&](const auto& D){return D.BuildingClass==Class;}); if(Index!=INDEX_NONE){ Section=ESection::Buildings; SectionSwitcher->SetActiveWidgetIndex(0); SelectBuilding(Index); } } return FReply::Handled(); }

FText FDeepLevelBuildingCatalogEditorToolkit::GetPresetSummary() const
{
	if (!Catalog) return LOCTEXT("NoCatalog", "No catalog");
	if(!Catalog->Presets.IsValidIndex(SelectedPreset)) return LOCTEXT("NoPreset", "No preset selected");
	double Total=0;
	int32 ElementIndex=0;
	for(const auto& C:Catalog->Presets[SelectedPreset].Buildings)
	{
		if(const auto* D=Catalog->Buildings.FindByPredicate([&](const auto& X){return X.BuildingClass==C;}))
		{
			EDeepLevelBuildingVolumeFace Face;
			const int32 FaceSeed = FDeepLevelBuildingPlacementGeometry::MakeStreetFaceSeed(
				PresetProxy ? PresetProxy->PreviewSeed : 1337,
				0,
				Catalog->Buildings.Num() + SelectedPreset,
				ElementIndex++);
			if(FDeepLevelBuildingPlacementGeometry::ResolveStreetFace(*D, FaceSeed, Face)) Total+=FDeepLevelBuildingPlacementGeometry::ResolveGeometry(*D,Face).HalfWidth*2.0;
		}
	}
	return FText::Format(LOCTEXT("PresetSummary", "{0} actors | {1} cm"), FText::AsNumber(Catalog->Presets[SelectedPreset].Buildings.Num()), FText::AsNumber(Total));
}

FText FDeepLevelBuildingCatalogEditorToolkit::GetValidationText() const
{
	return CachedValidationText;
}

void FDeepLevelBuildingCatalogEditorToolkit::RefreshValidation()
{
	if (!Catalog)
	{
		CachedValidationText = LOCTEXT("NoCatalogValidation", "Catalog unavailable");
		return;
	}

	FText ValidationError;
	CachedValidationText = Catalog->ValidateForGeneration(ValidationError)
		? LOCTEXT("CatalogReady", "Ready for generation")
		: ValidationError;
}

void FDeepLevelBuildingCatalogEditorToolkit::RefreshPreview() { if(!Catalog) return; if(Section==ESection::Buildings && BuildingPreviewViewport.IsValid()) { BuildingPreviewViewport->SetPreviewStreetFace(GetPreviewStreetFace()); BuildingPreviewViewport->SetGuideVisibility(BuildingProxy ? BuildingProxy->SideGuides : EDeepLevelPreviewGuideVisibility::Visible, BuildingProxy ? BuildingProxy->GroundGuide : EDeepLevelPreviewGuideVisibility::Visible); BuildingPreviewViewport->PreviewBuilding(*Catalog,SelectedBuilding); } else if(Section==ESection::Presets && PresetPreviewViewport.IsValid()) PresetPreviewViewport->PreviewPreset(*Catalog,SelectedPreset,PresetProxy ? PresetProxy->PreviewSeed : 1337); }
double FDeepLevelBuildingCatalogEditorToolkit::GetPreviewStride() const { const auto& Viewport=Section==ESection::Buildings?BuildingPreviewViewport:PresetPreviewViewport; return Viewport.IsValid()?Viewport->GetGuideStride():0.0; }
FVector FDeepLevelBuildingCatalogEditorToolkit::GetPreviewWidgetLocation() const { const auto& Viewport=Section==ESection::Buildings?BuildingPreviewViewport:PresetPreviewViewport; return Viewport.IsValid()?Viewport->GetVolumePivotWorld():FVector::ZeroVector; }
EDeepLevelBuildingVolumeFace FDeepLevelBuildingCatalogEditorToolkit::GetPreviewStreetFace() const { return BuildingProxy ? BuildingProxy->PreviewStreetFace : EDeepLevelBuildingVolumeFace::NegativeY; }
void FDeepLevelBuildingCatalogEditorToolkit::ApplyPreviewWidgetDelta(const FVector& Drag, const FRotator& Rotation, const FVector& Scale, UE::Widget::EWidgetMode InWidgetMode)
{
	if(!Catalog || Section!=ESection::Buildings || !Catalog->Buildings.IsValidIndex(SelectedBuilding)) return;
	ModifyCatalog(LOCTEXT("TransformVolumeTx", "Transform Building Placement Volume"), [this,&Drag,&Rotation,&Scale,InWidgetMode]
	{
		auto& Volume=Catalog->Buildings[SelectedBuilding].PlacementVolume;
		if(InWidgetMode==UE::Widget::WM_Translate) Volume.Center+=Drag;
		else if(InWidgetMode==UE::Widget::WM_Rotate)
		{
			FVector Pivot = Volume.Center + Volume.Rotation.Quaternion() * FVector(0, 0, -Volume.Extent.Z);
			FQuat NewRot = Rotation.Quaternion() * Volume.Rotation.Quaternion();
			Volume.Rotation = NewRot.Rotator();
			Volume.Center = Pivot - NewRot * FVector(0, 0, -Volume.Extent.Z);
		}
		else if(InWidgetMode==UE::Widget::WM_Scale)
		{
			FVector Pivot = Volume.Center + Volume.Rotation.Quaternion() * FVector(0, 0, -Volume.Extent.Z);
			Volume.Extent=(Volume.Extent+Volume.Extent*Scale).ComponentMax(FVector(1.0));
			Volume.Center = Pivot - Volume.Rotation.Quaternion() * FVector(0, 0, -Volume.Extent.Z);
		}
		Catalog->Buildings[SelectedBuilding].bCalibrated = true;
	});
	LoadBuildingProxy(); RefreshPreview();
}

void FDeepLevelBuildingCatalogEditorToolkit::CycleExposureRule(EDeepLevelBuildingVolumeFace Face)
{
	if(!Catalog || Section!=ESection::Buildings || !Catalog->Buildings.IsValidIndex(SelectedBuilding)) return;
	ModifyCatalog(LOCTEXT("CycleExposureTx", "Cycle Building Exposure Rule"), [this, Face]
	{
		auto& Exposure = Catalog->Buildings[SelectedBuilding].PlacementVolume.Exposure;
		auto Cycle = [](EDeepLevelStreetExposureRule& Rule) {
			switch (Rule) {
			case EDeepLevelStreetExposureRule::Forbidden: Rule = EDeepLevelStreetExposureRule::Neutral; break;
			case EDeepLevelStreetExposureRule::Neutral: Rule = EDeepLevelStreetExposureRule::Preferred; break;
			case EDeepLevelStreetExposureRule::Preferred: Rule = EDeepLevelStreetExposureRule::Required; break;
			case EDeepLevelStreetExposureRule::Required: Rule = EDeepLevelStreetExposureRule::Forbidden; break;
			}
		};
		if (Face == EDeepLevelBuildingVolumeFace::PositiveX) Cycle(Exposure.PositiveX);
		else if (Face == EDeepLevelBuildingVolumeFace::NegativeX) Cycle(Exposure.NegativeX);
		else if (Face == EDeepLevelBuildingVolumeFace::PositiveY) Cycle(Exposure.PositiveY);
		else if (Face == EDeepLevelBuildingVolumeFace::NegativeY) Cycle(Exposure.NegativeY);
	});
	LoadBuildingProxy(); RefreshPreview();
}

void FDeepLevelBuildingCatalogEditorToolkit::BeginPreviewFaceDrag()
{
	if (FaceDragTransaction || !Catalog || !Catalog->Buildings.IsValidIndex(SelectedBuilding))
	{
		return;
	}

	FaceDragTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("ResizeVolumeFaceTx", "Resize Building Placement Volume Face"));
	Catalog->Modify();
}

void FDeepLevelBuildingCatalogEditorToolkit::ResizePreviewFace(const EDeepLevelBuildingVolumeFace Face, const double Distance)
{
	if (!FaceDragTransaction || !Catalog || !Catalog->Buildings.IsValidIndex(SelectedBuilding)
		|| FMath::IsNearlyZero(Distance, UE_DOUBLE_KINDA_SMALL_NUMBER))
	{
		return;
	}

	FDeepLevelBuildingPlacementDefinition& Definition = Catalog->Buildings[SelectedBuilding];
	if (!FDeepLevelBuildingPlacementGeometry::ResizeVolumeFace(Definition.PlacementVolume, Face, Distance))
	{
		return;
	}

	Definition.bCalibrated = true;
	Catalog->MarkPackageDirty();
	RefreshValidation();
	LoadBuildingProxy();
	RefreshPreview();
}

void FDeepLevelBuildingCatalogEditorToolkit::EndPreviewFaceDrag()
{
	FaceDragTransaction.Reset();
}

#undef LOCTEXT_NAMESPACE
