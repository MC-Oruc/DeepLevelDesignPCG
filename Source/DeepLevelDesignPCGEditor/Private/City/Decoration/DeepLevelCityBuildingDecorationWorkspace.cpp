// Copyright <--\, Inc. All Rights Reserved.
#include "City/Decoration/DeepLevelCityBuildingDecorationEditor.h"
#include "City/Decoration/DeepLevelCityDecorationAuthoring.h"
#include "AssetRegistry/AssetData.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/GenericCommands.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "IDetailsView.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "STransformViewportToolbar.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DeepLevelCityBuildingDecorationEditor"
namespace Authoring = DeepLevelCityDecorationAuthoring;


TSharedRef<SWidget> FDeepLevelCityBuildingDecorationEditor::BuildWorkspace()
{
	return SNew(SVerticalBox)
	+ SVerticalBox::Slot().FillHeight(1)
	[
		SNew(SSplitter)
		+ SSplitter::Slot().Value(0.24f).MinSize(240)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[BuildSetup()]
			+ SVerticalBox::Slot().FillHeight(1)[BuildOutline()]
		]
		+ SSplitter::Slot().Value(0.51f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[BuildViewportControls()]
			+ SVerticalBox::Slot().FillHeight(1)[Preview.ToSharedRef()]
		]
		+ SSplitter::Slot().Value(0.25f).MinSize(240)[Inspector.ToSharedRef()]
	]
	+ SVerticalBox::Slot().AutoHeight().Padding(4)
	[
		SNew(SExpandableArea).InitiallyCollapsed(true)
		.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSPLambda(SharedThis(this), [this]
		{
			return IssueItems.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
		})))
		.HeaderContent()[SNew(STextBlock).Text(this, &FDeepLevelCityBuildingDecorationEditor::GetStatusText)]
		.BodyContent()
		[
			SNew(SBox).MaxDesiredHeight(110)
			[
				SAssignNew(IssueList, SListView<TSharedPtr<FDeepLevelCityDecorationDocumentIssue>>).ListItemsSource(&IssueItems)
				.OnGenerateRow(this, &FDeepLevelCityBuildingDecorationEditor::IssueRow)
				.OnSelectionChanged(this, &FDeepLevelCityBuildingDecorationEditor::IssueSelected)
			]
		]
	];
}

TSharedRef<SWidget> FDeepLevelCityBuildingDecorationEditor::BuildSetup()
{
	return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(4)
	[
		SNew(SExpandableArea).InitiallyCollapsed(Document->GetCatalog() != nullptr)
		.HeaderContent()[SNew(STextBlock).Text(LOCTEXT("Setup", "City setup")).Font(FAppStyle::GetFontStyle("SmallFontBold"))]
		.BodyContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(70)[SNew(STextBlock).Text(LOCTEXT("Set", "City set"))]]
				+ SHorizontalBox::Slot().FillWidth(1)[SNew(SObjectPropertyEntryBox).AllowedClass(UDeepLevelCityDecorationSet::StaticClass())
					.ObjectPath(this, &FDeepLevelCityBuildingDecorationEditor::GetSetPath).OnObjectChanged(this, &FDeepLevelCityBuildingDecorationEditor::ChooseSet)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(70)[SNew(STextBlock).Text(LOCTEXT("Catalog", "Catalog"))]]
				+ SHorizontalBox::Slot().FillWidth(1)[SNew(SObjectPropertyEntryBox).AllowedClass(UDeepLevelBuildingPlacementCatalog::StaticClass())
					.IsEnabled(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSPLambda(SharedThis(this), [this] { return Document->GetSet() != nullptr; })))
					.ObjectPath(this, &FDeepLevelCityBuildingDecorationEditor::GetCatalogPath).OnObjectChanged(this, &FDeepLevelCityBuildingDecorationEditor::ChooseCatalog)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[
				SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.Text(LOCTEXT("SetSettings", "Edit city settings"))
				.IsEnabled(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSPLambda(SharedThis(this), [this] { return Document->GetSet() != nullptr; })))
				.OnClicked(this, &FDeepLevelCityBuildingDecorationEditor::ShowSetSettings)
			]
		]
	];
}

TSharedRef<SWidget> FDeepLevelCityBuildingDecorationEditor::BuildOutline()
{
	return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(4)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("Outline", "Buildings")).Font(FAppStyle::GetFontStyle("SmallFontBold"))]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton").ToolTipText(LOCTEXT("AddVariantTip", "Add a variant to the selected building"))
				.IsEnabled(this, &FDeepLevelCityBuildingDecorationEditor::CanEdit)
				.OnClicked_Lambda([Weak = TWeakPtr<FDeepLevelCityBuildingDecorationEditor>(SharedThis(this))]
				{
					if (const auto Pinned = Weak.Pin()) { Pinned->Document->AddVariant(); }
					return FReply::Handled();
				})
				[SNew(SImage).Image(FAppStyle::GetBrush("Icons.Plus"))]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SComboButton).ButtonStyle(FAppStyle::Get(), "SimpleButton").HasDownArrow(false)
				.ToolTipText(LOCTEXT("Actions", "Selection actions"))
				.OnGetMenuContent_Lambda([Weak = TWeakPtr<FDeepLevelCityBuildingDecorationEditor>(SharedThis(this))]() -> TSharedRef<SWidget>
				{
					const auto Pinned = Weak.Pin();
					return Pinned ? Pinned->BuildSelectionMenu().ToSharedRef() : SNullWidget::NullWidget;
				})
				.ButtonContent()[SNew(SImage).Image(FAppStyle::GetBrush("Icons.Settings"))]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2)
		[
			SNew(SSearchBox).HintText(LOCTEXT("SearchOutline", "Search buildings, variants, decorations"))
			.OnTextChanged(this, &FDeepLevelCityBuildingDecorationEditor::SearchChanged)
		]
		+ SVerticalBox::Slot().FillHeight(1)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(Outline, STreeView<TSharedPtr<FDeepLevelCityDecorationOutlineItem>>).TreeItemsSource(&OutlineItems)
				.SelectionMode(ESelectionMode::Single)
				.OnKeyDownHandler(FOnKeyDown::CreateSPLambda(SharedThis(this), [this](const FGeometry&, const FKeyEvent& Key)
				{
					return Preview->GetCommandList()->ProcessCommandBindings(Key) ? FReply::Handled() : FReply::Unhandled();
				}))
				.OnGenerateRow(this, &FDeepLevelCityBuildingDecorationEditor::OutlineRow)
				.OnGetChildren(this, &FDeepLevelCityBuildingDecorationEditor::OutlineChildren)
				.OnSelectionChanged(this, &FDeepLevelCityBuildingDecorationEditor::OutlineSelected)
				.OnMouseButtonClick(this, &FDeepLevelCityBuildingDecorationEditor::OutlineClicked)
				.OnExpansionChanged(this, &FDeepLevelCityBuildingDecorationEditor::OutlineExpansionChanged)
				.OnContextMenuOpening(this, &FDeepLevelCityBuildingDecorationEditor::BuildSelectionMenu)
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(12)
			[
				SNew(STextBlock).Text(this, &FDeepLevelCityBuildingDecorationEditor::GetOutlineHint).AutoWrapText(true)
				.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSPLambda(SharedThis(this), [this]
				{
					return OutlineItems.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
				})))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(STextBlock).Text(LOCTEXT("DragHint", "Drag decorations from Content Browser onto the preview."))
			.AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
	];
}

TSharedRef<SWidget> FDeepLevelCityBuildingDecorationEditor::BuildViewportControls()
{
	const auto Toggle = [this](const FText& Label, bool* Flag, TFunction<void()> Changed)
	{
		return SNew(SCheckBox)
			.IsChecked(TAttribute<ECheckBoxState>::Create(TAttribute<ECheckBoxState>::FGetter::CreateSPLambda(SharedThis(this), [Flag]
			{
				return *Flag ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})))
			.OnCheckStateChanged(FOnCheckStateChanged::CreateSPLambda(SharedThis(this), [Flag, Changed = MoveTemp(Changed)](ECheckBoxState State)
			{
				*Flag = State == ECheckBoxState::Checked; Changed();
			}))
			[SNew(STextBlock).Text(Label)];
	};
	return SNew(SScrollBox).Orientation(Orient_Horizontal)
	+ SScrollBox::Slot()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(STransformViewportToolBar).Viewport(Preview).CommandList(Preview->GetCommandList())
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[Toggle(LOCTEXT("OBB", "Placement guide"), &bShowPlacementGuide, [this]
		{
			Preview->SetPlacementGuide(bShowPlacementGuide ? Document->GetBuildingDefinition() : nullptr);
		})]

	];
}


void FDeepLevelCityBuildingDecorationEditor::RefreshOutline()
{
	OutlineItems.Reset();
	const auto* Catalog = Document->GetCatalog();
	if (Catalog)
	{
		TSet<FSoftObjectPath> Added;
		for (const auto& Definition : Catalog->Buildings)
		{
			const auto Path = Definition.BuildingClass.ToSoftObjectPath();
			if (Path.IsNull() || Added.Contains(Path)) { continue; }
			Added.Add(Path);
			auto Building = MakeShared<FDeepLevelCityDecorationOutlineItem>();
			Building->Building = Path;
			Building->Label = FText::FromString(Path.GetAssetName());
			Building->Icon = TEXT("ClassIcon.PackedLevelActor");
			const auto* Profile = Document->GetProfileForBuilding(Definition.BuildingClass);
			Building->Detail = Profile ? FText::Format(LOCTEXT("VariantCount", "{0} variants"), FText::AsNumber(Profile->Variants.Num())) : LOCTEXT("NoProfileBadge", "Profile unavailable");
			if (Profile)
			{
				for (const auto& Variant : Profile->Variants)
				{
					auto Row = MakeShared<FDeepLevelCityDecorationOutlineItem>();
					Row->Kind = EDeepLevelCityDecorationOutlineKind::Variant;
					Row->Building = Path;
					Row->Variant = Variant.VariantGuid;
					Row->Label = FText::FromName(Variant.Name);
					Row->Detail = FText::Format(LOCTEXT("EntryCount", "{0} decorations"), FText::AsNumber(Variant.Entries.Num()));
					Row->Icon = TEXT("Icons.FolderOpen");
					for (const auto& Entry : Variant.Entries)
					{
						auto Child = MakeShared<FDeepLevelCityDecorationOutlineItem>();
						Child->Kind = EDeepLevelCityDecorationOutlineKind::Entry;
						Child->Building = Path;
						Child->Variant = Variant.VariantGuid;
						Child->Entry = Entry.EntryGuid;
						Child->Label = FText::FromName(Entry.Name);
						if (Variant.SymmetryPairs.ContainsByPredicate([&](const auto& Pair) { return Pair.First == Entry.EntryGuid || Pair.Second == Entry.EntryGuid; }))
						{
							Child->Detail = LOCTEXT("LinkedSymmetry", "Linked");
						}
						Child->Icon = Entry.Output == EDeepLevelCityDecorationOutput::Mesh ? TEXT("ClassIcon.StaticMesh")
							: Entry.Output == EDeepLevelCityDecorationOutput::Decal ? TEXT("ClassIcon.DecalActor") : TEXT("ClassIcon.Actor");
						Row->Children.Add(Child);
					}
					Building->Children.Add(Row);
				}
			}
			OutlineItems.Add(Building);
		}
	}
	if (!Search.IsEmpty())
	{
		TFunction<bool(const TSharedPtr<FDeepLevelCityDecorationOutlineItem>&)> Filter;
		Filter = [&](const auto& Item)
		{
			if (Item->Label.ToString().Contains(Search)) { return true; }
			Item->Children.RemoveAll([&](const auto& Child) { return !Filter(Child); });
			return !Item->Children.IsEmpty();
		};
		OutlineItems.RemoveAll([&](const auto& Item) { return !Filter(Item); });
	}
	if (Outline)
	{
		Outline->RequestTreeRefresh();
		TFunction<void(const TSharedPtr<FDeepLevelCityDecorationOutlineItem>&)> Expand;
		Expand = [&](const auto& Item)
		{
			Outline->SetItemExpansion(Item, !Search.IsEmpty() || ExpandedOutlineItems.Contains(Item->Key()));
			for (const auto& Child : Item->Children) { Expand(Child); }
		};
		for (const auto& Item : OutlineItems) { Expand(Item); }
		SynchronizeOutlineSelection();
	}
}


void FDeepLevelCityBuildingDecorationEditor::SynchronizeOutlineSelection()
{
	if (!Outline) { return; }
	Outline->ClearSelection();
	const auto Path = Document->GetBuildingClass().ToSoftObjectPath();
	for (const auto& Building : OutlineItems)
	{
		if (Building->Building != Path) { continue; }
		if (!Document->GetVariantId().IsValid())
		{
			Outline->SetSelection(Building, ESelectInfo::Direct);
			return;
		}
		Outline->SetItemExpansion(Building, true);
		ExpandedOutlineItems.Add(Building->Key());
		for (const auto& Variant : Building->Children)
		{
			if (Variant->Variant != Document->GetVariantId()) { continue; }
			if (!Document->GetEntryId().IsValid())
			{
				Outline->SetSelection(Variant, ESelectInfo::Direct);
				return;
			}
			Outline->SetItemExpansion(Variant, true);
			ExpandedOutlineItems.Add(Variant->Key());
			for (const auto& Entry : Variant->Children)
			{
				if (Entry->Entry == Document->GetEntryId()) { Outline->SetSelection(Entry, ESelectInfo::Direct); return; }
			}
			return;
		}
		return;
	}
}

void FDeepLevelCityBuildingDecorationEditor::SearchChanged(const FText& Text)
{
	Search = Text.ToString();
	TGuardValue<bool> Guard(bRefreshing, true);
	RefreshOutline();
}

void FDeepLevelCityBuildingDecorationEditor::OutlineClicked(TSharedPtr<FDeepLevelCityDecorationOutlineItem> Item)
{
	if (bRefreshing || !Item) { return; }
	bSetSettings = false;
	if (Item->Kind == EDeepLevelCityDecorationOutlineKind::Building)
	{
		Document->SelectBuilding(TSoftClassPtr<AActor>(Item->Building));
	}
	else
	{
		if (Document->GetBuildingClass().ToSoftObjectPath() != Item->Building
			&& !Document->SelectBuilding(TSoftClassPtr<AActor>(Item->Building))) { return; }
		if (Item->Kind == EDeepLevelCityDecorationOutlineKind::Variant || Document->GetVariantId() != Item->Variant) { Document->SelectVariant(Item->Variant); }
		if (Item->Kind == EDeepLevelCityDecorationOutlineKind::Entry) { Document->SelectEntry(Item->Entry); }
	}
	RefreshInspector();
}

void FDeepLevelCityBuildingDecorationEditor::OutlineSelected(TSharedPtr<FDeepLevelCityDecorationOutlineItem> Item, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct) { OutlineClicked(Item); }
}

void FDeepLevelCityBuildingDecorationEditor::OutlineChildren(TSharedPtr<FDeepLevelCityDecorationOutlineItem> Item,
	TArray<TSharedPtr<FDeepLevelCityDecorationOutlineItem>>& Children) const
{
	Children.Append(Item->Children);
}

void FDeepLevelCityBuildingDecorationEditor::OutlineExpansionChanged(TSharedPtr<FDeepLevelCityDecorationOutlineItem> Item, bool bExpanded)
{
	if (bRefreshing || !Search.IsEmpty()) { return; }
	if (bExpanded) { ExpandedOutlineItems.Add(Item->Key()); }
	else { ExpandedOutlineItems.Remove(Item->Key()); }
}

TSharedRef<ITableRow> FDeepLevelCityBuildingDecorationEditor::OutlineRow(TSharedPtr<FDeepLevelCityDecorationOutlineItem> Item,
	const TSharedRef<STableViewBase>& Owner)
{
	return SNew(STableRow<TSharedPtr<FDeepLevelCityDecorationOutlineItem>>, Owner).Padding(FMargin(2, 4))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 6, 0)[SNew(SImage).Image(FAppStyle::GetBrush(Item->Icon)).DesiredSizeOverride(FVector2D(16))]
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text(Item->Label).ToolTipText(FText::FromString(Item->Building.ToString()))]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)[SNew(STextBlock).Text(Item->Detail).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
	];
}

TSharedPtr<SWidget> FDeepLevelCityBuildingDecorationEditor::BuildSelectionMenu()
{
	FMenuBuilder Menu(true, Preview->GetCommandList());
	Menu.BeginSection("Building", LOCTEXT("BuildingActions", "Building"));
	Menu.AddMenuEntry(LOCTEXT("AddVariant", "Add variant"), LOCTEXT("AddVariantTip", "Add a variant to the selected building"), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"),
		FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this] { Document->AddVariant(); }), FCanExecuteAction::CreateSP(this, &FDeepLevelCityBuildingDecorationEditor::CanEdit)));
	if (CanCreateProfile())
	{
		Menu.AddMenuEntry(LOCTEXT("CreateProfile", "Create profile"), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this] { CreateProfile(); })));
		Menu.AddWidget(SNew(SObjectPropertyEntryBox).AllowedClass(UDeepLevelCityBuildingDecorationProfile::StaticClass())
			.OnObjectChanged(this, &FDeepLevelCityBuildingDecorationEditor::AttachProfile), LOCTEXT("AttachProfile", "Attach profile"));
	}
	if (CanUnlinkProfile())
	{
		Menu.AddMenuEntry(LOCTEXT("Unlink", "Unlink profile"), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this] { UnlinkProfile(); })));
	}
	Menu.EndSection();
	if (HasVariant())
	{
		Menu.BeginSection("Selection", HasEntry() ? LOCTEXT("DecorationActions", "Decoration") : LOCTEXT("VariantActions", "Variant"));
		Menu.AddMenuEntry(FGenericCommands::Get().Duplicate);
		Menu.AddMenuEntry(FGenericCommands::Get().Delete);
		if (HasEntry())
		{
			Menu.AddMenuEntry(FGenericCommands::Get().Copy);
			Menu.AddSubMenu(LOCTEXT("SymmetryMenu", "Symmetry"), LOCTEXT("SymmetryTip", "Create or manage a linked symmetric decoration"),
				FNewMenuDelegate::CreateSP(this, &FDeepLevelCityBuildingDecorationEditor::BuildSymmetryMenu));
			Menu.AddMenuEntry(LOCTEXT("MoveUp", "Move up"), FText::GetEmpty(), FSlateIcon(),
				FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this] { Document->MoveEntry(-1); })));
			Menu.AddMenuEntry(LOCTEXT("MoveDown", "Move down"), FText::GetEmpty(), FSlateIcon(),
				FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this] { Document->MoveEntry(1); })));
		}
		Menu.AddMenuEntry(FGenericCommands::Get().Paste);
		Menu.EndSection();
	}
	return Menu.MakeWidget();
}


void FDeepLevelCityBuildingDecorationEditor::BuildSymmetryMenu(FMenuBuilder& Menu)
{
	if (const auto* Pair = Document->GetSymmetryPair())
	{
		const FGuid Other = Pair->First == Document->GetEntryId() ? Pair->Second : Pair->First;
		Menu.AddMenuEntry(LOCTEXT("SelectMirror", "Select other side"), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this, Other] { SelectEntry(Other); })));
		Menu.AddMenuEntry(LOCTEXT("UnlinkSymmetry", "Unlink symmetry"), LOCTEXT("UnlinkSymmetryTip", "Keep both decorations and edit them independently"), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this] { Document->UnlinkSymmetry(); })));
		return;
	}
	const auto AddAxis = [&](const FText& Label, EAxis::Type Axis)
	{
		Menu.AddMenuEntry(Label, LOCTEXT("CreateMirrorTip", "Requires a calibrated building. Creates a linked mirror around its placement center, using its local axis."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this, Axis] { Document->CreateSymmetry(Axis); }),
				FCanExecuteAction::CreateSPLambda(SharedThis(this), [this] { return Document->CanCreateSymmetry(); })));
	};
	AddAxis(LOCTEXT("MirrorX", "Create linked mirror across X"), EAxis::X);
	AddAxis(LOCTEXT("MirrorY", "Create linked mirror across Y"), EAxis::Y);
	AddAxis(LOCTEXT("MirrorZ", "Create linked mirror across Z"), EAxis::Z);
	const auto AddExisting = [&](const FText& Label, EAxis::Type Axis)
	{
		Menu.AddSubMenu(Label, LOCTEXT("LinkExistingTip", "Link another decoration in this variant, keeping both placements as offsets. Assets can differ."),
			FNewMenuDelegate::CreateSP(this, &FDeepLevelCityBuildingDecorationEditor::BuildExistingSymmetryMenu, Axis));
	};
	AddExisting(LOCTEXT("LinkExistingX", "Link existing across X"), EAxis::X);
	AddExisting(LOCTEXT("LinkExistingY", "Link existing across Y"), EAxis::Y);
	AddExisting(LOCTEXT("LinkExistingZ", "Link existing across Z"), EAxis::Z);
}

void FDeepLevelCityBuildingDecorationEditor::BuildExistingSymmetryMenu(FMenuBuilder& Menu, EAxis::Type Axis)
{
	const auto* Variant = Document->GetVariant();
	if (!Variant) { return; }
	bool bFound = false;
	for (const auto& Entry : Variant->Entries)
	{
		const FGuid Id = Entry.EntryGuid;
		if (!Document->CanCreateSymmetry(Id)) { continue; }
		bFound = true;
		Menu.AddMenuEntry(FText::Format(LOCTEXT("LinkTarget", "{0} ({1})"), FText::FromName(Entry.Name),
			FText::FromString(Id.ToString(EGuidFormats::Digits).Left(8))), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this, Axis, Id]
			{
				if (!Document->CreateSymmetry(Axis, Id))
				{
					OperationError = LOCTEXT("CannotLinkExisting", "Cannot link these decorations. Check calibration, pairing and scale values.");
					RefreshIssues();
				}
			}), FCanExecuteAction::CreateSPLambda(SharedThis(this), [this, Id] { return Document->CanCreateSymmetry(Id); })));
	}
	if (!bFound)
	{
		Menu.AddMenuEntry(LOCTEXT("NoLinkTargets", "No unlinked decorations available"), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
	}
}

FText FDeepLevelCityBuildingDecorationEditor::GetOutlineHint() const
{
	if (!Document->GetCatalog()) { return LOCTEXT("ChooseSetup", "Choose a city set and building catalog in City setup."); }
	return Search.IsEmpty() ? LOCTEXT("EmptyCatalog", "The building catalog is empty.") : LOCTEXT("NoMatches", "No matching buildings, variants or decorations.");
}

void FDeepLevelCityBuildingDecorationEditor::IssueSelected(TSharedPtr<FDeepLevelCityDecorationDocumentIssue> Item, ESelectInfo::Type)
{
	if (bRefreshing || !Item) { return; }
	if (!Item->Building.IsNull() && Document->SelectBuilding(TSoftClassPtr<AActor>(Item->Building)))
	{
		if (Item->Variant.IsValid()) { Document->SelectVariant(Item->Variant); }
		if (Item->Entry.IsValid()) { SelectEntry(Item->Entry); }
	}
}

void FDeepLevelCityBuildingDecorationEditor::RefreshIssues()
{
	IssueItems.Reset();
	for (const auto& Issue : Document->GetIssues()) { IssueItems.Add(MakeShared<FDeepLevelCityDecorationDocumentIssue>(Issue)); }
	for (const auto& Pair : PreviewErrors)
	{
		IssueItems.Add(MakeShared<FDeepLevelCityDecorationDocumentIssue>(FDeepLevelCityDecorationDocumentIssue
			{Document->GetBuildingClass().ToSoftObjectPath(), Document->GetVariantId(), Pair.Key, Pair.Value}));
	}
	if (!OperationError.IsEmpty()) { IssueItems.Add(MakeShared<FDeepLevelCityDecorationDocumentIssue>(FDeepLevelCityDecorationDocumentIssue{{}, {}, {}, OperationError})); }
	if (IssueList) { IssueList->RequestListRefresh(); }
}
TSharedRef<ITableRow> FDeepLevelCityBuildingDecorationEditor::IssueRow(TSharedPtr<FDeepLevelCityDecorationDocumentIssue> Item, const TSharedRef<STableViewBase>& Owner)
{
	const UDeepLevelCityBuildingDecorationProfile* Profile = nullptr;
	if (const auto* Set = Document->GetSet())
	{
		for (const UDeepLevelCityBuildingDecorationProfile* Candidate : Set->BuildingProfiles)
		{
			if (Candidate && Candidate->BuildingClass.ToSoftObjectPath() == Item->Building) { Profile = Candidate; break; }
		}
	}
	const auto* Variant = Profile ? Authoring::FindVariant(*Profile, Item->Variant) : nullptr;
	const auto* Entry = Profile ? Authoring::FindEntry(*Profile, Item->Variant, Item->Entry) : nullptr;
	const FText Label = Item->Building.IsNull() ? Item->Message : FText::Format(LOCTEXT("Issue", "{0} / {1} / {2}: {3}"),
		FText::FromString(Item->Building.GetAssetName()), Variant ? FText::FromName(Variant->Name) : FText::GetEmpty(),
		Entry ? FText::FromName(Entry->Name) : FText::GetEmpty(), Item->Message);
	return SNew(STableRow<TSharedPtr<FDeepLevelCityDecorationDocumentIssue>>, Owner)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(Label).AutoWrapText(true)]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton).Text(LOCTEXT("Unlink", "Unlink profile"))
			.Visibility(Item->AssignmentIndex == INDEX_NONE ? EVisibility::Collapsed : EVisibility::Visible)
			.OnClicked(this, &FDeepLevelCityBuildingDecorationEditor::UnlinkIssue, Item)
		]
	];
}
FText FDeepLevelCityBuildingDecorationEditor::GetStatusText() const
{
	if (!IssueItems.IsEmpty()) { return FText::Format(LOCTEXT("Issues", "{0} issues - select a row to inspect"), FText::AsNumber(IssueItems.Num())); }
	if (!Document->GetProfile()) { return LOCTEXT("NoProfile", "Selected building has no profile. Create one or attach a matching profile."); }
	return LOCTEXT("Ready", "Profile linked and valid. Save assets here; regenerate the city separately.");
}
#undef LOCTEXT_NAMESPACE
