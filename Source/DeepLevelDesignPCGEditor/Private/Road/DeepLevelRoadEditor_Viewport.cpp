// Copyright <--\, Inc. All Rights Reserved.

#include "Road/DeepLevelRoadEditor.h"

// ---- DeepLevelRoadSplineComponentVisualizer ----

#include "Editor.h"
#include "EditorViewportClient.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "InputCoreTypes.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "Selection.h"
#include "ScopedTransaction.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "DeepLevelRoadSplineComponentVisualizer"

namespace
{
	struct FGridEdge
	{
		FIntPoint A;
		FIntPoint B;

		bool operator==(const FGridEdge& Other) const
		{
			return A == Other.A && B == Other.B;
		}

		friend uint32 GetTypeHash(const FGridEdge& Edge)
		{
			return HashCombineFast(GetTypeHash(Edge.A), GetTypeHash(Edge.B));
		}
	};

	FGridEdge MakeGridEdge(FIntPoint A, FIntPoint B)
	{
		if (A.X > B.X || (A.X == B.X && A.Y > B.Y))
		{
			Swap(A, B);
		}
		return {A, B};
	}

	FIntPoint ToGridCell(const FVector& Location, const FVector& GridOrigin, const double GridSize)
	{
		return FIntPoint(
			FMath::RoundToInt((Location.X - GridOrigin.X) / GridSize),
			FMath::RoundToInt((Location.Y - GridOrigin.Y) / GridSize));
	}

	void AddSegmentEdges(
		const FVector& Start,
		const FVector& End,
		const FVector& GridOrigin,
		const double GridSize,
		TFunctionRef<void(const FGridEdge&)> Visitor)
	{
		FIntPoint Current = ToGridCell(Start, GridOrigin, GridSize);
		const FIntPoint Target = ToGridCell(End, GridOrigin, GridSize);
		if (Current.X != Target.X && Current.Y != Target.Y)
		{
			return;
		}
		while (Current != Target)
		{
			FIntPoint Next = Current;
			Next.X += FMath::Sign(Target.X - Current.X);
			Next.Y += FMath::Sign(Target.Y - Current.Y);
			Visitor(MakeGridEdge(Current, Next));
			Current = Next;
		}
	}

	void AppendGridSegment(
		const FVector& Start,
		const FVector& End,
		const double GridSize,
		TArray<FVector>& OutPath)
	{
		if (OutPath.IsEmpty())
		{
			OutPath.Add(Start);
		}
		else if (!OutPath.Last().Equals(Start, 0.1))
		{
			OutPath.Add(Start);
		}

		FVector Current = Start;
		const bool bMovesInX = !FMath::IsNearlyEqual(Current.X, End.X, 0.1);
		const bool bMovesInY = !FMath::IsNearlyEqual(Current.Y, End.Y, 0.1);
		if (bMovesInX && bMovesInY)
		{
			return;
		}

		while (!Current.Equals(End, 0.1))
		{
			if (bMovesInX)
			{
				Current.X += FMath::Sign(End.X - Current.X) * GridSize;
			}
			else
			{
				Current.Y += FMath::Sign(End.Y - Current.Y) * GridSize;
			}
			Current.X = bMovesInX && FMath::Abs(End.X - Current.X) < GridSize ? End.X : Current.X;
			Current.Y = bMovesInY && FMath::Abs(End.Y - Current.Y) < GridSize ? End.Y : Current.Y;
			OutPath.Add(Current);
		}
	}
}

FDeepLevelRoadSplineComponentVisualizer::~FDeepLevelRoadSplineComponentVisualizer()
{
	ResetDrag();
}

FVector FDeepLevelRoadSplineComponentVisualizer::SnapWorldToGrid(
	const FVector& WorldLocation,
	const FVector& GridOrigin,
	const double GridSize)
{
	check(GridSize > UE_DOUBLE_SMALL_NUMBER);
	return FVector(
		FMath::GridSnap(WorldLocation.X - GridOrigin.X, GridSize) + GridOrigin.X,
		FMath::GridSnap(WorldLocation.Y - GridOrigin.Y, GridSize) + GridOrigin.Y,
		GridOrigin.Z);
}

void FDeepLevelRoadSplineComponentVisualizer::SimplifyGridPath(
	const TArray<FVector>& GridPath,
	TArray<FVector>& OutSplinePoints)
{
	OutSplinePoints.Reset();
	if (GridPath.IsEmpty())
	{
		return;
	}
	OutSplinePoints.Add(GridPath[0]);
	for (int32 Index = 1; Index < GridPath.Num() - 1; ++Index)
	{
		const FVector Incoming = GridPath[Index] - GridPath[Index - 1];
		const FVector Outgoing = GridPath[Index + 1] - GridPath[Index];
		if (!Incoming.GetSafeNormal2D().Equals(Outgoing.GetSafeNormal2D(), 0.01))
		{
			OutSplinePoints.Add(GridPath[Index]);
		}
	}
	if (GridPath.Num() > 1)
	{
		OutSplinePoints.Add(GridPath.Last());
	}
}

void FDeepLevelRoadSplineComponentVisualizer::ComposeReshapedGridPath(
	const TArray<FVector>& FixedPath,
	const TArray<FVector>& DragPath,
	TArray<FVector>& OutPath)
{
	OutPath = FixedPath;
	if (OutPath.IsEmpty())
	{
		OutPath = DragPath;
		return;
	}

	const int32 FirstDragIndex = !DragPath.IsEmpty() && OutPath.Last().Equals(DragPath[0], 0.1) ? 1 : 0;
	for (int32 DragIndex = FirstDragIndex; DragIndex < DragPath.Num(); ++DragIndex)
	{
		const FVector& Next = DragPath[DragIndex];
		if (OutPath.Num() >= 2 && OutPath[OutPath.Num() - 2].Equals(Next, 0.1))
		{
			OutPath.Pop();
		}
		else if (!OutPath.Last().Equals(Next, 0.1))
		{
			OutPath.Add(Next);
		}
	}
}

int32 FDeepLevelRoadSplineComponentVisualizer::FindSplineEndpointAtGridCell(
	const UDeepLevelRoadSplineComponent& Spline,
	const FVector& GridCell,
	const FVector& GridOrigin,
	const double GridSize)
{
	if (Spline.GetNumberOfSplinePoints() < 2)
	{
		return INDEX_NONE;
	}

	const int32 EndpointIndices[] = {0, Spline.GetNumberOfSplinePoints() - 1};
	for (const int32 PointIndex : EndpointIndices)
	{
		const FVector Endpoint = SnapWorldToGrid(
			Spline.GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World),
			GridOrigin,
			GridSize);
		if (Endpoint.Equals(GridCell, 0.1))
		{
			return PointIndex;
		}
	}
	return INDEX_NONE;
}

bool FDeepLevelRoadSplineComponentVisualizer::ShouldShowForSelectedSubcomponents(const UActorComponent* Component)
{
	return Component && (
		Component->IsA<UDeepLevelRoadNetworkRootComponent>()
		|| Component->IsA<UDeepLevelRoadSplineComponent>());
}

ADeepLevelRoadNetworkActor* FDeepLevelRoadSplineComponentVisualizer::GetSelectedRoadNetwork() const
{
	if (!GEditor || GEditor->GetSelectedActorCount() != 1)
	{
		return nullptr;
	}
	return Cast<ADeepLevelRoadNetworkActor>(GEditor->GetSelectedActors()->GetTop<AActor>());
}

bool FDeepLevelRoadSplineComponentVisualizer::UpdateHoveredCell()
{
	ADeepLevelRoadNetworkActor* Network = GetSelectedRoadNetwork();
	if (!Network || !FMath::IsFinite(Network->GridSize) || Network->GridSize <= UE_DOUBLE_SMALL_NUMBER)
	{
		bHasHoveredCell = false;
		HoveredNetwork.Reset();
		return false;
	}

	FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	const TSharedPtr<SLevelViewport> LevelViewport = LevelEditor.GetFirstActiveLevelViewport();
	if (!LevelViewport.IsValid() || (!bDragging && !LevelViewport->IsHovered()))
	{
		bHasHoveredCell = false;
		HoveredNetwork.Reset();
		return false;
	}

	FLevelEditorViewportClient& ViewportClient = LevelViewport->GetLevelViewportClient();
	const FViewportCursorLocation Cursor = ViewportClient.GetCursorWorldLocationFromMousePos();
	const FVector Direction = Cursor.GetDirection();
	if (FMath::IsNearlyZero(Direction.Z))
	{
		bHasHoveredCell = false;
		HoveredNetwork.Reset();
		return false;
	}

	const double Distance = (Network->GetActorLocation().Z - Cursor.GetOrigin().Z) / Direction.Z;
	if (Distance < 0.0)
	{
		bHasHoveredCell = false;
		HoveredNetwork.Reset();
		return false;
	}

	const FVector GridOrigin = Network->GetGridOrigin();
	const FVector NewHoveredCell = SnapWorldToGrid(
		Cursor.GetOrigin() + Direction * Distance,
		GridOrigin,
		Network->GridSize);
	const bool bChanged = !bHasHoveredCell
		|| HoveredNetwork.Get() != Network
		|| !HoveredCell.Equals(NewHoveredCell, 0.1);
	HoveredNetwork = Network;
	HoveredCell = NewHoveredCell;
	bHasHoveredCell = true;
	if (bChanged)
	{
		ViewportClient.Invalidate();
	}
	return true;
}

bool FDeepLevelRoadSplineComponentVisualizer::FindEndpointAtHoveredCell(
	ADeepLevelRoadNetworkActor& Network,
	UDeepLevelRoadSplineComponent*& OutSpline,
	int32& OutPointIndex) const
{
	OutSpline = nullptr;
	OutPointIndex = INDEX_NONE;
	TArray<UDeepLevelRoadSplineComponent*> Splines;
	Network.GetRoadSplineComponents(Splines);
	for (UDeepLevelRoadSplineComponent* Spline : Splines)
	{
		if (!Spline || Spline->GetNumberOfSplinePoints() < 2)
		{
			continue;
		}

		const int32 PointIndex = FindSplineEndpointAtGridCell(
			*Spline,
			HoveredCell,
			Network.GetGridOrigin(),
			Network.GridSize);
		if (PointIndex != INDEX_NONE)
		{
			OutSpline = Spline;
			OutPointIndex = PointIndex;
			return true;
		}
	}
	return false;
}

void FDeepLevelRoadSplineComponentVisualizer::AppendHoveredCellToPath()
{
	ADeepLevelRoadNetworkActor* Network = DraggedNetwork.Get();
	if (!Network || !bHasHoveredCell)
	{
		return;
	}
	if (DragPath.IsEmpty())
	{
		DragPath.Add(DragAnchor);
	}

	FVector Current = DragPath.Last();
	while (!Current.Equals(HoveredCell, 0.1))
	{
		const FVector Delta = HoveredCell - Current;
		FVector Next = Current;
		if (FMath::Abs(Delta.X) >= FMath::Abs(Delta.Y) && !FMath::IsNearlyZero(Delta.X))
		{
			Next.X += FMath::Sign(Delta.X) * Network->GridSize;
		}
		else
		{
			Next.Y += FMath::Sign(Delta.Y) * Network->GridSize;
		}

		if (DragPath.Num() >= 2 && Next.Equals(DragPath[DragPath.Num() - 2], 0.1))
		{
			DragPath.Pop();
		}
		else
		{
			DragPath.Add(Next);
		}
		Current = Next;
	}
}

void FDeepLevelRoadSplineComponentVisualizer::BuildReplacementPath(TArray<FVector>& OutPath) const
{
	OutPath.Reset();
	const UDeepLevelRoadSplineComponent* Spline = DraggedSpline.Get();
	const ADeepLevelRoadNetworkActor* Network = DraggedNetwork.Get();
	if (bCreatedSpline || !Spline || !Network || EditedSegmentIndex == INDEX_NONE)
	{
		OutPath = DragPath;
		return;
	}

	const FVector GridOrigin = Network->GetGridOrigin();
	auto GetSnappedPoint = [Spline, &GridOrigin, Network](const int32 PointIndex)
	{
		return SnapWorldToGrid(
			Spline->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World),
			GridOrigin,
			Network->GridSize);
	};
	TArray<FVector> FixedPath;
	if (bReplaceSplineStart)
	{
		FixedPath.Add(GetSnappedPoint(Spline->GetNumberOfSplinePoints() - 1));
		for (int32 PointIndex = Spline->GetNumberOfSplinePoints() - 2;
			PointIndex > EditedSegmentIndex;
			--PointIndex)
		{
			AppendGridSegment(FixedPath.Last(), GetSnappedPoint(PointIndex), Network->GridSize, FixedPath);
		}
		AppendGridSegment(
			FixedPath.Last(),
			SnapWorldToGrid(DragAnchor, GridOrigin, Network->GridSize),
			Network->GridSize,
			FixedPath);
		ComposeReshapedGridPath(FixedPath, DragPath, OutPath);
		for (int32 Left = 0, Right = OutPath.Num() - 1; Left < Right; ++Left, --Right)
		{
			Swap(OutPath[Left], OutPath[Right]);
		}
	}
	else
	{
		FixedPath.Add(GetSnappedPoint(0));
		for (int32 PointIndex = 1; PointIndex <= EditedSegmentIndex; ++PointIndex)
		{
			AppendGridSegment(FixedPath.Last(), GetSnappedPoint(PointIndex), Network->GridSize, FixedPath);
		}
		AppendGridSegment(
			FixedPath.Last(),
			SnapWorldToGrid(DragAnchor, GridOrigin, Network->GridSize),
			Network->GridSize,
			FixedPath);
		ComposeReshapedGridPath(FixedPath, DragPath, OutPath);
	}
}

bool FDeepLevelRoadSplineComponentVisualizer::DoesDragPathOverlapExistingRoad() const
{
	const ADeepLevelRoadNetworkActor* Network = DraggedNetwork.Get();
	if (!Network)
	{
		return false;
	}

	TSet<FGridEdge> ExistingEdges;
	const FVector GridOrigin = Network->GetGridOrigin();
	TArray<UDeepLevelRoadSplineComponent*> Splines;
	Network->GetRoadSplineComponents(Splines);
	for (const UDeepLevelRoadSplineComponent* Spline : Splines)
	{
		if (!Spline || (bEditingSplineEndpoint && Spline == DraggedSpline.Get()))
		{
			continue;
		}
		const int32 SegmentCount = Spline->GetNumberOfSplinePoints() - 1;
		for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
		{
			AddSegmentEdges(
				Spline->GetLocationAtSplinePoint(SegmentIndex, ESplineCoordinateSpace::World),
				Spline->GetLocationAtSplinePoint(SegmentIndex + 1, ESplineCoordinateSpace::World),
				GridOrigin,
				Network->GridSize,
				[&ExistingEdges](const FGridEdge& Edge) { ExistingEdges.Add(Edge); });
		}
	}

	TSet<FGridEdge> ProposedEdges;
	bool bOverlaps = false;
	auto TestEdge = [&ExistingEdges, &ProposedEdges, &bOverlaps](const FGridEdge& Edge)
	{
		bOverlaps |= ExistingEdges.Contains(Edge) || ProposedEdges.Contains(Edge);
		ProposedEdges.Add(Edge);
	};
	TArray<FVector> CandidatePath;
	BuildReplacementPath(CandidatePath);
	for (int32 Index = 0; Index + 1 < CandidatePath.Num(); ++Index)
	{
		AddSegmentEdges(CandidatePath[Index], CandidatePath[Index + 1], GridOrigin, Network->GridSize, TestEdge);
	}
	return bOverlaps;
}

void FDeepLevelRoadSplineComponentVisualizer::ShowOverlapWarning() const
{
	FNotificationInfo Info(LOCTEXT(
		"OverlappingRoadBlocked",
		"Road drawing was blocked because it overlaps an existing Road Line."));
	Info.bFireAndForget = true;
	Info.ExpireDuration = 4.0f;
	if (const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Notification->SetCompletionState(SNotificationItem::CS_Fail);
	}
}

void FDeepLevelRoadSplineComponentVisualizer::DrawVisualization(
	const UActorComponent* Component,
	const FSceneView* View,
	FPrimitiveDrawInterface* PDI)
{
	const UDeepLevelRoadSplineComponent* Spline = Cast<UDeepLevelRoadSplineComponent>(Component);
	if (Spline)
	{
		FSplineComponentVisualizer::DrawVisualization(Component, View, PDI);
		return;
	}

	const UDeepLevelRoadNetworkRootComponent* Root = Cast<UDeepLevelRoadNetworkRootComponent>(Component);
	const ADeepLevelRoadNetworkActor* Network = Root ? Root->GetOwner<ADeepLevelRoadNetworkActor>() : nullptr;
	if (!Root || !Network || !PDI || !bHasHoveredCell || HoveredNetwork.Get() != Network)
	{
		return;
	}

	const double HalfGrid = Network->GridSize * 0.5;
	const double Height = Network->GetActorLocation().Z + 8.0;
	const FVector Corners[] = {
		FVector(HoveredCell.X - HalfGrid, HoveredCell.Y - HalfGrid, Height),
		FVector(HoveredCell.X + HalfGrid, HoveredCell.Y - HalfGrid, Height),
		FVector(HoveredCell.X + HalfGrid, HoveredCell.Y + HalfGrid, Height),
		FVector(HoveredCell.X - HalfGrid, HoveredCell.Y + HalfGrid, Height)
	};
	const bool bAuthoringActive = bDragging || FSlateApplication::Get().GetModifierKeys().IsShiftDown();
	const FLinearColor HoverColor = bAuthoringActive
		? FLinearColor(0.0f, 0.75f, 1.0f, 0.45f)
		: FLinearColor(0.55f, 0.55f, 0.55f, 0.25f);
	for (int32 CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
	{
		PDI->DrawLine(
			Corners[CornerIndex],
			Corners[(CornerIndex + 1) % 4],
			HoverColor,
			SDPG_Foreground,
			5.0f);
	}
	PDI->DrawLine(Corners[0], Corners[2], HoverColor, SDPG_Foreground, 1.5f);
	PDI->DrawLine(Corners[1], Corners[3], HoverColor, SDPG_Foreground, 1.5f);

	if (bDragging)
	{
		const FLinearColor PreviewColor = bDragOverlapsExistingRoad ? FLinearColor::Red : FLinearColor::Green;
		if (bCreatedSpline || bEditingSplineEndpoint)
		{
			TArray<FVector> PreviewPath;
			BuildReplacementPath(PreviewPath);
			for (int32 Index = 0; Index + 1 < PreviewPath.Num(); ++Index)
			{
				PDI->DrawLine(PreviewPath[Index], PreviewPath[Index + 1], PreviewColor, SDPG_Foreground, 7.0f);
			}
		}
	}
}

void FDeepLevelRoadSplineComponentVisualizer::DrawVisualizationHUD(
	const UActorComponent* Component,
	const FViewport* Viewport,
	const FSceneView* View,
	FCanvas* Canvas)
{
	const UDeepLevelRoadSplineComponent* Spline = Cast<UDeepLevelRoadSplineComponent>(Component);
	if (Spline)
	{
		FSplineComponentVisualizer::DrawVisualizationHUD(Component, Viewport, View, Canvas);
		return;
	}

	const UDeepLevelRoadNetworkRootComponent* Root = Cast<UDeepLevelRoadNetworkRootComponent>(Component);
	const ADeepLevelRoadNetworkActor* Network = Root ? Root->GetOwner<ADeepLevelRoadNetworkActor>() : nullptr;
	if (!Root || !Network || !View || !Canvas || GetSelectedRoadNetwork() != Network)
	{
		return;
	}

	FCanvasTextItem Hint(
		FVector2D(24.0, 48.0),
		LOCTEXT("RoadAuthoringHint", "Shift + Left Drag: Draw / Reshape Road"),
		GEngine->GetSmallFont(),
		FLinearColor(0.7f, 0.9f, 1.0f));
	Hint.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(Hint);

	FVector2D ScreenPosition;
	if (View->WorldToPixel(Network->GetActorLocation(), ScreenPosition))
	{
		FCanvasTextItem NetworkLabel(
			ScreenPosition + FVector2D(18.0, -12.0),
			LOCTEXT("RoadNetworkViewportLabel", "ROAD NETWORK PIVOT"),
			GEngine->GetSmallFont(),
			FLinearColor(0.0f, 0.75f, 1.0f));
		NetworkLabel.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(NetworkLabel);
	}
}

void FDeepLevelRoadSplineComponentVisualizer::Tick(
	const float,
	FSlateApplication&,
	TSharedRef<ICursor>)
{
	if (!bDragging)
	{
		UpdateHoveredCell();
	}
}

bool FDeepLevelRoadSplineComponentVisualizer::HandleMouseMoveEvent(
	FSlateApplication&,
	const FPointerEvent&)
{
	if (!bDragging)
	{
		UpdateHoveredCell();
		return false;
	}

	if (UpdateHoveredCell())
	{
		ApplyDrag();
	}
	return true;
}

bool FDeepLevelRoadSplineComponentVisualizer::HandleMouseButtonDownEvent(
	FSlateApplication&,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton
		|| !MouseEvent.IsShiftDown()
		|| bDragging
		|| !UpdateHoveredCell())
	{
		return false;
	}

	ADeepLevelRoadNetworkActor* Network = HoveredNetwork.Get();
	if (!Network)
	{
		return false;
	}

	DraggedNetwork = Network;

	UDeepLevelRoadSplineComponent* Spline = nullptr;
	int32 PointIndex = INDEX_NONE;
	if (FindEndpointAtHoveredCell(*Network, Spline, PointIndex))
	{
		bCreatedSpline = false;
		bEditingSplineEndpoint = true;
		bReplaceSplineStart = PointIndex == 0;
		EditedSegmentIndex = bReplaceSplineStart ? 0 : Spline->GetNumberOfSplinePoints() - 2;
	}
	else
	{
		bCreatedSpline = true;
		bEditingSplineEndpoint = false;
		bReplaceSplineStart = false;
		EditedSegmentIndex = INDEX_NONE;
	}

	DragAnchor = HoveredCell;
	DragPath.Reset();
	DragPath.Add(HoveredCell);
	DraggedSpline = Spline;
	bDragging = true;
	if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->Viewport)
	{
		GCurrentLevelEditingViewportClient->Viewport->CaptureMouse(true);
	}
	ApplyDrag();
	return true;
}

bool FDeepLevelRoadSplineComponentVisualizer::HandleMouseButtonUpEvent(
	FSlateApplication&,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bDragging)
	{
		return false;
	}
	FinishDrag();
	return true;
}

void FDeepLevelRoadSplineComponentVisualizer::ApplyDrag()
{
	if (!bHasHoveredCell)
	{
		return;
	}
	AppendHoveredCellToPath();
	bDragOverlapsExistingRoad = DoesDragPathOverlapExistingRoad();
	if (GCurrentLevelEditingViewportClient)
	{
		GCurrentLevelEditingViewportClient->Invalidate();
	}
}

void FDeepLevelRoadSplineComponentVisualizer::FinishDrag()
{
	ADeepLevelRoadNetworkActor* Network = DraggedNetwork.Get();
	if (!Network || !bHasHoveredCell)
	{
		ResetDrag();
		return;
	}

	UDeepLevelRoadSplineComponent* Spline = DraggedSpline.Get();
	if (bDragOverlapsExistingRoad)
	{
		ShowOverlapWarning();
		ResetDrag();
		return;
	}
	if (DragPath.Num() <= 1)
	{
		ResetDrag();
		return;
	}

	TArray<FVector> ReplacementPath;
	BuildReplacementPath(ReplacementPath);
	TArray<FVector> SplinePoints;
	SimplifyGridPath(ReplacementPath, SplinePoints);
	if (SplinePoints.Num() < 2)
	{
		ResetDrag();
		return;
	}

	DragTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("DrawRoadLineTransaction", "Draw Road Line"));
	Network->Modify();
	if (bCreatedSpline && DragPath.Num() > 1)
	{
		Spline = Network->CreateRoadBranch();
		DraggedSpline = Spline;
	}

	if (!Spline)
	{
		ResetDrag();
		return;
	}

	Spline->Modify();
	Spline->SetRoadPathFromWorldPoints(SplinePoints);
	Spline->MarkPackageDirty();
	Network->MarkPackageDirty();
	Network->NotifyRoadNetworkChanged(
		bCreatedSpline ? EDeepLevelRoadNetworkChange::Structure : EDeepLevelRoadNetworkChange::Geometry);
	if (bCreatedSpline && GEditor)
	{
		GEditor->SelectComponent(Spline, true, true, true);
	}

	ResetDrag();
	if (GCurrentLevelEditingViewportClient)
	{
		GCurrentLevelEditingViewportClient->Invalidate();
	}
}

void FDeepLevelRoadSplineComponentVisualizer::ResetDrag()
{
	if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->Viewport)
	{
		GCurrentLevelEditingViewportClient->Viewport->CaptureMouse(false);
	}
	bDragging = false;
	bCreatedSpline = false;
	bEditingSplineEndpoint = false;
	bReplaceSplineStart = false;
	DraggedNetwork.Reset();
	DraggedSpline.Reset();
	EditedSegmentIndex = INDEX_NONE;
	DragAnchor = FVector::ZeroVector;
	DragPath.Reset();
	bDragOverlapsExistingRoad = false;
	DragTransaction.Reset();
}

#undef LOCTEXT_NAMESPACE

// ---- DeepLevelRoadTileCatalogPreviewViewport ----

#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CanvasTypes.h"
#include "CanvasItem.h"
#include "EditorViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "SceneManagement.h"

#define LOCTEXT_NAMESPACE "DeepLevelRoadTileCatalogPreviewViewport"

IMPLEMENT_HIT_PROXY(HDeepLevelRoadConnectionProxy, HHitProxy);

FDeepLevelRoadTileCatalogViewportClient::FDeepLevelRoadTileCatalogViewportClient(
	FAdvancedPreviewScene& Scene,
	const TSharedRef<SEditorViewport>& Viewport,
	TWeakPtr<FDeepLevelRoadTileCatalogEditorToolkit> InToolkit)
	: FEditorViewportClient(nullptr, &Scene, Viewport), Toolkit(InToolkit)
{
	bSetListenerPosition = false;
}

FVector FDeepLevelRoadTileCatalogViewportClient::GetWidgetLocation() const
{
	if (const TSharedPtr<FDeepLevelRoadTileCatalogEditorToolkit> Pinned = Toolkit.Pin())
	{
		return Pinned->GetPreviewWidgetLocation();
	}
	return FVector::ZeroVector;
}

bool FDeepLevelRoadTileCatalogViewportClient::InputWidgetDelta(
	FViewport* InViewport,
	const EAxisList::Type CurrentAxis,
	FVector& Drag,
	FRotator& Rotation,
	FVector& Scale)
{
	if (CurrentAxis == EAxisList::None)
	{
		return false;
	}
	if (const TSharedPtr<FDeepLevelRoadTileCatalogEditorToolkit> Pinned = Toolkit.Pin())
	{
		Pinned->ApplyPreviewWidgetDelta(Drag, Rotation, Scale, GetWidgetMode());
		return true;
	}
	return false;
}

FMatrix FDeepLevelRoadTileCatalogViewportClient::GetWidgetCoordSystem() const
{
	if (GetWidgetCoordSystemSpace() == COORD_Local)
	{
		if (const TSharedPtr<SEditorViewport> ViewportWidget = EditorViewportWidget.Pin())
		{
			return FRotationMatrix(StaticCastSharedPtr<SDeepLevelRoadTileCatalogPreviewViewport>(ViewportWidget)->GetVolumeRotationWorld());
		}
	}
	return FMatrix::Identity;
}

void FDeepLevelRoadTileCatalogViewportClient::ProcessClick(
	FSceneView& View,
	HHitProxy* HitProxy,
	FKey Key,
	EInputEvent Event,
	uint32 HitX,
	uint32 HitY)
{
	if (HitProxy && HitProxy->IsA(HDeepLevelRoadConnectionProxy::StaticGetType())
		&& Event == IE_Released
		&& Key == EKeys::LeftMouseButton)
	{
		if (const TSharedPtr<FDeepLevelRoadTileCatalogEditorToolkit> Pinned = Toolkit.Pin())
		{
			Pinned->CycleRoadConnection(static_cast<HDeepLevelRoadConnectionProxy*>(HitProxy)->ConnectionMask);
		}
		return;
	}
	FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
}

void FDeepLevelRoadTileCatalogViewportClient::DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{
	FEditorViewportClient::DrawCanvas(InViewport, View, Canvas);
	if (const TSharedPtr<SEditorViewport> ViewportWidget = EditorViewportWidget.Pin())
	{
		StaticCastSharedPtr<SDeepLevelRoadTileCatalogPreviewViewport>(ViewportWidget)->DrawPortLabels(&View, &Canvas);
	}
}

void FDeepLevelRoadTileCatalogViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);
	if (const TSharedPtr<SEditorViewport> ViewportWidget = EditorViewportWidget.Pin())
	{
		StaticCastSharedPtr<SDeepLevelRoadTileCatalogPreviewViewport>(ViewportWidget)->DrawTileGuides(PDI);
	}
}

void SDeepLevelRoadTileCatalogPreviewViewport::Construct(const FArguments& Args)
{
	Toolkit = Args._Toolkit;
	PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
	SEditorViewport::Construct(SEditorViewport::FArguments());
	ViewportClient->SetViewMode(VMI_Lit);
	ViewportClient->SetWidgetMode(UE::Widget::WM_Translate);
	ViewportClient->SetInitialViewTransform(
		LVT_Perspective,
		FVector(-900.0, -900.0, 700.0),
		FRotator(-25.0, 45.0, 0.0),
		DEFAULT_ORTHOZOOM);
}

SDeepLevelRoadTileCatalogPreviewViewport::~SDeepLevelRoadTileCatalogPreviewViewport()
{
	ClearPreview();
	ViewportClient.Reset();
	PreviewScene.Reset();
}

TSharedRef<FEditorViewportClient> SDeepLevelRoadTileCatalogPreviewViewport::MakeEditorViewportClient()
{
	ViewportClient = MakeShared<FDeepLevelRoadTileCatalogViewportClient>(*PreviewScene, SharedThis(this), Toolkit);
	return ViewportClient.ToSharedRef();
}

void SDeepLevelRoadTileCatalogPreviewViewport::ClearPreview()
{
	if (PreviewActor.IsValid() && PreviewScene.IsValid() && PreviewScene->GetWorld())
	{
		PreviewScene->GetWorld()->DestroyActor(PreviewActor.Get());
	}
	PreviewActor.Reset();
	bHasDefinition = false;
}

AStaticMeshActor* SDeepLevelRoadTileCatalogPreviewViewport::SpawnTile(UStaticMesh* TileMesh)
{
	if (!TileMesh || !PreviewScene.IsValid())
	{
		return nullptr;
	}
	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags = RF_Transient;
	AStaticMeshActor* Actor = PreviewScene->GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform::Identity, Parameters);
	if (Actor && Actor->GetStaticMeshComponent())
	{
		Actor->GetStaticMeshComponent()->SetStaticMesh(TileMesh);
	}
	return Actor;
}

void SDeepLevelRoadTileCatalogPreviewViewport::PreviewTile(const FDeepLevelRoadTileDefinition* Definition, const double GridCellSize)
{
	ClearPreview();
	PreviewGridCellSize = GridCellSize;
	if (!Definition)
	{
		ViewportClient->Invalidate();
		return;
	}
	PreviewDefinition = *Definition;
	bHasDefinition = true;
	PreviewActor = SpawnTile(Definition->TileMesh.LoadSynchronous());
	ViewportClient->Invalidate();
}

bool SDeepLevelRoadTileCatalogPreviewViewport::AutoFitVolume(
	UStaticMesh* TileMesh,
	const double GridCellSize,
	FVector& OutCenter,
	FVector& OutExtent)
{
	ClearPreview();
	if (!TileMesh)
	{
		return false;
	}
	const FBox Bounds = TileMesh->GetBoundingBox();
	const bool bValid = Bounds.IsValid != 0;
	if (bValid)
	{
		OutCenter = Bounds.GetCenter();
		OutExtent = FVector(GridCellSize * 0.5, GridCellSize * 0.5, FMath::Max(Bounds.GetExtent().Z, 1.0));
	}
	ClearPreview();
	return bValid;
}

FVector SDeepLevelRoadTileCatalogPreviewViewport::GetVolumeCenterWorld() const
{
	return bHasDefinition ? PreviewDefinition.PlacementVolume.Center : FVector::ZeroVector;
}

FRotator SDeepLevelRoadTileCatalogPreviewViewport::GetVolumeRotationWorld() const
{
	return bHasDefinition ? PreviewDefinition.PlacementVolume.Rotation : FRotator::ZeroRotator;
}

void SDeepLevelRoadTileCatalogPreviewViewport::DrawTileGuides(FPrimitiveDrawInterface* PDI) const
{
	if (!PDI)
	{
		return;
	}
	for (int32 Grid = -4; Grid <= 4; ++Grid)
	{
		const double Position = Grid * PreviewGridCellSize;
		const FLinearColor Color = Grid == 0 ? FLinearColor(0.4f, 0.4f, 0.4f) : FLinearColor(0.12f, 0.12f, 0.12f);
		PDI->DrawLine(FVector(-2000.0, Position, 0.0), FVector(2000.0, Position, 0.0), Color, SDPG_World);
		PDI->DrawLine(FVector(Position, -2000.0, 0.0), FVector(Position, 2000.0, 0.0), Color, SDPG_World);
	}
	if (!bHasDefinition)
	{
		return;
	}
	const FTransform VolumeTransform(PreviewDefinition.PlacementVolume.Rotation, PreviewDefinition.PlacementVolume.Center);
	DrawWireBox(
		PDI,
		VolumeTransform.ToMatrixWithScale(),
		FBox(-PreviewDefinition.PlacementVolume.Extent, PreviewDefinition.PlacementVolume.Extent),
		FLinearColor(0.1f, 1.0f, 0.25f),
		SDPG_World,
		2.0f);
	struct FDirection { int32 Mask; FVector Vector; };
	const FDirection Directions[] = {
		{static_cast<int32>(EDeepLevelRoadConnection::PositiveX), FVector::ForwardVector},
		{static_cast<int32>(EDeepLevelRoadConnection::PositiveY), FVector::RightVector},
		{static_cast<int32>(EDeepLevelRoadConnection::NegativeX), -FVector::ForwardVector},
		{static_cast<int32>(EDeepLevelRoadConnection::NegativeY), -FVector::RightVector}
	};
	const FVector Center = VolumeTransform.TransformPosition(FVector(0.0, 0.0, PreviewDefinition.PlacementVolume.Extent.Z + 20.0));
	for (const FDirection& Direction : Directions)
	{
		const bool bConnected = (PreviewDefinition.ConnectionMask & Direction.Mask) != 0;
		const bool bApproach = bConnected
			&& (PreviewDefinition.ApproachJunctionDirectionMask & Direction.Mask) != 0;
		const FLinearColor Color = bApproach
			? FLinearColor(1.0f, 0.45f, 0.0f)
			: bConnected ? FLinearColor(0.1f, 0.55f, 1.0f) : FLinearColor(0.22f, 0.22f, 0.22f);
		const FVector WorldDirection = VolumeTransform.TransformVectorNoScale(Direction.Vector).GetSafeNormal();
		const FVector PortCenter = Center + WorldDirection * PreviewGridCellSize * 0.46;
		PDI->SetHitProxy(new HDeepLevelRoadConnectionProxy(Direction.Mask));
		PDI->DrawLine(Center, PortCenter, Color, SDPG_Foreground, bConnected ? 10.0f : 5.0f);
		DrawWireBox(
			PDI,
			FTranslationMatrix(PortCenter),
			FBox(FVector(-35.0), FVector(35.0)),
			Color,
			SDPG_Foreground,
			bConnected ? 6.0f : 3.0f);
		PDI->SetHitProxy(nullptr);
	}
}

void SDeepLevelRoadTileCatalogPreviewViewport::DrawPortLabels(const FSceneView* View, FCanvas* Canvas) const
{
	if (!bHasDefinition || !View || !Canvas)
	{
		return;
	}
	struct FDirection { int32 Mask; FVector Vector; };
	const FDirection Directions[] = {
		{static_cast<int32>(EDeepLevelRoadConnection::PositiveX), FVector::ForwardVector},
		{static_cast<int32>(EDeepLevelRoadConnection::PositiveY), FVector::RightVector},
		{static_cast<int32>(EDeepLevelRoadConnection::NegativeX), -FVector::ForwardVector},
		{static_cast<int32>(EDeepLevelRoadConnection::NegativeY), -FVector::RightVector}
	};
	const FTransform VolumeTransform(PreviewDefinition.PlacementVolume.Rotation, PreviewDefinition.PlacementVolume.Center);
	const FVector Center = VolumeTransform.TransformPosition(FVector(0.0, 0.0, PreviewDefinition.PlacementVolume.Extent.Z + 20.0));
	for (const FDirection& Direction : Directions)
	{
		const bool bConnected = (PreviewDefinition.ConnectionMask & Direction.Mask) != 0;
		const bool bJunction = bConnected && PreviewDefinition.ApproachJunctionDirectionMask == Direction.Mask;
		const FLinearColor Color = bJunction
			? FLinearColor(1.0f, 0.45f, 0.0f)
			: bConnected ? FLinearColor(0.1f, 0.55f, 1.0f) : FLinearColor(0.55f, 0.55f, 0.55f);
		const FVector WorldDirection = VolumeTransform.TransformVectorNoScale(Direction.Vector).GetSafeNormal();
		FVector2D ScreenPosition;
		if (!View->ScreenToPixel(View->WorldToScreen(Center + WorldDirection * PreviewGridCellSize * 0.46), ScreenPosition))
		{
			continue;
		}
		const FText State = bJunction
			? LOCTEXT("JunctionPortLabel", "JUNCTION")
			: bConnected ? LOCTEXT("RoadPortLabel", "ROAD") : LOCTEXT("ClosedPortLabel", "CLOSED");
		FCanvasTextItem TextItem(ScreenPosition - FVector2D(0.0f, 48.0f), State, GEngine->GetSmallFont(), Color);
		TextItem.bOutlined = true;
		TextItem.OutlineColor = FLinearColor::Black;
		TextItem.bCentreX = true;
		TextItem.bCentreY = true;
		TextItem.Scale = FVector2D(1.25f);
		Canvas->DrawItem(TextItem);
	}
	const FText TileMode = FText::Format(
		LOCTEXT("TileTypeLabel", "TILE TYPE: {0}"),
		GetRoadTileTypeDisplayName(PreviewDefinition));
	FCanvasTextItem ModeItem(FVector2D(20.0f, 20.0f), TileMode, GEngine->GetMediumFont(), FLinearColor::White);
	ModeItem.bOutlined = true;
	ModeItem.OutlineColor = FLinearColor::Black;
	Canvas->DrawItem(ModeItem);
}

#undef LOCTEXT_NAMESPACE
