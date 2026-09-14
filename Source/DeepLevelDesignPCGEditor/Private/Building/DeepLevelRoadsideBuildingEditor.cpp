// Copyright <--\, Inc. All Rights Reserved.

#include "Building/DeepLevelBuildingEditor.h"

#include "Building/DeepLevelBuildingPCG.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "InputCoreTypes.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ScopedTransaction.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "DeepLevelRoadsideBuildingEditor"

namespace
{
void ShowFrontageNotification(const FText& Message)
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 2.5f;
	Info.bUseSuccessFailIcons = true;
	FSlateNotificationManager::Get().AddNotification(Info);
}
}

void FDeepLevelRoadsideFrontageVisualizer::DrawVisualizationHUD(
	const UActorComponent* Component,
	const FViewport* Viewport,
	const FSceneView* View,
	FCanvas* Canvas)
{
	const UDeepLevelRoadsideFrontageSplineComponent* Frontage =
		Cast<UDeepLevelRoadsideFrontageSplineComponent>(Component);
	const ADeepLevelPCGRoadsideBuildingActor* Owner = Frontage
		? Cast<ADeepLevelPCGRoadsideBuildingActor>(Frontage->GetOwner())
		: nullptr;
	if (!Owner || !Canvas || Owner->FrontageSplines.IsEmpty() || Owner->FrontageSplines[0] != Frontage)
	{
		return;
	}

	const UDeepLevelRoadsideFrontageSplineComponent* Edited =
		Cast<UDeepLevelRoadsideFrontageSplineComponent>(GetEditedSplineComponent());
	const bool bActive = Edited && Edited->GetOwner() == Owner;
	const FLinearColor StatusColor = bActive
		? FLinearColor(0.25f, 1.0f, 0.35f)
		: FLinearColor(1.0f, 0.8f, 0.25f);

	FCanvasTextItem Status(
		FVector2D(24.0f, 48.0f),
		bActive
			? LOCTEXT("FrontageEditingActive", "ROADSIDE FRONTAGE EDITING: ACTIVE")
			: LOCTEXT("FrontageEditingReady", "ROADSIDE FRONTAGE EDITING: Select a green spline or control point"),
		GEngine->GetSmallFont(),
		StatusColor);
	Status.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(Status);

	if (!bActive)
	{
		return;
	}

	const FText Kind = Edited->Kind == EDeepLevelRoadsideFrontageKind::Automatic
		? (Edited->bExcluded ? LOCTEXT("ExcludedFrontage", "Selected: Automatic (Excluded)") : LOCTEXT("AutomaticFrontage", "Selected: Automatic"))
		: (Edited->Kind == EDeepLevelRoadsideFrontageKind::Replace
			? LOCTEXT("ReplacementFrontage", "Selected: Manual Replacement")
			: LOCTEXT("AddedFrontage", "Selected: Manual Add"));
	FCanvasTextItem KindItem(FVector2D(24.0f, 68.0f), Kind, GEngine->GetSmallFont(), FLinearColor::White);
	KindItem.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(KindItem);

	float Y = 88.0f;
	if (Edited->Kind == EDeepLevelRoadsideFrontageKind::Automatic)
	{
		FCanvasTextItem AutomaticHint(
			FVector2D(24.0f, Y),
			LOCTEXT("AutomaticFrontageHint", "Shift + Right Click: Exclude / Restore | Ctrl + Shift + Left Click: Convert to Replacement"),
			GEngine->GetSmallFont(), FLinearColor(1.0f, 0.75f, 0.45f));
		AutomaticHint.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(AutomaticHint);
		Y += 20.0f;
	}
	else
	{
		FCanvasTextItem ManualHint(
			FVector2D(24.0f, Y),
			LOCTEXT("ManualFrontageHint", "Ctrl + Shift + Middle Click: Remove Manual Override"),
			GEngine->GetSmallFont(), FLinearColor(1.0f, 0.75f, 0.45f));
		ManualHint.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(ManualHint);
		Y += 20.0f;
	}

	FCanvasTextItem AddHint(
		FVector2D(24.0f, Y),
		LOCTEXT("AddFrontageHint", "Ctrl + Shift + Right Click: Duplicate as Manual Add"),
		GEngine->GetSmallFont(), FLinearColor(1.0f, 0.75f, 0.45f));
	AddHint.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(AddHint);
}

bool FDeepLevelRoadsideFrontageVisualizer::HandleInputKey(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport,
	const FKey Key,
	const EInputEvent Event)
{
	UDeepLevelRoadsideFrontageSplineComponent* Frontage =
		Cast<UDeepLevelRoadsideFrontageSplineComponent>(GetEditedSplineComponent());
	ADeepLevelPCGRoadsideBuildingActor* Owner = Frontage
		? Cast<ADeepLevelPCGRoadsideBuildingActor>(Frontage->GetOwner())
		: nullptr;
	if (!Owner || Event != IE_Pressed || !Viewport)
	{
		return FSplineComponentVisualizer::HandleInputKey(ViewportClient, Viewport, Key, Event);
	}

	const bool bShift = Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift);
	const bool bControl = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);
	if (!bShift)
	{
		return FSplineComponentVisualizer::HandleInputKey(ViewportClient, Viewport, Key, Event);
	}

	if (!bControl && Key == EKeys::RightMouseButton
		&& Frontage->Kind == EDeepLevelRoadsideFrontageKind::Automatic)
	{
		const bool bWasExcluded = Frontage->bExcluded;
		const FScopedTransaction Transaction(LOCTEXT("ToggleFrontageExclusion", "Toggle Roadside Frontage Exclusion"));
		Owner->ToggleFrontageExclusion(*Frontage);
		ShowFrontageNotification(bWasExcluded
			? LOCTEXT("FrontageRestored", "Roadside frontage restored.")
			: LOCTEXT("FrontageExcluded", "Roadside frontage excluded."));
		return true;
	}
	if (bControl && Key == EKeys::LeftMouseButton
		&& Frontage->Kind == EDeepLevelRoadsideFrontageKind::Automatic)
	{
		const FScopedTransaction Transaction(LOCTEXT("ReplaceFrontage", "Create Roadside Frontage Replacement"));
		Owner->CreateFrontageOverride(*Frontage, true);
		ShowFrontageNotification(LOCTEXT("FrontageReplaced", "Editable frontage replacement created."));
		return true;
	}
	if (bControl && Key == EKeys::RightMouseButton)
	{
		const FScopedTransaction Transaction(LOCTEXT("AddFrontage", "Add Roadside Frontage"));
		Owner->CreateFrontageOverride(*Frontage, false);
		ShowFrontageNotification(LOCTEXT("FrontageAdded", "Editable manual frontage created."));
		return true;
	}
	if (bControl && Key == EKeys::MiddleMouseButton
		&& Frontage->Kind != EDeepLevelRoadsideFrontageKind::Automatic)
	{
		const FScopedTransaction Transaction(LOCTEXT("RemoveFrontageOverride", "Remove Roadside Frontage Override"));
		Owner->RemoveFrontageOverride(*Frontage);
		ShowFrontageNotification(LOCTEXT("FrontageOverrideRemoved", "Manual frontage override removed."));
		return true;
	}

	return FSplineComponentVisualizer::HandleInputKey(ViewportClient, Viewport, Key, Event);
}

#undef LOCTEXT_NAMESPACE
