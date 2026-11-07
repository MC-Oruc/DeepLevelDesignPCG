#pragma once

#include "City/DeepLevelCityDecoration.h"
#include "Widgets/SLeafWidget.h"

/** Read-only plan view of the native city snapshot and resolved decoration output. */
class SDeepLevelCityStreetMap final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SDeepLevelCityStreetMap) {}
        SLATE_EVENT(TDelegate<void(FGuid)>, OnRuleSelected)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    void SetData(TSharedPtr<const FDeepLevelCityLayoutSnapshot> InSnapshot, TArray<FDeepLevelCityResolvedDecoration> InPlacements, bool bFrame);
    void Frame();
    void SetShowAnchors(bool bValue);
    void SelectRule(FGuid Id);
    void SetFocusedRules(const TSet<FGuid>& Rules) { FocusedRules=Rules; Invalidate(EInvalidateWidgetReason::Paint); }
    FGuid PickRule(FVector2D Local, FVector2D Size) const;
    FVector2D WorldToMap(FVector2D World, FVector2D Size) const;
    FVector2D MapToWorld(FVector2D Local, FVector2D Size) const;
    int32 GetPlacementCount() const { return Placements.Num(); }
    int32 GetCellRunCount() const { return Runs.Num(); }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(500, 400); }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&, FSlateWindowElementList&, int32, const FWidgetStyle&, bool) const override;
    virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override;
    virtual FReply OnMouseMove(const FGeometry&, const FPointerEvent&) override;
    virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent&) override;
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent&) override;
private:
    struct FRun { FBox2D Bounds; uint8 Kind; };
    double Scale(FVector2D Size) const;
    TSharedPtr<const FDeepLevelCityLayoutSnapshot> Snapshot;
    TArray<FRun> Runs;
    TArray<FDeepLevelCityResolvedDecoration> Placements;
    FBox2D Bounds{ForceInit};
    FVector2D Center = FVector2D::ZeroVector;
    double Zoom = 1;
    bool bShowAnchors = false;
    FGuid SelectedRule;
    TSet<FGuid> FocusedRules;
    TDelegate<void(FGuid)> RuleSelected;
};
