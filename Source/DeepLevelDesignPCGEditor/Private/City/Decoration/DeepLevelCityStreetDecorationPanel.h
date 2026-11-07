#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SComboBox.h"

class FDeepLevelCityBuildingDecorationEditor;
class SDeepLevelCityStreetMap;
class SDeepLevelCityStreetViewport;
class SWidgetSwitcher;
class SDockTab;
class ADeepLevelCityLayoutActor;
class IDetailsView;
class UDeepLevelCityDecorationCategory;
struct FAssetData;

/** Edits the actual shared category assets; the city document owns set membership. */
class SDeepLevelCityStreetDecorationPanel final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDeepLevelCityStreetDecorationPanel) {}
        SLATE_ARGUMENT(TSharedPtr<FDeepLevelCityBuildingDecorationEditor>, Editor)
        SLATE_ARGUMENT(TWeakPtr<SDockTab>, ParentTab)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
private:
    friend class FDeepLevelCityStreetDecorationPanelTest;
    friend class FDeepLevelStreetGroupFocusTest;
    using FCategoryItem = TSharedPtr<TWeakObjectPtr<UDeepLevelCityDecorationCategory>>;
    void Refresh();
    void SelectCategory(FCategoryItem Item, ESelectInfo::Type);
    void UpdateFocus();
    void SetNightPreference(bool bValue);
    bool bRefreshingSelection = false;
    bool bNightPreview = true;
    void LinkCategory(UDeepLevelCityDecorationCategory* Category);
    FReply CreateCategory();
    FReply UnlinkCategory();
    void ValidateSelection();
    void RefreshRoadSources();
    void RefreshRoadPreview(bool bFrame = false, bool bRefreshSnapshot = false);
    void SelectRule(FGuid Id);
    using FCityItem = TSharedPtr<TWeakObjectPtr<ADeepLevelCityLayoutActor>>;
    TSharedRef<ITableRow> MakeRow(FCategoryItem Item, const TSharedRef<STableViewBase>& Owner);

    TWeakPtr<FDeepLevelCityBuildingDecorationEditor> Editor;
    TArray<FCategoryItem> Categories;
    TWeakObjectPtr<UDeepLevelCityDecorationCategory> Selected;
    TSharedPtr<SListView<FCategoryItem>> List;
    TSharedPtr<IDetailsView> Details;
    FText ValidationError;
    FText RoadStatus;
    FText RuleStatus;
    TArray<FCityItem> Cities;
    TWeakObjectPtr<ADeepLevelCityLayoutActor> City;
    TSharedPtr<SComboBox<FCityItem>> CityPicker;
    TSharedPtr<SDeepLevelCityStreetMap> Map;
    bool bRefreshingSources = false;
    TSharedPtr<SDeepLevelCityStreetViewport> View3D;
    TSharedPtr<SWidgetSwitcher> ViewSwitcher;
    TWeakPtr<SDockTab> ParentTab;
};
