// Copyright <--\, Inc. All Rights Reserved.

#include "Road/DeepLevelRoadEditor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DeepLevelRoadEditor)

// ---- DeepLevelRoadNetworkActorDetails ----



#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "DeepLevelRoadNetworkActorDetails"

TSharedRef<IDetailCustomization> FDeepLevelRoadNetworkActorDetails::MakeInstance()
{
	return MakeShared<FDeepLevelRoadNetworkActorDetails>();
}

void FDeepLevelRoadNetworkActorDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	Network = Objects.Num() == 1 ? Cast<ADeepLevelRoadNetworkActor>(Objects[0].Get()) : nullptr;
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ADeepLevelRoadNetworkActor, RoadNetworkRoot));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ADeepLevelRoadNetworkActor, RoadLines));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ADeepLevelRoadNetworkActor, GeneratedRoadMeshes));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ADeepLevelRoadNetworkActor, PCGComponent));

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Deep Level Design PCG"));
	Category.AddCustomRow(LOCTEXT("AddRoadBranchSearch", "Add Road Branch"))
	.WholeRowContent()
	[
		SNew(SButton)
		.Text(LOCTEXT("AddRoadBranch", "Add Road Branch"))
		.IsEnabled_Lambda([WeakNetwork = Network]()
		{
			return WeakNetwork.IsValid() && !WeakNetwork->HasAnyFlags(RF_ClassDefaultObject);
		})
		.OnClicked(this, &FDeepLevelRoadNetworkActorDetails::AddRoadBranch)
	];
}

FReply FDeepLevelRoadNetworkActorDetails::AddRoadBranch()
{
	ADeepLevelRoadNetworkActor* NetworkActor = Network.Get();
	if (!NetworkActor)
	{
		return FReply::Handled();
	}

	FText Error;
	UDeepLevelRoadSplineComponent* NewSpline = FDeepLevelRoadSplineAuthoringService::AddBranch(*NetworkActor, Error);
	if (!NewSpline)
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return FReply::Handled();
	}

	if (GEditor)
	{
		GEditor->SelectComponent(NewSpline, true, true, true);
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE

// ---- DeepLevelRoadSplineAuthoringService ----



#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "DeepLevelRoadSplineAuthoringService"

UDeepLevelRoadSplineComponent* FDeepLevelRoadSplineAuthoringService::AddBranch(
	ADeepLevelRoadNetworkActor& Network,
	FText& OutError)
{
	OutError = FText::GetEmpty();

	const FScopedTransaction Transaction(LOCTEXT("AddRoadBranchTransaction", "Add Road Branch"));
	Network.Modify();
	UDeepLevelRoadSplineComponent* NewSpline = Network.CreateRoadBranch();
	Network.MarkPackageDirty();
	Network.NotifyRoadNetworkChanged(EDeepLevelRoadNetworkChange::Structure);
	return NewSpline;
}

#undef LOCTEXT_NAMESPACE

// ---- DeepLevelRoadEditorRefreshService ----

#include "LevelEditor.h"

FDelegateHandle FDeepLevelRoadEditorRefreshService::ChangeHandle;

void FDeepLevelRoadEditorRefreshService::Initialize()
{
	check(!ChangeHandle.IsValid());
	ChangeHandle = ADeepLevelRoadNetworkActor::OnRoadNetworkEditorChanged.AddStatic(
		&FDeepLevelRoadEditorRefreshService::HandleRoadNetworkChanged);
}

void FDeepLevelRoadEditorRefreshService::Shutdown()
{
	ADeepLevelRoadNetworkActor::OnRoadNetworkEditorChanged.Remove(ChangeHandle);
	ChangeHandle.Reset();
}

void FDeepLevelRoadEditorRefreshService::HandleRoadNetworkChanged(
	ADeepLevelRoadNetworkActor& Network,
	const EDeepLevelRoadNetworkChange Change)
{
	if (Change != EDeepLevelRoadNetworkChange::Geometry)
	{
		FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		LevelEditor.BroadcastComponentsEdited();
	}

	if (GEditor)
	{
		GEditor->NoteSelectionChange();
		GEditor->RedrawLevelEditingViewports(true);
	}
}

// ---- DeepLevelRoadTileCatalogAssetTypeActions ----


#define LOCTEXT_NAMESPACE "DeepLevelRoadTileCatalogAssetTypeActions"

FText FDeepLevelRoadTileCatalogAssetTypeActions::GetName() const
{
	return LOCTEXT("Name", "DeepLevel Road Tile Catalog");
}

UClass* FDeepLevelRoadTileCatalogAssetTypeActions::GetSupportedClass() const
{
	return UDeepLevelRoadTileCatalog::StaticClass();
}

void FDeepLevelRoadTileCatalogAssetTypeActions::OpenAssetEditor(
	const TArray<UObject*>& Objects,
	TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;
	for (UObject* Object : Objects)
	{
		if (UDeepLevelRoadTileCatalog* Catalog = Cast<UDeepLevelRoadTileCatalog>(Object))
		{
			MakeShared<FDeepLevelRoadTileCatalogEditorToolkit>()->InitEditor(Mode, EditWithinLevelEditor, Catalog);
		}
	}
}

#undef LOCTEXT_NAMESPACE

// ---- DeepLevelRoadTileCatalogEditorToolkit ----

#include "AssetRegistry/AssetData.h"
#include "ContentBrowserModule.h"
#include "Engine/StaticMesh.h"
#include "IContentBrowserSingleton.h"
#include "PropertyEditorModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "DeepLevelRoadTileCatalogEditor"

const FName FDeepLevelRoadTileCatalogEditorToolkit::WorkspaceTabId(TEXT("DeepLevelRoadTileCatalogWorkspace"));

FText GetRoadTileTypeDisplayName(const FDeepLevelRoadTileDefinition& Tile)
{
	if (Tile.IsJunctionApproach())
	{
		return LOCTEXT("JunctionApproachType", "Junction Approach");
	}
	switch (Tile.GetTopology())
	{
	case EDeepLevelRoadTileTopology::Sidewalk: return LOCTEXT("SidewalkType", "Sidewalk");
	case EDeepLevelRoadTileTopology::DeadEnd: return LOCTEXT("DeadEndType", "Dead End");
	case EDeepLevelRoadTileTopology::Straight: return LOCTEXT("StraightType", "Straight");
	case EDeepLevelRoadTileTopology::Corner: return LOCTEXT("CornerType", "Corner");
	case EDeepLevelRoadTileTopology::TJunction: return LOCTEXT("TJunctionType", "T-Junction");
	case EDeepLevelRoadTileTopology::FourWay: return LOCTEXT("FourWayType", "Four-Way Junction");
	default: return LOCTEXT("InvalidType", "Invalid");
	}
}

FDeepLevelRoadTileCatalogEditorToolkit::~FDeepLevelRoadTileCatalogEditorToolkit()
{
	PreviewViewport.Reset();
	TileList.Reset();
	DetailsView.Reset();
	Proxy = nullptr;
	Catalog = nullptr;
}

void FDeepLevelRoadTileCatalogEditorToolkit::InitEditor(
	const EToolkitMode::Type Mode,
	const TSharedPtr<IToolkitHost>& Host,
	UDeepLevelRoadTileCatalog* InCatalog)
{
	Catalog = InCatalog;
	if (!Catalog)
	{
		return;
	}
	Proxy = NewObject<UDeepLevelRoadTileEditorProxy>(GetTransientPackage(), NAME_None, RF_Transient);
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = true;
	DetailsArgs.bHideSelectionTip = true;
	DetailsView = PropertyEditor.CreateDetailView(DetailsArgs);
	DetailsView->SetObject(Proxy);
	DetailsView->OnFinishedChangingProperties().AddSP(this, &FDeepLevelRoadTileCatalogEditorToolkit::CommitProxy);
	RefreshItems();
	if (!Catalog->Tiles.IsEmpty())
	{
		SelectedTile = 0;
		LoadProxy();
	}
	RefreshValidation();

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("DeepLevel_RoadTileCatalogEditor_v1"))
		->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split(FTabManager::NewStack()->AddTab(WorkspaceTabId, ETabState::OpenedTab)->SetHideTabWell(true)));
	InitAssetEditor(Mode, Host, TEXT("DeepLevelRoadTileCatalogEditor"), Layout, true, true, Catalog);
	RegenerateMenusAndToolbars();
}

void FDeepLevelRoadTileCatalogEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
	InTabManager->RegisterTabSpawner(WorkspaceTabId, FOnSpawnTab::CreateSP(this, &FDeepLevelRoadTileCatalogEditorToolkit::SpawnWorkspaceTab))
		.SetDisplayName(LOCTEXT("Workspace", "Road Tile Calibration"));
}

void FDeepLevelRoadTileCatalogEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(WorkspaceTabId);
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

void FDeepLevelRoadTileCatalogEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Catalog);
	Collector.AddReferencedObject(Proxy);
}

FText FDeepLevelRoadTileCatalogEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "Road Tile Calibration");
}

TSharedRef<SDockTab> FDeepLevelRoadTileCatalogEditorToolkit::SpawnWorkspaceTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SWidget> Workspace = BuildWorkspace();
	RefreshPreview();
	return SNew(SDockTab).TabRole(ETabRole::PanelTab)[Workspace];
}

TSharedRef<SWidget> FDeepLevelRoadTileCatalogEditorToolkit::BuildWorkspace()
{
	SAssignNew(PreviewViewport, SDeepLevelRoadTileCatalogPreviewViewport).Toolkit(SharedThis(this));
	return SNew(SVerticalBox)
	+ SVerticalBox::Slot().AutoHeight().Padding(4)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("AddTile", "Add Tile")).OnClicked(this, &FDeepLevelRoadTileCatalogEditorToolkit::AddTile)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("RemoveTile", "Remove Tile")).OnClicked(this, &FDeepLevelRoadTileCatalogEditorToolkit::RemoveTile)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("AutoFit", "Auto Fit")).OnClicked(this, &FDeepLevelRoadTileCatalogEditorToolkit::AutoFit)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(LOCTEXT("Reset", "Reset")).OnClicked(this, &FDeepLevelRoadTileCatalogEditorToolkit::ResetTile)]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		+ SHorizontalBox::Slot().AutoWidth().Padding(6)[SNew(STextBlock).Text(this, &FDeepLevelRoadTileCatalogEditorToolkit::GetValidationText).ColorAndOpacity(FLinearColor(1.0f, 0.65f, 0.1f))]
	]
	+ SVerticalBox::Slot().AutoHeight().Padding(6, 0, 6, 4)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("PortInstructions", "Click a port to cycle CLOSED -> ROAD -> JUNCTION. No active ports means Sidewalk; any active port means Road."))
		.ColorAndOpacity(FLinearColor(0.72f, 0.72f, 0.72f))
	]
	+ SVerticalBox::Slot().FillHeight(1.0f)
	[
		SNew(SSplitter)
		+ SSplitter::Slot().Value(0.2f)
		[
			SAssignNew(TileList, SListView<TSharedPtr<int32>>)
			.ListItemsSource(&TileItems)
			.OnGenerateRow(this, &FDeepLevelRoadTileCatalogEditorToolkit::GenerateTileRow)
			.OnSelectionChanged(this, &FDeepLevelRoadTileCatalogEditorToolkit::OnSelectionChanged)
		]
		+ SSplitter::Slot().Value(0.55f)[PreviewViewport.ToSharedRef()]
		+ SSplitter::Slot().Value(0.25f)[DetailsView.ToSharedRef()]
	];
}

TSharedRef<ITableRow> FDeepLevelRoadTileCatalogEditorToolkit::GenerateTileRow(
	TSharedPtr<int32> Item,
	const TSharedRef<STableViewBase>& Owner)
{
	if (!Catalog || !Item.IsValid() || !Catalog->Tiles.IsValidIndex(*Item))
	{
		return SNew(STableRow<TSharedPtr<int32>>, Owner)[SNew(STextBlock).Text(LOCTEXT("InvalidTile", "<Invalid tile>"))];
	}
	const FDeepLevelRoadTileDefinition& Tile = Catalog->Tiles[*Item];
	const FString Label = FString::Printf(
		TEXT("%s [%s] %s"),
		Tile.bCalibrated ? TEXT("OK") : TEXT("!"),
		*GetRoadTileTypeDisplayName(Tile).ToString(),
		*Tile.TileMesh.GetAssetName());
	return SNew(STableRow<TSharedPtr<int32>>, Owner)[SNew(STextBlock).Text(FText::FromString(Label))];
}

void FDeepLevelRoadTileCatalogEditorToolkit::RefreshItems()
{
	TileItems.Reset();
	if (Catalog)
	{
		for (int32 Index = 0; Index < Catalog->Tiles.Num(); ++Index)
		{
			TileItems.Add(MakeShared<int32>(Index));
		}
	}
	if (TileList)
	{
		TileList->RequestListRefresh();
	}
}

void FDeepLevelRoadTileCatalogEditorToolkit::SelectTile(const int32 Index)
{
	SelectedTile = Index;
	LoadProxy();
	RefreshPreview();
}

void FDeepLevelRoadTileCatalogEditorToolkit::LoadProxy()
{
	if (!Catalog || !Proxy || !Catalog->Tiles.IsValidIndex(SelectedTile))
	{
		return;
	}
	bLoadingProxy = true;
	const FDeepLevelRoadTileDefinition& Tile = Catalog->Tiles[SelectedTile];
	Proxy->TileMesh = Tile.TileMesh;
	Proxy->TileMaterialOverride = Tile.TileMaterialOverride;
	Proxy->VolumeCenter = Tile.PlacementVolume.Center;
	Proxy->VolumeRotation = Tile.PlacementVolume.Rotation;
	Proxy->VolumeHeight = Tile.PlacementVolume.Extent.Z * 2.0;
	Proxy->TileSize = Tile.PlacementVolume.Extent.X * 2.0;
	Proxy->SelectionWeight = Tile.SelectionWeight;
	Proxy->bCalibrated = Tile.bCalibrated;
	DetailsView->ForceRefresh();
	bLoadingProxy = false;
}

void FDeepLevelRoadTileCatalogEditorToolkit::CommitProxy(const FPropertyChangedEvent& Event)
{
	if (bLoadingProxy || !Catalog || !Proxy || !Catalog->Tiles.IsValidIndex(SelectedTile))
	{
		return;
	}
	ModifyCatalog(LOCTEXT("EditTile", "Edit Road Tile"), [this]
	{
		FDeepLevelRoadTileDefinition& Tile = Catalog->Tiles[SelectedTile];
		Tile.TileMaterialOverride = Proxy->TileMaterialOverride;
		Tile.PlacementVolume.Center = Proxy->VolumeCenter;
		Tile.PlacementVolume.Rotation = Proxy->VolumeRotation;
		const double TileSize = Tile.PlacementVolume.Extent.X * 2.0;
		Tile.PlacementVolume.Extent = FVector(TileSize * 0.5, TileSize * 0.5, FMath::Max(Proxy->VolumeHeight * 0.5, 1.0));
		Tile.SelectionWeight = FMath::Max(Proxy->SelectionWeight, 0.01);
		Tile.bCalibrated = true;
	});
	LoadProxy();
	RefreshItems();
	RefreshPreview();
}

void FDeepLevelRoadTileCatalogEditorToolkit::RefreshPreview()
{
	if (PreviewViewport && Catalog)
	{
		const FDeepLevelRoadTileDefinition* Tile = Catalog->Tiles.IsValidIndex(SelectedTile) ? &Catalog->Tiles[SelectedTile] : nullptr;
		PreviewViewport->PreviewTile(Tile, Tile ? Tile->PlacementVolume.Extent.X * 2.0 : 0.0);
	}
}

FVector FDeepLevelRoadTileCatalogEditorToolkit::GetPreviewWidgetLocation() const
{
	return PreviewViewport ? PreviewViewport->GetVolumeCenterWorld() : FVector::ZeroVector;
}

void FDeepLevelRoadTileCatalogEditorToolkit::ApplyPreviewWidgetDelta(
	const FVector& Drag,
	const FRotator& Rotation,
	const FVector& Scale,
	const UE::Widget::EWidgetMode WidgetMode)
{
	if (!Catalog || !Catalog->Tiles.IsValidIndex(SelectedTile))
	{
		return;
	}
	ModifyCatalog(LOCTEXT("TransformVolumeTx", "Transform Road Tile Placement Volume"), [this, &Drag, &Rotation, &Scale, WidgetMode]
	{
		FDeepLevelRoadTileDefinition& Tile = Catalog->Tiles[SelectedTile];
		FDeepLevelRoadTilePlacementVolume& Volume = Tile.PlacementVolume;
		if (WidgetMode == UE::Widget::WM_Translate)
		{
			Volume.Center += Drag;
		}
		else if (WidgetMode == UE::Widget::WM_Rotate)
		{
			const FVector GroundPivot = Volume.Center + Volume.Rotation.Quaternion() * FVector(0.0, 0.0, -Volume.Extent.Z);
			const FQuat NewRotation = Rotation.Quaternion() * Volume.Rotation.Quaternion();
			Volume.Rotation = NewRotation.Rotator();
			Volume.Center = GroundPivot - NewRotation * FVector(0.0, 0.0, -Volume.Extent.Z);
		}
		else if (WidgetMode == UE::Widget::WM_Scale)
		{
			const FVector GroundPivot = Volume.Center + Volume.Rotation.Quaternion() * FVector(0.0, 0.0, -Volume.Extent.Z);
			Volume.Extent.Z = FMath::Max(Volume.Extent.Z + Volume.Extent.Z * Scale.Z, 1.0);
			Volume.Center = GroundPivot - Volume.Rotation.Quaternion() * FVector(0.0, 0.0, -Volume.Extent.Z);
		}
		Tile.bCalibrated = true;
	});
	LoadProxy();
	RefreshPreview();
}

void FDeepLevelRoadTileCatalogEditorToolkit::CycleRoadConnection(const int32 ConnectionMask)
{
	if (!Catalog || !Catalog->Tiles.IsValidIndex(SelectedTile))
	{
		return;
	}
	ModifyCatalog(LOCTEXT("CycleRoadConnectionTx", "Cycle Road Tile Connection"), [this, ConnectionMask]
	{
		FDeepLevelRoadTileDefinition& Tile = Catalog->Tiles[SelectedTile];
		const bool bConnected = (Tile.ConnectionMask & ConnectionMask) != 0;
		const bool bJunction = Tile.ApproachJunctionDirectionMask == ConnectionMask;
		if (!bConnected)
		{
			Tile.ConnectionMask |= ConnectionMask;
			return;
		}
		if (!bJunction)
		{
			Tile.ApproachJunctionDirectionMask = ConnectionMask;
			return;
		}
		Tile.ConnectionMask &= ~ConnectionMask;
		Tile.ApproachJunctionDirectionMask = 0;
	});
	LoadProxy();
	RefreshPreview();
}

void FDeepLevelRoadTileCatalogEditorToolkit::RefreshValidation()
{
	if (!Catalog || Catalog->ValidateForGeneration(ValidationText))
	{
		ValidationText = LOCTEXT("Ready", "Ready for generation");
	}
}

void FDeepLevelRoadTileCatalogEditorToolkit::ModifyCatalog(const FText& TransactionText, TFunctionRef<void()> Mutation)
{
	FScopedTransaction Transaction(TransactionText);
	Catalog->Modify();
	Mutation();
	Catalog->MarkPackageDirty();
	RefreshValidation();
}

FReply FDeepLevelRoadTileCatalogEditorToolkit::AddTile()
{
	if (!Catalog || !PreviewViewport)
	{
		return FReply::Handled();
	}
	FOpenAssetDialogConfig Dialog;
	Dialog.DialogTitleOverride = LOCTEXT("AddDialog", "Select 500x500 Road or Sidewalk Meshes");
	Dialog.DefaultPath = TEXT("/Game");
	Dialog.AssetClassNames.Add(UStaticMesh::StaticClass()->GetClassPathName());
	Dialog.bAllowMultipleSelection = true;
	const TArray<FAssetData> Assets = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"))
		.Get().CreateModalOpenAssetDialog(Dialog);
	const double NewTileSize = FDeepLevelRoadTilePlacementVolume().Extent.X * 2.0;
	for (const FAssetData& Asset : Assets)
	{
		UStaticMesh* TileMesh = Cast<UStaticMesh>(Asset.GetAsset());
		if (!TileMesh)
		{
			continue;
		}
		FVector Center;
		FVector Extent;
		if (!PreviewViewport->AutoFitVolume(TileMesh, NewTileSize, Center, Extent))
		{
			continue;
		}
		ModifyCatalog(LOCTEXT("AddTileTx", "Add Road Tile"), [this, TileMesh, Center, Extent]
		{
			FDeepLevelRoadTileDefinition& Tile = Catalog->Tiles.Emplace_GetRef();
			Tile.TileMesh = TileMesh;
			Tile.PlacementVolume.Center = Center;
			Tile.PlacementVolume.Extent = Extent;
			Tile.bCalibrated = true;
		});
	}
	SelectedTile = Catalog->Tiles.IsEmpty() ? INDEX_NONE : Catalog->Tiles.Num() - 1;
	RefreshItems();
	LoadProxy();
	RefreshPreview();
	return FReply::Handled();
}

FReply FDeepLevelRoadTileCatalogEditorToolkit::RemoveTile()
{
	if (Catalog && Catalog->Tiles.IsValidIndex(SelectedTile))
	{
		ModifyCatalog(LOCTEXT("RemoveTileTx", "Remove Road Tile"), [this] { Catalog->Tiles.RemoveAt(SelectedTile); });
		SelectedTile = Catalog->Tiles.IsEmpty() ? INDEX_NONE : FMath::Clamp(SelectedTile, 0, Catalog->Tiles.Num() - 1);
		RefreshItems();
		LoadProxy();
		RefreshPreview();
	}
	return FReply::Handled();
}

FReply FDeepLevelRoadTileCatalogEditorToolkit::AutoFit()
{
	if (Catalog && PreviewViewport && Catalog->Tiles.IsValidIndex(SelectedTile))
	{
		FDeepLevelRoadTileDefinition& Tile = Catalog->Tiles[SelectedTile];
		FVector Center;
		FVector Extent;
		if (PreviewViewport->AutoFitVolume(Tile.TileMesh.LoadSynchronous(), Tile.PlacementVolume.Extent.X * 2.0, Center, Extent))
		{
			ModifyCatalog(LOCTEXT("AutoFitTx", "Auto Fit Road Tile"), [&Tile, Center, Extent]
			{
				Tile.PlacementVolume.Center = Center;
				Tile.PlacementVolume.Extent = Extent;
				Tile.bCalibrated = true;
			});
			LoadProxy();
			RefreshPreview();
		}
	}
	return FReply::Handled();
}

FReply FDeepLevelRoadTileCatalogEditorToolkit::ResetTile()
{
	if (Catalog && Catalog->Tiles.IsValidIndex(SelectedTile))
	{
		ModifyCatalog(LOCTEXT("ResetTileTx", "Reset Road Tile Calibration"), [this]
		{
			FDeepLevelRoadTileDefinition& Tile = Catalog->Tiles[SelectedTile];
			Tile.PlacementVolume = FDeepLevelRoadTilePlacementVolume();
			const double TileSize = Tile.PlacementVolume.Extent.X * 2.0;
			Tile.PlacementVolume.Extent = FVector(TileSize * 0.5, TileSize * 0.5, 50.0);
			Tile.bCalibrated = false;
		});
		LoadProxy();
		RefreshPreview();
	}
	return FReply::Handled();
}

FText FDeepLevelRoadTileCatalogEditorToolkit::GetValidationText() const
{
	return ValidationText;
}

void FDeepLevelRoadTileCatalogEditorToolkit::OnSelectionChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo)
{
	if (Item.IsValid())
	{
		SelectTile(*Item);
	}
}

#undef LOCTEXT_NAMESPACE
