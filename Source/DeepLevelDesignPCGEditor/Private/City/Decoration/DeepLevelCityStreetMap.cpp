#include "City/Decoration/DeepLevelCityStreetMap.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"

void SDeepLevelCityStreetMap::Construct(const FArguments& Args)
{
    RuleSelected = Args._OnRuleSelected;
    SetClipping(EWidgetClipping::ClipToBounds);
}

void SDeepLevelCityStreetMap::SetData(TSharedPtr<const FDeepLevelCityLayoutSnapshot> InSnapshot,
    TArray<FDeepLevelCityResolvedDecoration> InPlacements, const bool bFrame)
{
    Snapshot = MoveTemp(InSnapshot);
    Placements = MoveTemp(InPlacements);
    Runs.Reset(); Bounds = FBox2D(ForceInit);
    if (Snapshot)
    {
        TArray<FIntPoint> Cells;
        Snapshot->GetCells().GetKeys(Cells);
        Cells.Sort([](FIntPoint A, FIntPoint B) { return A.Y == B.Y ? A.X < B.X : A.Y < B.Y; });
        FIntPoint Previous(MAX_int32, MAX_int32);
        const auto& Grid = Snapshot->GetGrid();
        for (FIntPoint Cell : Cells)
        {
            const int32 Mask = Snapshot->FindCell(Cell)->OccupancyMask;
            const uint8 Kind = (Mask & int32(EDeepLevelCityOccupancy::Road)) ? 1 :
                (Mask & int32(EDeepLevelCityOccupancy::Sidewalk)) ? 2 :
                (Mask & int32(EDeepLevelCityOccupancy::Building)) ? 3 : 0;
            if (!Kind) continue;
            const FVector2D P(Grid.CellToWorld(Cell));
            const FVector2D Half(Grid.TileSize * .5);
            const FBox2D Box(P - Half, P + Half);
            Bounds += Box;
            if (!Runs.IsEmpty() && Previous.Y == Cell.Y && Previous.X + 1 == Cell.X && Runs.Last().Kind == Kind)
                Runs.Last().Bounds += Box;
            else Runs.Add({Box, Kind});
            Previous = Cell;
        }
    }
    if (bFrame) Frame();
    Invalidate(EInvalidateWidgetReason::Paint);
}

void SDeepLevelCityStreetMap::Frame()
{
    Center = Bounds.bIsValid ? Bounds.GetCenter() : FVector2D::ZeroVector;
    Zoom = 1;
    Invalidate(EInvalidateWidgetReason::Paint);
}

void SDeepLevelCityStreetMap::SetShowAnchors(bool bValue) { bShowAnchors = bValue; Invalidate(EInvalidateWidgetReason::Paint); }
void SDeepLevelCityStreetMap::SelectRule(FGuid Id) { SelectedRule = Id; Invalidate(EInvalidateWidgetReason::Paint); }

double SDeepLevelCityStreetMap::Scale(FVector2D Size) const
{
    const FVector2D Span = Bounds.bIsValid ? Bounds.GetSize() : FVector2D(1);
    return FMath::Max(.000001, FMath::Min(FMath::Max(1., Size.X - 32) / FMath::Max(1., Span.X),
        FMath::Max(1., Size.Y - 32) / FMath::Max(1., Span.Y)) * Zoom);
}

FVector2D SDeepLevelCityStreetMap::WorldToMap(FVector2D World, FVector2D Size) const
{
    const FVector2D D = (World - Center) * Scale(Size);
    return Size * .5 + FVector2D(D.X, -D.Y);
}
FVector2D SDeepLevelCityStreetMap::MapToWorld(FVector2D Local, FVector2D Size) const
{
    const FVector2D D = (Local - Size * .5) / Scale(Size);
    return Center + FVector2D(D.X, -D.Y);
}

FGuid SDeepLevelCityStreetMap::PickRule(FVector2D Local, FVector2D Size) const
{
    double Distance = 81;
    FGuid Hit;
    for (const auto& Placement : Placements)
    {
        const double D = (WorldToMap(FVector2D(Placement.Transform.GetLocation()), Size) - Local).SizeSquared();
        if (D < Distance) { Distance = D; Hit = Placement.EntryGuid; }
    }
    return Hit;
}

int32 SDeepLevelCityStreetMap::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&,
    FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const
{
    const FVector2D Size = G.GetLocalSize();
    const auto* Brush = FAppStyle::GetBrush("WhiteBrush");
    FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(), Brush, ESlateDrawEffect::None, FLinearColor(.025f, .03f, .04f));
    const FLinearColor Colors[] = {FLinearColor::Black, FLinearColor(.12f,.15f,.19f), FLinearColor(.36f,.36f,.33f), FLinearColor(.075f,.09f,.11f)};
    for (const auto& Run : Runs)
    {
        const FVector2D P = WorldToMap(FVector2D(Run.Bounds.Min.X, Run.Bounds.Max.Y), Size);
        const FVector2D S = Run.Bounds.GetSize() * Scale(Size);
        if (P.X > Size.X || P.Y > Size.Y || P.X + S.X < 0 || P.Y + S.Y < 0) continue;
        FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2f(S), FSlateLayoutTransform(FVector2f(P))), Brush, ESlateDrawEffect::None, Colors[Run.Kind]);
    }
    auto Line = [&](FVector2D A, FVector2D B, FLinearColor Color)
    {
        TArray<FVector2D> Points{A, B};
        FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, 1);
    };
    if (Snapshot && bShowAnchors)
    {
        for (const auto& Anchor : Snapshot->GetAnchors())
        {
            const FVector P = Anchor.Transform.GetLocation();
            const FVector D = Anchor.Transform.GetUnitAxis(EAxis::X) * FMath::Max(20., Anchor.Extent.X);
            Line(WorldToMap(FVector2D(P-D),Size), WorldToMap(FVector2D(P+D),Size), FLinearColor(.2f,.55f,.7f,.6f));
        }
    }
    for (const auto& Placement : Placements)
    {
        const FVector2D P = WorldToMap(FVector2D(Placement.Transform.GetLocation()), Size);
        if (P.X < -8 || P.Y < -8 || P.X > Size.X + 8 || P.Y > Size.Y + 8) continue;
        const bool bSelected = SelectedRule.IsValid() && Placement.EntryGuid == SelectedRule;
        FLinearColor C = bSelected ? FLinearColor(1,.7f,.12f) : FLinearColor(.15f,.85f,.5f);
        if (!FocusedRules.IsEmpty() && !FocusedRules.Contains(Placement.EntryGuid)) C.A=.3f;
        const float R = bSelected ? 4 : 2.5f;
        FSlateDrawElement::MakeBox(Out, Layer + 3, G.ToPaintGeometry(FVector2f(R*2), FSlateLayoutTransform(FVector2f(P-FVector2D(R)))), Brush, ESlateDrawEffect::None, C);
        const FVector D = Placement.Transform.GetUnitAxis(EAxis::X);
        Line(P, P + FVector2D(D.X, -D.Y) * (bSelected ? 12 : 7), C);
    }
    return Layer + 3;
}

FReply SDeepLevelCityStreetMap::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& Event)
{
    if (Event.GetEffectingButton() == EKeys::RightMouseButton || Event.GetEffectingButton() == EKeys::MiddleMouseButton)
        return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
    if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        SelectedRule = PickRule(G.AbsoluteToLocal(Event.GetScreenSpacePosition()), G.GetLocalSize());
        RuleSelected.ExecuteIfBound(SelectedRule);
        Invalidate(EInvalidateWidgetReason::Paint);
        return FReply::Handled().SetUserFocus(SharedThis(this));
    }
    return FReply::Unhandled();
}
FReply SDeepLevelCityStreetMap::OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event)
{
    if (HasMouseCapture() && (Event.GetEffectingButton() == EKeys::RightMouseButton || Event.GetEffectingButton() == EKeys::MiddleMouseButton))
        return FReply::Handled().ReleaseMouseCapture();
    return FReply::Unhandled();
}
FReply SDeepLevelCityStreetMap::OnMouseMove(const FGeometry& G, const FPointerEvent& Event)
{
    if (!HasMouseCapture()) return FReply::Unhandled();
    const auto D = Event.GetCursorDelta();
    Center -= FVector2D(D.X,-D.Y) / Scale(G.GetLocalSize());
    Invalidate(EInvalidateWidgetReason::Paint);
    return FReply::Handled();
}
FReply SDeepLevelCityStreetMap::OnMouseWheel(const FGeometry& G, const FPointerEvent& Event)
{
    const FVector2D P = G.AbsoluteToLocal(Event.GetScreenSpacePosition());
    const FVector2D Before = MapToWorld(P,G.GetLocalSize());
    Zoom = FMath::Clamp(Zoom * FMath::Pow(1.2, Event.GetWheelDelta()), .1, 200.);
    Center += Before - MapToWorld(P,G.GetLocalSize());
    Invalidate(EInvalidateWidgetReason::Paint);
    return FReply::Handled();
}
FReply SDeepLevelCityStreetMap::OnKeyDown(const FGeometry&, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::F) { Frame(); return FReply::Handled(); }
    return FReply::Unhandled();
}
