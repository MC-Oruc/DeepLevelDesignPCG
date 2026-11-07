#include "City/Decoration/DeepLevelCityStreetDecorationPanel.h"
#include "City/Decoration/DeepLevelCityBuildingDecorationEditor.h"
#include "City/Decoration/DeepLevelCityStreetMap.h"
#include "City/Decoration/DeepLevelCityStreetViewport.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "PropertyPath.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/Input/SCheckBox.h"
#include "AssetRegistry/AssetData.h"
#include "AssetToolsModule.h"
#include "Factories/DataAssetFactory.h"
#include "IDetailsView.h"
#include "Misc/PackageName.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DeepLevelCityStreetDecoration"

void FDeepLevelCityBuildingDecorationEditor::OpenStreetDecorations()
{
    GetTabManager()->TryInvokeTab(StreetsTab);
}

TSharedRef<SDockTab> FDeepLevelCityBuildingDecorationEditor::SpawnStreets(const FSpawnTabArgs&)
{
    auto Tab = SNew(SDockTab).TabRole(ETabRole::PanelTab).CanEverClose(false);
    Tab->SetContent(SNew(SDeepLevelCityStreetDecorationPanel).Editor(SharedThis(this)).ParentTab(Tab));
    return Tab;
}

void SDeepLevelCityStreetDecorationPanel::Construct(const FArguments& Args)
{
    Editor = Args._Editor;
    ParentTab = Args._ParentTab;
    bool SavedNight = true;
    if (GConfig->GetBool(TEXT("DeepLevelDesignPCG.DecorationPreview"),TEXT("NightLighting"),SavedNight,GEditorPerProjectIni)) bNightPreview=SavedNight;
    FDetailsViewArgs DetailsArgs;
    DetailsArgs.bAllowSearch = true;
    DetailsArgs.bHideSelectionTip = true;
    Details = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(DetailsArgs);
    Details->OnFinishedChangingProperties().AddSPLambda(this, [this](const FPropertyChangedEvent&) { ValidateSelection(); RefreshRoadPreview(); });
    SAssignNew(Map, SDeepLevelCityStreetMap).OnRuleSelected(TDelegate<void(FGuid)>::CreateSP(this, &SDeepLevelCityStreetDecorationPanel::SelectRule));
    TWeakPtr<SDeepLevelCityStreetDecorationPanel> Weak = SharedThis(this);
    SAssignNew(View3D, SDeepLevelCityStreetViewport)
        .IsActive_Lambda([Weak] { auto P = Weak.Pin(); return P && (!P->ParentTab.IsValid() || P->ParentTab.Pin()->IsForeground()) && (!P->ViewSwitcher || P->ViewSwitcher->GetActiveWidgetIndex() == 0); })
        .OnRuleSelected(TDelegate<void(FGuid)>::CreateSP(this, &SDeepLevelCityStreetDecorationPanel::SelectRule));
    View3D->SetNight(bNightPreview);
    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(8)
        [SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("Help", "Edit the city's shared decoration categories here: street furniture, sidewalk props, road markings and decals. Anchor tags choose where entries appear; probability, spacing and cadence control density. The map uses the selected City Layout road data and the native decoration resolver. Preview changes do not modify the level. Save assets, then regenerate city decoration to update the map."))]
        + SVerticalBox::Slot().FillHeight(1)
        [
            SNew(SSplitter)
            + SSplitter::Slot().Value(.23f)
            [
                SNew(SBorder).Padding(6)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(2)
                    [SNew(STextBlock).Text(LOCTEXT("Categories", "City decoration categories"))]
                    + SVerticalBox::Slot().AutoHeight().Padding(2)
                    [
                        SNew(SObjectPropertyEntryBox).AllowedClass(UDeepLevelCityDecorationCategory::StaticClass())
                        .OnObjectChanged_Lambda([this](const FAssetData& Asset) { LinkCategory(Cast<UDeepLevelCityDecorationCategory>(Asset.GetAsset())); })
                        .DisplayUseSelected(true).DisplayBrowse(true)
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(2)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1)
                        [SNew(SButton).Text(LOCTEXT("Create", "New category")).OnClicked(this, &SDeepLevelCityStreetDecorationPanel::CreateCategory)]
                        + SHorizontalBox::Slot().FillWidth(1).Padding(4, 0, 0, 0)
                        [SNew(SButton).Text(LOCTEXT("Unlink", "Unlink category"))
                            .ToolTipText(LOCTEXT("UnlinkTip", "Remove this category from this city set. The asset is preserved."))
                            .IsEnabled_Lambda([this] { return Selected.IsValid(); })
                            .OnClicked(this, &SDeepLevelCityStreetDecorationPanel::UnlinkCategory)]
                    ]
                    + SVerticalBox::Slot().FillHeight(1).Padding(2)
                    [SAssignNew(List, SListView<FCategoryItem>).ListItemsSource(&Categories)
                        .SelectionMode(ESelectionMode::Multi)
                        .OnGenerateRow(this, &SDeepLevelCityStreetDecorationPanel::MakeRow)
                        .OnSelectionChanged(this, &SDeepLevelCityStreetDecorationPanel::SelectCategory)]
                ]
            ]
            + SSplitter::Slot().Value(.49f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SAssignNew(CityPicker, SComboBox<FCityItem>).OptionsSource(&Cities)
                    .OnGenerateWidget_Lambda([](FCityItem Item) -> TSharedRef<SWidget>
                    {
                        return SNew(STextBlock).Text(Item && Item->IsValid() ? FText::FromString(Item->Get()->GetActorLabel()) : FText::GetEmpty());
                    })
                    .OnSelectionChanged_Lambda([this](FCityItem Item, ESelectInfo::Type)
                    {
                        if (bRefreshingSources) return;
                        City = Item ? Item->Get() : nullptr;
                        RefreshRoadPreview(true);
                    })
                    [SNew(STextBlock).Text_Lambda([this] { return City.IsValid() ? FText::FromString(City->GetActorLabel()) : LOCTEXT("ChooseCity", "Choose a City Layout in the open map"); })]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(4)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()
                    [SNew(SButton).Text(LOCTEXT("RefreshRoad", "Refresh road data"))
                        .OnClicked_Lambda([this] { RefreshRoadSources(); RefreshRoadPreview(false, true); return FReply::Handled(); })]
                    + SHorizontalBox::Slot().AutoWidth().Padding(4,0)
                    [SNew(SButton).Text(LOCTEXT("FrameRoad", "Frame all (F)"))
                        .OnClicked_Lambda([this] { Map->Frame(); View3D->Frame(); return FReply::Handled(); })]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [SNew(SCheckBox).OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Map->SetShowAnchors(State == ECheckBoxState::Checked); })
                        [SNew(STextBlock).Text(LOCTEXT("Anchors", "Anchors"))]]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(4)
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("View3D","3D viewport")).OnClicked_Lambda([this] { ViewSwitcher->SetActiveWidgetIndex(0); return FReply::Handled(); })]
                    + SHorizontalBox::Slot().AutoWidth().Padding(4,0)[SNew(SButton).Text(LOCTEXT("ViewPlan","Plan map")).OnClicked_Lambda([this] { ViewSwitcher->SetActiveWidgetIndex(1); return FReply::Handled(); })]
                    + SHorizontalBox::Slot().AutoWidth().Padding(4,0)
                    [SNew(SCheckBox).Style(FAppStyle::Get(),"ToggleButtonCheckbox").IsChecked_Lambda([this]{return bNightPreview?ECheckBoxState::Unchecked:ECheckBoxState::Checked;})
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State){if(State==ECheckBoxState::Checked)SetNightPreference(false);})[SNew(STextBlock).Text(LOCTEXT("DayLighting","Daylight"))]]
                    + SHorizontalBox::Slot().AutoWidth()
                    [SNew(SCheckBox).Style(FAppStyle::Get(),"ToggleButtonCheckbox").IsChecked_Lambda([this]{return bNightPreview?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State){if(State==ECheckBoxState::Checked)SetNightPreference(true);})[SNew(STextBlock).Text(LOCTEXT("NightLighting","Nightlight"))]]
                    + SHorizontalBox::Slot().AutoWidth().Padding(4,0)
                    [SNew(SButton).Text(LOCTEXT("ClearGroupFocus","Clear selection")).OnClicked_Lambda([this]{List->ClearSelection();SelectCategory({},ESelectInfo::Direct);return FReply::Handled();})]]
                + SVerticalBox::Slot().FillHeight(1)
                [SAssignNew(ViewSwitcher,SWidgetSwitcher).WidgetIndex(0)
                    + SWidgetSwitcher::Slot()[View3D.ToSharedRef()]
                    + SWidgetSwitcher::Slot()[Map.ToSharedRef()]]
                + SVerticalBox::Slot().AutoHeight().Padding(4)
                [SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("Legend", "Dark: roads | Gray: sidewalks | Green: placements | Yellow: selected rule. Wheel: zoom | Right/middle drag: pan | Click: focus group/rule | Ctrl/Shift: select groups | Clear selection: show all"))]
                + SVerticalBox::Slot().AutoHeight().Padding(4)
                [SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] { return RoadStatus; })]
                + SVerticalBox::Slot().AutoHeight().Padding(4)
                [SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] { return RuleStatus; })]
            ]
            + SSplitter::Slot().Value(.28f)
            [Details.ToSharedRef()]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8)
        [SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FLinearColor(1, .45f, .15f))
            .Text_Lambda([this] { return ValidationError; })]
    ];
    if (const auto Owner = Editor.Pin())
    {
        Owner->Buildings->Document->OnChanged.AddSPLambda(this, [this](EDeepLevelCityDecorationChange Change, FGuid)
        {
            if (Change == EDeepLevelCityDecorationChange::Context || Change == EDeepLevelCityDecorationChange::Metadata
                || Change == EDeepLevelCityDecorationChange::Undo) Refresh();
        });
    }
    Refresh();
}

void SDeepLevelCityStreetDecorationPanel::Refresh()
{
    TSet<UDeepLevelCityDecorationCategory*> PreviouslySelected;
    if (List) for (const auto& Item:List->GetSelectedItems()) if (Item->IsValid()) PreviouslySelected.Add(Item->Get());
    Categories.Reset();
    const auto Owner = Editor.Pin();
    const auto* Set = Owner ? Owner->Buildings->Document->GetSet() : nullptr;
    if (Set)
    {
        for (UDeepLevelCityDecorationCategory* Category : Set->Categories)
        {
            if (!Category) continue;
            Categories.Add(MakeShared<TWeakObjectPtr<UDeepLevelCityDecorationCategory>>(Category));
            if (!Owner->GetEditingObjects().Contains(Category)) Owner->AddEditingObject(Category);
        }
    }
    List->RequestListRefresh();
    {
        TGuardValue<bool> Guard(bRefreshingSelection,true);
        List->ClearSelection();
        for(const auto& Item:Categories) if(PreviouslySelected.Contains(Item->Get())) List->SetItemSelection(Item,true);
    }
    RefreshRoadSources();
    auto* Match=Categories.FindByPredicate([this](const FCategoryItem& Item){return Item->Get()==Selected.Get();});
    const auto Selection=List->GetSelectedItems();
    Selected=Selection.IsEmpty()?nullptr:Match&&List->IsItemSelected(*Match)?(*Match)->Get():Selection[0]->Get();
    Details->SetObject(Selected.Get()); ValidateSelection();
    RefreshRoadPreview();
}

TSharedRef<ITableRow> SDeepLevelCityStreetDecorationPanel::MakeRow(FCategoryItem Item, const TSharedRef<STableViewBase>& Owner)
{
    return SNew(STableRow<FCategoryItem>, Owner).Padding(4)
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [SNew(STextBlock).Text(FText::FromName(Item->Get()->GetFName()))]
        + SVerticalBox::Slot().AutoHeight()
        [SNew(STextBlock).Text_Lambda([Item]
        {
            const auto* Category = Item->Get();
            return Category ? FText::Format(LOCTEXT("EntryCount", "{0} entries | {1}"),
                FText::AsNumber(Category->Entries.Num()), FText::FromName(Category->CategoryTag.GetTagName())) : FText::GetEmpty();
        })]
    ];
}

void SDeepLevelCityStreetDecorationPanel::SelectCategory(FCategoryItem Item, ESelectInfo::Type)
{
    if(bRefreshingSelection)return;
    const auto Selection=List->GetSelectedItems();
    Selected=Item && List->IsItemSelected(Item)?Item->Get():Selection.IsEmpty()?nullptr:Selection[0]->Get();
    Details->SetObject(Selected.Get()); ValidateSelection();
    RuleStatus=FText::GetEmpty(); Map->SelectRule({}); View3D->SelectRule({});
    UpdateFocus();
}
void SDeepLevelCityStreetDecorationPanel::UpdateFocus()
{
    TSet<FGuid> Rules;
    for(const auto& Item:List->GetSelectedItems()) if(const auto* Category=Item->Get()) for(const auto& Entry:Category->Entries) Rules.Add(Entry.EntryGuid);
    // An empty selected category must still dim populated groups.
    if(!List->GetSelectedItems().IsEmpty() && Rules.IsEmpty()) Rules.Add(FGuid());
    Map->SetFocusedRules(Rules); View3D->SetFocusedRules(Rules);
}
void SDeepLevelCityStreetDecorationPanel::SetNightPreference(bool bValue)
{
    bNightPreview=bValue;View3D->SetNight(bValue);
    GConfig->SetBool(TEXT("DeepLevelDesignPCG.DecorationPreview"),TEXT("NightLighting"),bValue,GEditorPerProjectIni);
    GConfig->Flush(false,GEditorPerProjectIni);
}

void SDeepLevelCityStreetDecorationPanel::ValidateSelection()
{
    ValidationError = FText::GetEmpty();
    if (const auto* Category = Selected.Get()) Category->Validate(ValidationError);
}

void SDeepLevelCityStreetDecorationPanel::LinkCategory(UDeepLevelCityDecorationCategory* Category)
{
    const auto Owner = Editor.Pin();
    const auto* Set = Owner ? Owner->Buildings->Document->GetSet() : nullptr;
    if (!Set || !Category) return;
    auto Linked = Set->Categories;
    Linked.AddUnique(Category);
    Selected = Category;
    Owner->Buildings->Document->SetCategories(Linked);
    if(auto* Item=Categories.FindByPredicate([Category](const FCategoryItem& Candidate){return Candidate->Get()==Category;})) List->SetSelection(*Item);
}

FReply SDeepLevelCityStreetDecorationPanel::CreateCategory()
{
    const auto Owner = Editor.Pin();
    const auto* Set = Owner ? Owner->Buildings->Document->GetSet() : nullptr;
    if (!Set) return FReply::Handled();
    auto* Factory = NewObject<UDataAssetFactory>();
    Factory->DataAssetClass = UDeepLevelCityDecorationCategory::StaticClass();
    auto* Asset = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateAssetWithDialog(
        TEXT("DA_CityDecoration"), FPackageName::GetLongPackagePath(Set->GetOutermost()->GetName()) / TEXT("Categories"),
        UDeepLevelCityDecorationCategory::StaticClass(), Factory);
    LinkCategory(Cast<UDeepLevelCityDecorationCategory>(Asset));
    return FReply::Handled();
}

FReply SDeepLevelCityStreetDecorationPanel::UnlinkCategory()
{
    const auto Owner = Editor.Pin();
    const auto* Set = Owner ? Owner->Buildings->Document->GetSet() : nullptr;
    if (!Set || !Selected.IsValid()) return FReply::Handled();
    auto Linked = Set->Categories;
    Linked.Remove(Selected.Get());
    Owner->Buildings->Document->SetCategories(Linked);
    return FReply::Handled();
}

void SDeepLevelCityStreetDecorationPanel::RefreshRoadSources()
{
    TGuardValue<bool> Guard(bRefreshingSources, true);
    Cities.Reset();
    const auto Owner = Editor.Pin();
    const auto* Set = Owner ? Owner->Buildings->Document->GetSet() : nullptr;
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (World && Set)
        for (TActorIterator<ADeepLevelCityLayoutActor> It(World); It; ++It)
            if (It->DecorationSet == Set) Cities.Add(MakeShared<TWeakObjectPtr<ADeepLevelCityLayoutActor>>(*It));
    auto* Current = Cities.FindByPredicate([this](const FCityItem& Item) { return Item->Get() == City.Get(); });
    FCityItem Item = Current ? *Current : Cities.Num() == 1 ? Cities[0] : FCityItem();
    if (City.Get() != (Item ? Item->Get() : nullptr)) Map->SetData({}, {}, true);
    City = Item ? Item->Get() : nullptr;
    CityPicker->RefreshOptions();
    CityPicker->SetSelectedItem(Item);
}

void SDeepLevelCityStreetDecorationPanel::RefreshRoadPreview(const bool bFrame, const bool bRefreshSnapshot)
{
    const auto Owner = Editor.Pin();
    const auto* Set = Owner ? Owner->Buildings->Document->GetSet() : nullptr;
    ADeepLevelCityLayoutActor* Source = City.Get();
    View3D->SetSource(Source, bRefreshSnapshot);
    if (!Source || !Set || Source->DecorationSet != Set)
    {
        View3D->SetPlacements({});
        Map->SetData({}, {}, true);
        RoadStatus = LOCTEXT("NoRoad", "Choose the City Layout that owns this decoration set. Only loaded editor-world road sources are shown.");
        return;
    }
    FText Error;
    auto Snapshot = Source->GetSnapshot();
    if (!Snapshot || bRefreshSnapshot)
    {
        TSet<FIntPoint> Dirty;
        if (!Source->RefreshSnapshot(Dirty, Error))
        {
            View3D->SetPlacements({});
        Map->SetData({}, {}, true); RoadStatus = Error; return;
        }
        Snapshot = Source->GetSnapshot();
    }
    TArray<FDeepLevelCityResolvedDecoration> Placements;
    if (!Snapshot || !FDeepLevelCityDecorationResolver::Resolve(*Snapshot, *Set, Source->DecorationSeed, Placements, Error, Source->DecorationOverrides))
    {
        View3D->SetPlacements({}); Map->SetData(Snapshot, {}, bFrame); RoadStatus = Error; return;
    }
    TSet<FGuid> EntryIds;
    for (const auto& Item:Categories) if (const auto* Category=Item->Get()) for (const auto& Entry : Category->Entries) EntryIds.Add(Entry.EntryGuid);
    Placements.RemoveAll([&](const auto& Placement) { return !EntryIds.Contains(Placement.EntryGuid); });
    const bool bFirstFrame = Map->GetCellRunCount() == 0;
    View3D->SetPlacements(Placements);
    if (bFrame) View3D->Frame();
    Map->SetData(Snapshot, MoveTemp(Placements), bFrame || bFirstFrame);
    UpdateFocus();
    RoadStatus = FText::Format(LOCTEXT("RoadCounts", "{0} cells | {1} anchors | {2} placements across all categories"),
        FText::AsNumber(Snapshot->GetCells().Num()), FText::AsNumber(Snapshot->GetAnchors().Num()), FText::AsNumber(Map->GetPlacementCount()));
}

void SDeepLevelCityStreetDecorationPanel::SelectRule(const FGuid Id)
{
    FCategoryItem Group;
    for(const auto& Item:Categories) if(const auto* Category=Item->Get()) if(Category->Entries.ContainsByPredicate([Id](const auto& Entry){return Entry.EntryGuid==Id;})){Group=Item;break;}
    {
        TGuardValue<bool> Guard(bRefreshingSelection,true);
        List->ClearSelection();
        if(Group)List->SetItemSelection(Group,true);
    }
    SelectCategory(Group,ESelectInfo::Direct);
    RuleStatus = FText::GetEmpty();
    Map->SelectRule(Id);
    View3D->SelectRule(Id);
    const auto* Category = Selected.Get();
    if (!Category) return;
    const int32 Index = Category->Entries.IndexOfByPredicate([Id](const auto& Entry) { return Entry.EntryGuid == Id; });
    if (Index == INDEX_NONE) return;
    const auto& Entry = Category->Entries[Index];
    RuleStatus = FText::Format(LOCTEXT("Rule", "Rule {0}: {1} | Probability {2} | Minimum spacing {3} cm"),
        FText::AsNumber(Index), FText::FromName(Entry.PlacementSlot), FText::AsNumber(Entry.Probability), FText::AsNumber(Entry.MinimumSpacing));
    FPropertyPath Path;
    Path.AddProperty(FPropertyInfo(FindFProperty<FProperty>(UDeepLevelCityDecorationCategory::StaticClass(), GET_MEMBER_NAME_CHECKED(UDeepLevelCityDecorationCategory, Entries)), Index));
    Details->ScrollPropertyIntoView(Path, true);
    Details->HighlightProperty(Path);
}

#undef LOCTEXT_NAMESPACE
