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
#include "Widgets/Input/SSpinBox.h"
#include "Materials/MaterialInterface.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "STransformViewportToolbar.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DeepLevelCityBuildingDecorationEditor"
namespace Authoring = DeepLevelCityDecorationAuthoring;


TSharedRef<SWidget> FDeepLevelCityDecorationWorkspace::BuildWorkspace()
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
			+ SVerticalBox::Slot().FillHeight(1)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[Preview.ToSharedRef()]
				+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Top).Padding(10)
				[
					BuildViewportOverlay()
				]
			]
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
		.HeaderContent()[SNew(STextBlock).Text(this, &FDeepLevelCityDecorationWorkspace::GetStatusText)]
		.BodyContent()
		[
			SNew(SBox).MaxDesiredHeight(110)
			[
				SAssignNew(IssueList, SListView<TSharedPtr<FDeepLevelCityDecorationDocumentIssue>>).ListItemsSource(&IssueItems)
				.OnGenerateRow(this, &FDeepLevelCityDecorationWorkspace::IssueRow)
				.OnSelectionChanged(this, &FDeepLevelCityDecorationWorkspace::IssueSelected)
			]
		]
	];
}

TSharedRef<SWidget> FDeepLevelCityDecorationWorkspace::BuildViewportOverlay()
{
	const auto Shortcuts = SNew(SGridPanel).FillColumn(1, 1.0f);
	for (int32 Row = 0; Row < 4; ++Row)
	{
		const auto Visible = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSPLambda(SharedThis(this), [this, Row]
		{
			return GetViewportShortcut(Row).Key.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
		}));
		Shortcuts->AddSlot(0, Row).Padding(0, 0, 12, 3).VAlign(VAlign_Top)
		[
			SNew(STextBlock).Visibility(Visible)
			.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSPLambda(SharedThis(this), [this, Row] { return GetViewportShortcut(Row).Key; })))
			.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black)
			.ColorAndOpacity(FLinearColor::White)
		];
		Shortcuts->AddSlot(1, Row).Padding(0, 0, 0, 3).VAlign(VAlign_Top)
		[
			SNew(STextBlock).Visibility(Visible)
			.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSPLambda(SharedThis(this), [this, Row] { return GetViewportShortcut(Row).Action; })))
			.AutoWrapText(true).ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black)
			.ColorAndOpacity(FLinearColor::White)
		];
	}
	return SNew(SHorizontalBox).Visibility(EVisibility::HitTestInvisible)
	+ SHorizontalBox::Slot().FillWidth(0.65f).VAlign(VAlign_Top).Padding(0, 0, 20, 0)[Shortcuts]
	+ SHorizontalBox::Slot().FillWidth(0.35f).VAlign(VAlign_Top)
	[
		SNew(STextBlock).Text(this, &FDeepLevelCityDecorationWorkspace::GetViewportStatus)
		.Justification(ETextJustify::Right).AutoWrapText(true)
		.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black)
		.ColorAndOpacity(FLinearColor::White)
	];
}

TSharedRef<SWidget> FDeepLevelCityDecorationWorkspace::BuildSetup()
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
					.ObjectPath(this, &FDeepLevelCityDecorationWorkspace::GetSetPath).OnObjectChanged(this, &FDeepLevelCityDecorationWorkspace::ChooseSet)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(70)[SNew(STextBlock).Text(LOCTEXT("Catalog", "Catalog"))]]
				+ SHorizontalBox::Slot().FillWidth(1)[SNew(SObjectPropertyEntryBox).AllowedClass(UDeepLevelBuildingPlacementCatalog::StaticClass())
					.IsEnabled(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSPLambda(SharedThis(this), [this] { return Document->GetSet() != nullptr; })))
					.ObjectPath(this, &FDeepLevelCityDecorationWorkspace::GetCatalogPath).OnObjectChanged(this, &FDeepLevelCityDecorationWorkspace::ChooseCatalog)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[
				SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.Text(LOCTEXT("SetSettings", "Edit city settings"))
				.IsEnabled(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSPLambda(SharedThis(this), [this] { return Document->GetSet() != nullptr; })))
				.OnClicked(this, &FDeepLevelCityDecorationWorkspace::ShowSetSettings)
			]
		]
	];
}

TSharedRef<SWidget> FDeepLevelCityDecorationWorkspace::BuildOutline()
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
				.IsEnabled(this, &FDeepLevelCityDecorationWorkspace::CanEdit)
				.OnClicked_Lambda([Weak = TWeakPtr<FDeepLevelCityDecorationWorkspace>(SharedThis(this))]
				{
					if (const auto Pinned = Weak.Pin()) { Pinned->AddArrangement(); }
					return FReply::Handled();
				})
				[SNew(SImage).Image(FAppStyle::GetBrush("Icons.Plus"))]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SComboButton).ButtonStyle(FAppStyle::Get(), "SimpleButton").HasDownArrow(false)
				.ToolTipText(LOCTEXT("Actions", "Selection actions"))
				.OnGetMenuContent_Lambda([Weak = TWeakPtr<FDeepLevelCityDecorationWorkspace>(SharedThis(this))]() -> TSharedRef<SWidget>
				{
					const auto Pinned = Weak.Pin();
					return Pinned ? Pinned->BuildSelectionMenu().ToSharedRef() : SNullWidget::NullWidget;
				})
				.ButtonContent()[SNew(SImage).Image(FAppStyle::GetBrush("Icons.Settings"))]
			]
		]
        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(SVerticalBox)
            .Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSPLambda(SharedThis(this), [this] { return bWindowLightingMode ? EVisibility::Visible : EVisibility::Collapsed; })))
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SButton).Text(LOCTEXT("WindowAppearanceSettings", "Shared window definitions & appearance palette"))
                .OnClicked(this, &FDeepLevelCityDecorationWorkspace::ShowWindowSettings)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SButton).Text(LOCTEXT("ScanWindows", "Import / refresh whole windows"))
                .ToolTipText(LOCTEXT("ScanWindowsTip", "Import translucent glass modules from this building. Preserve saved switches and colors. Opaque walls and door glass are excluded."))
                .IsEnabled(this, &FDeepLevelCityDecorationWorkspace::CanEdit)
                .OnClicked(this, &FDeepLevelCityDecorationWorkspace::ImportWindowLighting)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("Fill", "Fill"))]
                + SHorizontalBox::Slot().FillWidth(1).Padding(4,0)[SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(.05f)
                    .Value_Lambda([this] { return WindowDensity; }).OnValueChanged_Lambda([this](float Value) { WindowDensity = Value; })]
                + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("ApplyFill", "Apply fill"))
                    .IsEnabled(this, &FDeepLevelCityDecorationWorkspace::HasVariant)
                    .OnClicked_Lambda([this] { Document->FillWindows(WindowDensity, WindowSeed); return FReply::Handled(); })]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SSpinBox<int32>).MinValue(0).MaxValue(TNumericLimits<int32>::Max()).ToolTipText(LOCTEXT("Seed", "Seed used by Apply fill. Saved window switches remain deterministic."))
                .Value_Lambda([this] { return WindowSeed; }).OnValueChanged_Lambda([this](int32 Value) { WindowSeed = Value; })]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(STextBlock).Text(LOCTEXT("WindowHelp", "Each row is a whole window module. Switch it here; select it for color and brightness. Imported transoms start off."))
                .AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
        ]
		+ SVerticalBox::Slot().AutoHeight().Padding(2)
		[
			SNew(SSearchBox).HintText(LOCTEXT("SearchOutline", "Search buildings, variants, decorations"))
			.OnTextChanged(this, &FDeepLevelCityDecorationWorkspace::SearchChanged)
		]
		+ SVerticalBox::Slot().FillHeight(1)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(Outline, STreeView<TSharedPtr<FDeepLevelCityDecorationOutlineItem>>).TreeItemsSource(&OutlineItems)
				.SelectionMode(ESelectionMode::Multi)
				.OnKeyDownHandler(FOnKeyDown::CreateSPLambda(SharedThis(this), [this](const FGeometry&, const FKeyEvent& Key)
				{
					return Preview->GetCommandList()->ProcessCommandBindings(Key) ? FReply::Handled() : FReply::Unhandled();
				}))
				.OnGenerateRow(this, &FDeepLevelCityDecorationWorkspace::OutlineRow)
				.OnGetChildren(this, &FDeepLevelCityDecorationWorkspace::OutlineChildren)
				.OnSelectionChanged(this, &FDeepLevelCityDecorationWorkspace::OutlineSelected)
				.OnExpansionChanged(this, &FDeepLevelCityDecorationWorkspace::OutlineExpansionChanged)
				.OnContextMenuOpening(this, &FDeepLevelCityDecorationWorkspace::BuildSelectionMenu)
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(12)
			[
				SNew(STextBlock).Text(this, &FDeepLevelCityDecorationWorkspace::GetOutlineHint).AutoWrapText(true)
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

TSharedRef<SWidget> FDeepLevelCityDecorationWorkspace::BuildViewportControls()
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
			Preview->SetPlacementGuide(Document->GetBuildingDefinition(), bShowPlacementGuide);
		})]

	];
}


void FDeepLevelCityDecorationWorkspace::RefreshOutline()
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
			const int32 VisibleVariants = Profile ? Profile->Variants.FilterByPredicate([this](const auto& V)
                {
                    const bool bWindows = V.SelectionGroup == FName(TEXT("NightWindows")) || V.Entries.ContainsByPredicate([](const auto& E) { return E.bWindowLight; });
                    return bWindowLightingMode ? bWindows : V.SelectionGroup != FName(TEXT("NightWindows"));
                }).Num() : 0;
                Building->Detail = Profile ? FText::Format(LOCTEXT("VariantCount", "{0} variants"), FText::AsNumber(VisibleVariants)) : LOCTEXT("NoProfileBadge", "Profile unavailable");
			if (Profile)
			{
				for (const auto& StoredVariant : Profile->Variants)
				{
                    const auto Variant = Profile->BuildVariantView(StoredVariant);
                    const bool bWindows = Variant.SelectionGroup == FName(TEXT("NightWindows")) || Variant.Entries.ContainsByPredicate([](const auto& E) { return E.bWindowLight; });
                    if (bWindowLightingMode && !bWindows) continue;
                    if (!bWindowLightingMode && Variant.SelectionGroup == FName(TEXT("NightWindows"))) continue;
					auto Row = MakeShared<FDeepLevelCityDecorationOutlineItem>();
					Row->Kind = EDeepLevelCityDecorationOutlineKind::Variant;
					Row->Building = Path;
					Row->Variant = Variant.VariantGuid;
					Row->Label = FText::FromName(Variant.Name);
					Row->Detail = FText::Format(bWindowLightingMode ? LOCTEXT("WindowStateCount", "{0} window states") : LOCTEXT("EntryCount", "{0} decorations"), FText::AsNumber(Variant.Entries.Num()));
					Row->Icon = TEXT("Icons.FolderOpen");
					for (const auto& Entry : Variant.Entries)
					{
                        if (bWindowLightingMode && !Entry.bWindowLight) continue;
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
						Child->Icon = Entry.bWindowLight ? TEXT("ClassIcon.PointLight") : Entry.Output == EDeepLevelCityDecorationOutput::Mesh ? TEXT("ClassIcon.StaticMesh")
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


void FDeepLevelCityDecorationWorkspace::SynchronizeOutlineSelection()
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
				if (Document->GetSelectedEntryIds().Contains(Entry->Entry)) { Outline->SetItemSelection(Entry, true, ESelectInfo::Direct); }
			}
			return;
		}
		return;
	}
}

void FDeepLevelCityDecorationWorkspace::SearchChanged(const FText& Text)
{
	Search = Text.ToString();
	TGuardValue<bool> Guard(bRefreshing, true);
	RefreshOutline();
}

void FDeepLevelCityDecorationWorkspace::OutlineClicked(TSharedPtr<FDeepLevelCityDecorationOutlineItem> Item)
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

void FDeepLevelCityDecorationWorkspace::OutlineSelected(TSharedPtr<FDeepLevelCityDecorationOutlineItem> Item, ESelectInfo::Type SelectInfo)
{
	if (bRefreshing || SelectInfo == ESelectInfo::Direct) { return; }
	if (Item && Item->Kind != EDeepLevelCityDecorationOutlineKind::Entry) { OutlineClicked(Item); return; }
	const FSoftObjectPath Building = Item ? Item->Building : Document->GetBuildingClass().ToSoftObjectPath();
	const FGuid Variant = Item ? Item->Variant : Document->GetVariantId();
	TArray<FGuid> Ids;
	for (const auto& Selected : Outline->GetSelectedItems())
	{
		if (Selected->Kind == EDeepLevelCityDecorationOutlineKind::Entry && Selected->Building == Building && Selected->Variant == Variant)
		{
			Ids.Add(Selected->Entry);
		}
	}
	if (Item && Ids.Remove(Item->Entry) > 0) { Ids.Add(Item->Entry); }
	bSetSettings = false;
	if (Document->GetBuildingClass().ToSoftObjectPath() != Building
		&& !Document->SelectBuilding(TSoftClassPtr<AActor>(Building))) { return; }
	if (Document->GetVariantId() != Variant) { Document->SelectVariant(Variant); }
	Document->SelectEntries(Ids);
}

void FDeepLevelCityDecorationWorkspace::OutlineChildren(TSharedPtr<FDeepLevelCityDecorationOutlineItem> Item,
	TArray<TSharedPtr<FDeepLevelCityDecorationOutlineItem>>& Children) const
{
	Children.Append(Item->Children);
}

void FDeepLevelCityDecorationWorkspace::OutlineExpansionChanged(TSharedPtr<FDeepLevelCityDecorationOutlineItem> Item, bool bExpanded)
{
	if (bRefreshing || !Search.IsEmpty()) { return; }
	if (bExpanded) { ExpandedOutlineItems.Add(Item->Key()); }
	else { ExpandedOutlineItems.Remove(Item->Key()); }
}

TSharedRef<ITableRow> FDeepLevelCityDecorationWorkspace::OutlineRow(TSharedPtr<FDeepLevelCityDecorationOutlineItem> Item,
	const TSharedRef<STableViewBase>& Owner)
{
	return SNew(STableRow<TSharedPtr<FDeepLevelCityDecorationOutlineItem>>, Owner).Padding(FMargin(2, 4))
	[
		SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [SNew(SCheckBox)
            .Visibility(Item->Kind == EDeepLevelCityDecorationOutlineKind::Entry ? EVisibility::Visible : EVisibility::Collapsed)
            .IsChecked_Lambda([this, Item]
            {
                const auto* P = Document->GetProfileForBuilding(TSoftClassPtr<AActor>(Item->Building));
                const auto* V = P ? Authoring::FindVariant(*P, Item->Variant) : nullptr;
                const auto View = V ? P->BuildVariantView(*V) : FDeepLevelCityBuildingDecorationVariant();
                const auto* E = View.Entries.FindByPredicate([&](const auto& Entry) { return Entry.EntryGuid == Item->Entry; });
                return E && E->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this, Item](ECheckBoxState State)
            {
                if (!Document->SelectBuilding(TSoftClassPtr<AActor>(Item->Building))) return;
                Document->SelectVariant(Item->Variant);
                Document->SetEntryEnabled(Item->Entry, State == ECheckBoxState::Checked);
            })]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 6, 0)[SNew(SImage).Image(FAppStyle::GetBrush(Item->Icon)).DesiredSizeOverride(FVector2D(16))]
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text(Item->Label).ToolTipText(FText::FromString(Item->Building.ToString()))]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)[SNew(STextBlock).Text(Item->Detail).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
	];
}

TSharedPtr<SWidget> FDeepLevelCityDecorationWorkspace::BuildSelectionMenu()
{
	FMenuBuilder Menu(true, Preview->GetCommandList());
	Menu.BeginSection("Building", LOCTEXT("BuildingActions", "Building"));
	Menu.AddMenuEntry(LOCTEXT("AddVariant", "Add variant"), LOCTEXT("AddVariantTip", "Add a variant to the selected building"), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"),
		FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this] { AddArrangement(); }), FCanExecuteAction::CreateSP(this, &FDeepLevelCityDecorationWorkspace::CanEdit)));
	if (CanCreateProfile())
	{
		Menu.AddMenuEntry(LOCTEXT("CreateProfile", "Create profile"), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this] { CreateProfile(); })));
		Menu.AddWidget(SNew(SObjectPropertyEntryBox).AllowedClass(UDeepLevelCityBuildingDecorationProfile::StaticClass())
			.OnObjectChanged(this, &FDeepLevelCityDecorationWorkspace::AttachProfile), LOCTEXT("AttachProfile", "Attach profile"));
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
			Menu.AddSubMenu(LOCTEXT("SymmetryMenu", "Symmetry"), LOCTEXT("SymmetryTip", "Preview mirrors or manage the selected links"),
				FNewMenuDelegate::CreateSP(this, &FDeepLevelCityDecorationWorkspace::BuildSymmetryMenu));
			if (Document->GetSelectedEntryIds().Num() == 1)
			{
				Menu.AddMenuEntry(LOCTEXT("MoveUp", "Move up"), FText::GetEmpty(), FSlateIcon(),
					FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this] { Document->MoveEntry(-1); })));
				Menu.AddMenuEntry(LOCTEXT("MoveDown", "Move down"), FText::GetEmpty(), FSlateIcon(),
					FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this] { Document->MoveEntry(1); })));
			}
		}
		Menu.AddMenuEntry(FGenericCommands::Get().Paste);
		Menu.EndSection();
	}
	return Menu.MakeWidget();
}


void FDeepLevelCityDecorationWorkspace::BuildSymmetryMenu(FMenuBuilder& Menu)
{
	if (Document->GetSelectedEntryIds().Num() == 1 || Document->IsSymmetryPivotSelected())
	{
		if (const auto* Pair = Document->GetSymmetryPair())
		{
			const FGuid Member = Pair->First;
			Menu.AddMenuEntry(LOCTEXT("SelectSymmetryCenter", "Select symmetry center"),
				LOCTEXT("SelectSymmetryCenterTip", "Transform both linked decorations around their midpoint."), FSlateIcon(),
				FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this, Member] { SelectSymmetryPivot(Member); })));
			const FGuid Other = Pair->First == Document->GetEntryId() ? Pair->Second : Pair->First;
			Menu.AddMenuEntry(LOCTEXT("SelectMirror", "Select other side"), FText::GetEmpty(), FSlateIcon(),
				FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this, Other] { SelectEntry(Other); })));
		}
	}
	const bool bHasLinks = GetSymmetryPairs().ContainsByPredicate([&](const auto& Pair)
	{ return GetSelectedEntryIds().Contains(Pair.First) || GetSelectedEntryIds().Contains(Pair.Second); });
	if (bHasLinks)
	{
		Menu.AddMenuEntry(LOCTEXT("UnlinkSymmetry", "Unlink selected decorations"),
			LOCTEXT("UnlinkSymmetryTip", "Keep the decorations and remove their selected transform links."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this] { Document->UnlinkSymmetry(); })));
		return;
	}
	Menu.AddMenuEntry(LOCTEXT("PlaceMirrorAuto", "Place mirrored copies (auto plane)"),
		LOCTEXT("PlaceMirrorAutoTip", "Move the cursor to choose a center. Ghosts preview the automatically inferred mirror plane. Click to confirm; Esc cancels."), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this] { BeginSymmetryPlacement(); }),
			FCanExecuteAction::CreateSPLambda(SharedThis(this), [this] { return Document->CanPlaceSymmetry(); })));
	if (Document->GetSelectedEntryIds().Num() == 2)
	{
		Menu.AddMenuEntry(LOCTEXT("LinkSelectedPair", "Link the two selected decorations"),
			LOCTEXT("LinkSelectedPairTip", "Keep both placements and create a symmetry center exactly between them."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this] { Document->LinkSelectedPair(); }),
				FCanExecuteAction::CreateSPLambda(SharedThis(this), [this] { return Document->CanPlaceSymmetry(); })));
	}
	const auto AddAxis = [&](const FText& Label, EAxis::Type Axis)
	{
		Menu.AddMenuEntry(Label, LOCTEXT("PlaceMirrorAxisTip", "Preview all selected decorations across a locked local axis. Click to place the center."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSPLambda(SharedThis(this), [this, Axis] { BeginSymmetryPlacement(Axis); }),
				FCanExecuteAction::CreateSPLambda(SharedThis(this), [this] { return Document->CanPlaceSymmetry(); })));
	};
	AddAxis(LOCTEXT("MirrorX", "Place mirrors across X"), EAxis::X);
	AddAxis(LOCTEXT("MirrorY", "Place mirrors across Y"), EAxis::Y);
	AddAxis(LOCTEXT("MirrorZ", "Place mirrors across Z"), EAxis::Z);
	if (Document->GetSelectedEntryIds().Num() != 1) { return; }
	const auto AddExisting = [&](const FText& Label, EAxis::Type Axis)
	{
		Menu.AddSubMenu(Label, LOCTEXT("LinkExistingTip", "Link another decoration in this variant, keeping both placements as offsets. Assets can differ."),
			FNewMenuDelegate::CreateSP(this, &FDeepLevelCityDecorationWorkspace::BuildExistingSymmetryMenu, Axis));
	};
	AddExisting(LOCTEXT("LinkExistingX", "Link existing across X"), EAxis::X);
	AddExisting(LOCTEXT("LinkExistingY", "Link existing across Y"), EAxis::Y);
	AddExisting(LOCTEXT("LinkExistingZ", "Link existing across Z"), EAxis::Z);
}

void FDeepLevelCityDecorationWorkspace::BuildExistingSymmetryMenu(FMenuBuilder& Menu, EAxis::Type Axis)
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

FText FDeepLevelCityDecorationWorkspace::GetOutlineHint() const
{
	if (!Document->GetCatalog()) { return LOCTEXT("ChooseSetup", "Choose a city set and building catalog in City setup."); }
	return Search.IsEmpty() ? LOCTEXT("EmptyCatalog", "The building catalog is empty.") : LOCTEXT("NoMatches", "No matching buildings, variants or decorations.");
}

void FDeepLevelCityDecorationWorkspace::IssueSelected(TSharedPtr<FDeepLevelCityDecorationDocumentIssue> Item, ESelectInfo::Type)
{
	if (bRefreshing || !Item) { return; }
	if (!Item->Building.IsNull() && Document->SelectBuilding(TSoftClassPtr<AActor>(Item->Building)))
	{
		if (Item->Variant.IsValid()) { Document->SelectVariant(Item->Variant); }
		if (Item->Entry.IsValid()) { SelectEntry(Item->Entry); }
	}
}

void FDeepLevelCityDecorationWorkspace::RefreshIssues()
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
TSharedRef<ITableRow> FDeepLevelCityDecorationWorkspace::IssueRow(TSharedPtr<FDeepLevelCityDecorationDocumentIssue> Item, const TSharedRef<STableViewBase>& Owner)
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
			.OnClicked(this, &FDeepLevelCityDecorationWorkspace::UnlinkIssue, Item)
		]
	];
}
FText FDeepLevelCityDecorationWorkspace::GetStatusText() const
{
	if (!IssueItems.IsEmpty()) { return FText::Format(LOCTEXT("Issues", "{0} issues - select a row to inspect"), FText::AsNumber(IssueItems.Num())); }
	if (!Document->GetProfile()) { return LOCTEXT("NoProfile", "Selected building has no profile. Create one or attach a matching profile."); }
	return LOCTEXT("Ready", "Profile linked and valid. Save assets here; regenerate the city separately.");
}
void FDeepLevelCityDecorationWorkspace::AddArrangement()
{
    Document->AddVariant();
    if (bWindowLightingMode) Document->SetVariantFields(TEXT("Window arrangement"), 1, TEXT("NightWindows"));
}

void FDeepLevelCityBuildingDecorationEditor::OpenWindowBuilding(const TSoftClassPtr<AActor>& Building)
{
    Windows->Document->SelectBuilding(Building);
    Windows->SetWindowLightingMode(true);
    GetTabManager()->TryInvokeTab(WindowsTab);
}
void FDeepLevelCityDecorationWorkspace::SetWindowLightingMode(const bool bValue)
{
    bWindowLightingMode = bValue;
    if (const auto* Profile = Document->GetProfile())
    {
        if (const auto* V = Document->GetVariant(); V && V->SelectionGroup.IsNone()) PreviewBaseVariant = V->VariantGuid;
        if (!Authoring::FindVariant(*Profile, PreviewBaseVariant))
            for (const auto& V : Profile->Variants) if (V.SelectionGroup.IsNone()) { PreviewBaseVariant = V.VariantGuid; break; }
        if (bValue)
        {
            for (const auto& V : Profile->Variants)
                if (V.SelectionGroup == FName(TEXT("NightWindows"))) { Document->SelectVariant(V.VariantGuid); break; }
        }
        else if (PreviewBaseVariant.IsValid()) Document->SelectVariant(PreviewBaseVariant);
    }
    RefreshOutline(); RefreshInspector();
    Preview->Synchronize(Document->GetBuildingClass(), Document->GetProfile(), Document->GetVariantId(), bValue ? PreviewBaseVariant : FGuid());
}
FReply FDeepLevelCityDecorationWorkspace::ImportWindowLighting()
{
    const auto* Profile = Document->GetProfile();
    FText Error;
    if (!Profile || !Document->ImportWindows(Profile->WindowMaterial.LoadSynchronous(), Error))
    { OperationError = Error; RefreshIssues(); }
    else SetWindowLightingMode(true);
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
