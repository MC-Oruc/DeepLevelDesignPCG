// Copyright <--\, Inc. All Rights Reserved.

#include "Building/DeepLevelBuildingEditor.h"
#include "SceneView.h"

// ---- DeepLevelBuildingCatalogPreviewViewport ----

#include "EditorViewportClient.h"
#include "Engine/World.h"
#include "Building/DeepLevelBuildingPCG.h"
#include "Components/PrimitiveComponent.h"
#include "SceneManagement.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Engine.h"

IMPLEMENT_HIT_PROXY(HDeepLevelBuildingFaceProxy, HHitProxy);

namespace
{
	FLinearColor ExposureColor(const EDeepLevelStreetExposureRule Rule)
	{
		switch (Rule)
		{
		case EDeepLevelStreetExposureRule::Required: return FLinearColor(0.1f, 1.0f, 0.2f);
		case EDeepLevelStreetExposureRule::Preferred: return FLinearColor(0.1f, 0.5f, 1.0f);
		case EDeepLevelStreetExposureRule::Forbidden: return FLinearColor(1.0f, 0.1f, 0.1f);
		default: return FLinearColor(0.65f, 0.65f, 0.65f);
		}
	}
}

FDeepLevelBuildingCatalogViewportClient::FDeepLevelBuildingCatalogViewportClient(FAdvancedPreviewScene& InScene, const TSharedRef<SEditorViewport>& InViewport, TWeakPtr<FDeepLevelBuildingCatalogEditorToolkit> InToolkit)
	: FEditorViewportClient(nullptr, &InScene, InViewport), Toolkit(InToolkit)
{
	bSetListenerPosition = false;
}

SDeepLevelBuildingCatalogPreviewViewport::~SDeepLevelBuildingCatalogPreviewViewport()
{
	ClearPreview();
	ViewportClient.Reset();
	PreviewScene.Reset();
}

void FDeepLevelBuildingCatalogViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	if (!PDI) return;
	FEditorViewportClient::Draw(View, PDI);
	for (int32 Grid = -20; Grid <= 20; ++Grid)
	{
		const float Position = Grid * 500.0f;
		const FLinearColor Color = Grid == 0 ? FLinearColor(0.35f,0.35f,0.35f) : FLinearColor(0.12f,0.12f,0.12f);
		PDI->DrawLine(FVector(-10000, Position, 0), FVector(10000, Position, 0), Color, SDPG_World, Grid == 0 ? 2.0f : 1.0f);
		PDI->DrawLine(FVector(Position, -10000, 0), FVector(Position, 10000, 0), Color, SDPG_World, Grid == 0 ? 2.0f : 1.0f);
	}
	if (const TSharedPtr<FDeepLevelBuildingCatalogEditorToolkit> Pinned = Toolkit.Pin())
	{
		const double Stride = Pinned->GetPreviewStride();
		for (int32 Index = 0; Index <= 3; ++Index)
		{
			const double X = Stride * Index;
			PDI->DrawLine(FVector(X, -1000, 0), FVector(X, 5000, 0), FLinearColor(1.0f, 0.65f, 0.0f), SDPG_World, 2.0f);
		}
	}
	if (const TSharedPtr<SEditorViewport> ViewportWidget = EditorViewportWidget.Pin())
	{
		StaticCastSharedPtr<SDeepLevelBuildingCatalogPreviewViewport>(ViewportWidget)->DrawCalibration(PDI);
	}
}

FVector FDeepLevelBuildingCatalogViewportClient::GetWidgetLocation() const
{
	if (const TSharedPtr<FDeepLevelBuildingCatalogEditorToolkit> Pinned = Toolkit.Pin())
	{
		return Pinned->GetPreviewWidgetLocation();
	}
	return FVector::ZeroVector;
}

bool FDeepLevelBuildingCatalogViewportClient::InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale)
{
	if (CurrentAxis == EAxisList::None || GetWidgetMode() == UE::Widget::WM_None) return false;
	if (!Drag.IsNearlyZero() || !Rot.IsNearlyZero() || !Scale.IsNearlyZero())
	{
		if (const TSharedPtr<FDeepLevelBuildingCatalogEditorToolkit> Pinned = Toolkit.Pin())
		{
			Pinned->ApplyPreviewWidgetDelta(Drag, Rot, Scale, GetWidgetMode());
			return true;
		}
	}
	return false;
}

FMatrix FDeepLevelBuildingCatalogViewportClient::GetWidgetCoordSystem() const
{
	if (GetWidgetCoordSystemSpace() == COORD_Local)
	{
		if (const TSharedPtr<SEditorViewport> ViewportWidget = EditorViewportWidget.Pin())
		{
			return FRotationMatrix(StaticCastSharedPtr<SDeepLevelBuildingCatalogPreviewViewport>(ViewportWidget)->GetVolumeRotationWorld());
		}
	}
	return FMatrix::Identity;
}

double FDeepLevelBuildingCatalogViewportClient::ProjectCursorOntoDragAxis()
{
	const FViewportCursorLocation Cursor = GetCursorWorldLocationFromMousePos();
	const FVector Intersection = FMath::LinePlaneIntersection(
		Cursor.GetOrigin(),
		Cursor.GetOrigin() + Cursor.GetDirection() * HALF_WORLD_MAX,
		FPlane(DragPlaneOrigin, DragPlaneNormal));
	return FVector::DotProduct(Intersection - DragPlaneOrigin, DragAxis);
}

bool FDeepLevelBuildingCatalogViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	if (EventArgs.Key != EKeys::LeftMouseButton)
	{
		return FEditorViewportClient::InputKey(EventArgs);
	}

	if (EventArgs.Event == IE_Pressed && !DraggedFace.IsSet())
	{
		HHitProxy* HitProxy = EventArgs.Viewport->GetHitProxy(EventArgs.Viewport->GetMouseX(), EventArgs.Viewport->GetMouseY());
		if (HitProxy && HitProxy->IsA(HDeepLevelBuildingFaceProxy::StaticGetType()))
		{
			const EDeepLevelBuildingVolumeFace Face = static_cast<HDeepLevelBuildingFaceProxy*>(HitProxy)->Face;
			const TSharedPtr<SEditorViewport> ViewportWidget = EditorViewportWidget.Pin();
			const TSharedPtr<FDeepLevelBuildingCatalogEditorToolkit> PinnedToolkit = Toolkit.Pin();
			if (!ViewportWidget || !PinnedToolkit)
			{
				return true;
			}

			const TSharedPtr<SDeepLevelBuildingCatalogPreviewViewport> BuildingViewport =
				StaticCastSharedPtr<SDeepLevelBuildingCatalogPreviewViewport>(ViewportWidget);
			DraggedFace = Face;
			DragAxis = BuildingViewport->GetFaceNormalWorld(Face).GetSafeNormal();
			DragPlaneOrigin = BuildingViewport->GetFaceCenterWorld(Face);
			const FVector CursorDirection = GetCursorWorldLocationFromMousePos().GetDirection();
			DragPlaneNormal = (CursorDirection - DragAxis * FVector::DotProduct(CursorDirection, DragAxis)).GetSafeNormal();
			if (DragPlaneNormal.IsNearlyZero())
			{
				DragPlaneNormal = FVector::CrossProduct(DragAxis, FVector::UpVector).GetSafeNormal();
			}
			PreviousDragDistance = ProjectCursorOntoDragAxis();
			bFaceDragMoved = false;
			PinnedToolkit->BeginPreviewFaceDrag();
			EventArgs.Viewport->CaptureMouse(true);
			return true;
		}
	}
	else if (EventArgs.Event == IE_Released && DraggedFace.IsSet())
	{
		if (const TSharedPtr<FDeepLevelBuildingCatalogEditorToolkit> PinnedToolkit = Toolkit.Pin())
		{
			PinnedToolkit->EndPreviewFaceDrag();
			if (!bFaceDragMoved)
			{
				PinnedToolkit->CycleExposureRule(DraggedFace.GetValue());
			}
		}
		DraggedFace.Reset();
		EventArgs.Viewport->CaptureMouse(false);
		EventArgs.Viewport->Invalidate();
		return true;
	}

	return FEditorViewportClient::InputKey(EventArgs);
}

bool FDeepLevelBuildingCatalogViewportClient::InputAxis(const FInputKeyEventArgs& EventArgs)
{
	if (DraggedFace.IsSet() && (EventArgs.Key == EKeys::MouseX || EventArgs.Key == EKeys::MouseY))
	{
		MouseMove(EventArgs.Viewport, EventArgs.Viewport->GetMouseX(), EventArgs.Viewport->GetMouseY());
		return true;
	}
	return FEditorViewportClient::InputAxis(EventArgs);
}

void FDeepLevelBuildingCatalogViewportClient::MouseMove(FViewport* InViewport, const int32 X, const int32 Y)
{
	if (!DraggedFace.IsSet())
	{
		FEditorViewportClient::MouseMove(InViewport, X, Y);
		return;
	}

	const double CurrentDragDistance = ProjectCursorOntoDragAxis();
	const double Delta = CurrentDragDistance - PreviousDragDistance;
	if (!FMath::IsNearlyZero(Delta, 0.1))
	{
		if (const TSharedPtr<FDeepLevelBuildingCatalogEditorToolkit> PinnedToolkit = Toolkit.Pin())
		{
			PinnedToolkit->ResizePreviewFace(DraggedFace.GetValue(), Delta);
			PreviousDragDistance = CurrentDragDistance;
			bFaceDragMoved = true;
		}
	}
	InViewport->Invalidate();
}

void FDeepLevelBuildingCatalogViewportClient::CapturedMouseMove(FViewport* InViewport, const int32 X, const int32 Y)
{
	MouseMove(InViewport, X, Y);
}

void FDeepLevelBuildingCatalogViewportClient::ProcessClick(class FSceneView& View, class HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	if (HitProxy && HitProxy->IsA(HDeepLevelBuildingFaceProxy::StaticGetType()) && Event == IE_Released)
	{
		HDeepLevelBuildingFaceProxy* FaceProxy = static_cast<HDeepLevelBuildingFaceProxy*>(HitProxy);
		if (const TSharedPtr<FDeepLevelBuildingCatalogEditorToolkit> Pinned = Toolkit.Pin())
		{
			Pinned->CycleExposureRule(FaceProxy->Face);
		}
	}
	else
	{
		FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
	}
}

void FDeepLevelBuildingCatalogViewportClient::DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{
	FEditorViewportClient::DrawCanvas(InViewport, View, Canvas);
	if (const TSharedPtr<SEditorViewport> ViewportWidget = EditorViewportWidget.Pin())
	{
		StaticCastSharedPtr<SDeepLevelBuildingCatalogPreviewViewport>(ViewportWidget)->DrawLabels(&View, &Canvas);
	}
}

void SDeepLevelBuildingCatalogPreviewViewport::Construct(const FArguments& Args)
{
	Toolkit = Args._Toolkit;
	SideGuideVisibility = EDeepLevelPreviewGuideVisibility::Visible;
	GroundGuideVisibility = EDeepLevelPreviewGuideVisibility::Visible;
	PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
	SEditorViewport::Construct(SEditorViewport::FArguments());
	if (!ViewportClient.IsValid()) return;
	ViewportClient->SetViewMode(VMI_Lit);
	ViewportClient->SetWidgetMode(UE::Widget::WM_Translate);
	ViewportClient->SetInitialViewTransform(
		LVT_Perspective,
		FVector(-2500.0, -3500.0, 2200.0),
		FRotator(-20.0, 45.0, 0.0),
		DEFAULT_ORTHOZOOM);
}

TSharedRef<FEditorViewportClient> SDeepLevelBuildingCatalogPreviewViewport::MakeEditorViewportClient()
{
	ViewportClient = MakeShared<FDeepLevelBuildingCatalogViewportClient>(*PreviewScene, SharedThis(this), Toolkit);
	return ViewportClient.ToSharedRef();
}

void SDeepLevelBuildingCatalogPreviewViewport::ClearPreview()
{
	if (!PreviewScene.IsValid()) return;
	if (UWorld* World = PreviewScene->GetWorld())
	{
		for (const TWeakObjectPtr<AActor>& Actor : PreviewActors)
		{
			if (Actor.IsValid()) { World->DestroyActor(Actor.Get()); }
		}
	}
	PreviewActors.Reset();
	PrimaryActor.Reset();
	GuideStride = 0.0;
	bHasPreviewVolume = false;
}

AActor* SDeepLevelBuildingCatalogPreviewViewport::SpawnBuilding(UClass* BuildingClass, const FTransform& Transform)
{
	if (!BuildingClass || !BuildingClass->IsChildOf(AActor::StaticClass()) || !PreviewScene.IsValid()) { return nullptr; }
	UWorld* World = PreviewScene->GetWorld();
	if (!World) return nullptr;
	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	AActor* Actor = World->SpawnActor<AActor>(BuildingClass, Transform, Params);
	if (Actor) { PreviewActors.Add(Actor); }
	return Actor;
}

FTransform SDeepLevelBuildingCatalogPreviewViewport::MakeTransform(const FDeepLevelBuildingPlacementDefinition& Definition, double Cursor) const
{
	return FDeepLevelBuildingPlacementGeometry::BuildActorTransform(
		Definition,
		PreviewStreetFace,
		FVector(Cursor, 0, 0),
		FVector::ForwardVector,
		FVector::RightVector);
}

void SDeepLevelBuildingCatalogPreviewViewport::SpawnDefinition(const FDeepLevelBuildingPlacementDefinition& Definition, const FTransform& Transform, bool bPrimary)
{
	AActor* Actor = SpawnBuilding(Definition.BuildingClass.LoadSynchronous(), Transform);
	if (bPrimary && Actor)
	{
		PrimaryActor = Actor;
		PreviewVolumeTransform = FTransform(Definition.PlacementVolume.Rotation, Definition.PlacementVolume.Center) * Actor->GetActorTransform();
		PreviewVolumeExtent = Definition.PlacementVolume.Extent;
		PreviewExposure = Definition.PlacementVolume.Exposure;
		bHasPreviewVolume = true;
	}
}

void SDeepLevelBuildingCatalogPreviewViewport::PreviewBuilding(const UDeepLevelBuildingPlacementCatalog& Catalog, int32 BuildingIndex)
{
	if (!PreviewScene.IsValid()) return;
	ClearPreview();
	if (!Catalog.Buildings.IsValidIndex(BuildingIndex)) { return; }
	const FDeepLevelBuildingPlacementDefinition& Definition = Catalog.Buildings[BuildingIndex];
	const FDeepLevelResolvedBuildingGeometry Geometry = FDeepLevelBuildingPlacementGeometry::ResolveGeometry(Definition, PreviewStreetFace);
	GuideStride = Geometry.HalfWidth * 2.0;
	SpawnDefinition(Definition, FTransform::Identity, true);
	if (PrimaryActor.IsValid())
	{
		PreviewVolumeTransform = FTransform(Definition.PlacementVolume.Rotation, Definition.PlacementVolume.Center) * PrimaryActor->GetActorTransform();
		PreviewVolumeExtent = Definition.PlacementVolume.Extent;
		PreviewExposure = Definition.PlacementVolume.Exposure;
		bHasPreviewVolume = true;

		if (SideGuideVisibility != EDeepLevelPreviewGuideVisibility::Hidden)
		{
			const FTransform WorldToLocal = MakeTransform(Definition, 0.0).Inverse();
			const double Width = Geometry.HalfWidth * 2.0;
			AActor* LeftActor = SpawnBuilding(Definition.BuildingClass.LoadSynchronous(), MakeTransform(Definition, -Width) * WorldToLocal);
			AActor* RightActor = SpawnBuilding(Definition.BuildingClass.LoadSynchronous(), MakeTransform(Definition, Width) * WorldToLocal);
		}

		if (GroundGuideVisibility != EDeepLevelPreviewGuideVisibility::Hidden)
		{
			if (AStaticMeshActor* GroundPlane = Cast<AStaticMeshActor>(SpawnBuilding(AStaticMeshActor::StaticClass(), FTransform::Identity)))
			{
				if (UStaticMeshComponent* SMC = GroundPlane->GetStaticMeshComponent())
				{
					if (UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
					{
						SMC->SetStaticMesh(PlaneMesh);
					}
				}
				FVector PlaneLocation = PreviewVolumeTransform.TransformPosition(FVector(0, 0, -PreviewVolumeExtent.Z));
				GroundPlane->SetActorLocationAndRotation(PlaneLocation, PreviewVolumeTransform.Rotator());
				GroundPlane->SetActorScale3D(FVector(PreviewVolumeExtent.X / 50.0, PreviewVolumeExtent.Y / 50.0, 1.0));
			}
		}
	}
	if (ViewportClient) ViewportClient->Invalidate();
}

void SDeepLevelBuildingCatalogPreviewViewport::PreviewPreset(const UDeepLevelBuildingPlacementCatalog& Catalog, int32 PresetIndex, const int32 Seed)
{
	if (!PreviewScene.IsValid()) return;
	ClearPreview();
	if (!Catalog.Presets.IsValidIndex(PresetIndex)) { return; }
	double Cursor = 0.0;
	for (int32 Repeat = 0; Repeat < 2; ++Repeat)
	{
		for (int32 ElementIndex = 0; ElementIndex < Catalog.Presets[PresetIndex].Buildings.Num(); ++ElementIndex)
		{
			const TSoftClassPtr<AActor>& Class = Catalog.Presets[PresetIndex].Buildings[ElementIndex];
			const FDeepLevelBuildingPlacementDefinition* Definition = Catalog.Buildings.FindByPredicate([&Class](const FDeepLevelBuildingPlacementDefinition& Item) { return Item.BuildingClass == Class; });
			if (Definition)
			{
				EDeepLevelBuildingVolumeFace Face = EDeepLevelBuildingVolumeFace::NegativeY;
				const int32 FaceSeed = FDeepLevelBuildingPlacementGeometry::MakeStreetFaceSeed(
					Seed,
					Repeat,
					Catalog.Buildings.Num() + PresetIndex,
					ElementIndex);
				FDeepLevelBuildingPlacementGeometry::ResolveStreetFace(*Definition, FaceSeed, Face);
				PreviewStreetFace = Face;
				const double Width = FDeepLevelBuildingPlacementGeometry::ResolveGeometry(*Definition, Face).HalfWidth * 2.0;
				SpawnDefinition(*Definition, MakeTransform(*Definition, Cursor + Width * 0.5), !PrimaryActor.IsValid());
				Cursor += Width;
		}
	}
	if (ViewportClient) ViewportClient->Invalidate();
}
	GuideStride = Cursor * 0.5;
}



bool SDeepLevelBuildingCatalogPreviewViewport::AutoFitVolume(UClass* BuildingClass, const FRotator& VolumeRotation, FVector& OutCenter, FVector& OutExtent)
{
	if (!PreviewScene.IsValid()) return false;
	ClearPreview();
	AActor* Actor = SpawnBuilding(BuildingClass, FTransform::Identity);
	if (!Actor) return false;
	FBox LocalBox(EForceInit::ForceInit);
	const FQuat InverseRotation = VolumeRotation.Quaternion().Inverse();
	TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
	for (const UPrimitiveComponent* Component : Components)
	{
		if (!Component || !Component->IsRegistered()) continue;
		const FBox ComponentBox = Component->Bounds.GetBox();
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Point(
				(Corner & 1) ? ComponentBox.Max.X : ComponentBox.Min.X,
				(Corner & 2) ? ComponentBox.Max.Y : ComponentBox.Min.Y,
				(Corner & 4) ? ComponentBox.Max.Z : ComponentBox.Min.Z);
			LocalBox += InverseRotation.RotateVector(Point);
		}
	}
	const bool bValid = LocalBox.IsValid != 0;
	if (bValid)
	{
		OutCenter = VolumeRotation.Quaternion().RotateVector(LocalBox.GetCenter());
		OutExtent = LocalBox.GetExtent().ComponentMax(FVector(1.0));
	}
	ClearPreview();
	return bValid;
}

FVector SDeepLevelBuildingCatalogPreviewViewport::GetPrimaryLocation() const
{
	return PrimaryActor.IsValid() ? PrimaryActor->GetActorLocation() : FVector::ZeroVector;
}

void SDeepLevelBuildingCatalogPreviewViewport::SetGuideVisibility(const EDeepLevelPreviewGuideVisibility InSide, const EDeepLevelPreviewGuideVisibility InGround)
{
	SideGuideVisibility = InSide;
	GroundGuideVisibility = InGround;
	if (ViewportClient) ViewportClient->Invalidate();
}

FVector SDeepLevelBuildingCatalogPreviewViewport::GetVolumeCenterWorld() const
{
	return bHasPreviewVolume ? PreviewVolumeTransform.GetLocation() : FVector::ZeroVector;
}

FVector SDeepLevelBuildingCatalogPreviewViewport::GetVolumePivotWorld() const
{
	return bHasPreviewVolume ? PreviewVolumeTransform.TransformPosition(FVector(0, 0, -PreviewVolumeExtent.Z)) : FVector::ZeroVector;
}

FRotator SDeepLevelBuildingCatalogPreviewViewport::GetVolumeRotationWorld() const { return bHasPreviewVolume ? PreviewVolumeTransform.Rotator() : FRotator::ZeroRotator; }

FVector SDeepLevelBuildingCatalogPreviewViewport::GetFaceCenterWorld(const EDeepLevelBuildingVolumeFace Face) const
{
	if (!bHasPreviewVolume)
	{
		return FVector::ZeroVector;
	}

	FVector Center = FVector::ZeroVector;
	switch (Face)
	{
	case EDeepLevelBuildingVolumeFace::PositiveX: Center.X = PreviewVolumeExtent.X; break;
	case EDeepLevelBuildingVolumeFace::NegativeX: Center.X = -PreviewVolumeExtent.X; break;
	case EDeepLevelBuildingVolumeFace::PositiveY: Center.Y = PreviewVolumeExtent.Y; break;
	case EDeepLevelBuildingVolumeFace::NegativeY: Center.Y = -PreviewVolumeExtent.Y; break;
	default: break;
	}
	return PreviewVolumeTransform.TransformPosition(Center);
}

FVector SDeepLevelBuildingCatalogPreviewViewport::GetFaceNormalWorld(const EDeepLevelBuildingVolumeFace Face) const
{
	FVector Normal = FVector::ZeroVector;
	switch (Face)
	{
	case EDeepLevelBuildingVolumeFace::PositiveX: Normal = FVector::ForwardVector; break;
	case EDeepLevelBuildingVolumeFace::NegativeX: Normal = -FVector::ForwardVector; break;
	case EDeepLevelBuildingVolumeFace::PositiveY: Normal = FVector::RightVector; break;
	case EDeepLevelBuildingVolumeFace::NegativeY: Normal = -FVector::RightVector; break;
	default: break;
	}
	return PreviewVolumeTransform.TransformVectorNoScale(Normal);
}

void SDeepLevelBuildingCatalogPreviewViewport::DrawCalibration(FPrimitiveDrawInterface* PDI) const
{
	if (!PDI || !bHasPreviewVolume) return;
	DrawWireBox(PDI, PreviewVolumeTransform.ToMatrixWithScale(), FBox(-PreviewVolumeExtent, PreviewVolumeExtent), FLinearColor::White, SDPG_World, 3.0f);
	const FVector E = PreviewVolumeExtent;
	const auto DrawFace = [&](const EDeepLevelBuildingVolumeFace Face, const EDeepLevelStreetExposureRule Rule)
	{
		FVector Points[4];
		if (Face == EDeepLevelBuildingVolumeFace::PositiveX || Face == EDeepLevelBuildingVolumeFace::NegativeX)
		{
			const double X = Face == EDeepLevelBuildingVolumeFace::PositiveX ? E.X : -E.X;
			Points[0] = FVector(X,-E.Y,-E.Z); Points[1] = FVector(X,E.Y,-E.Z); Points[2] = FVector(X,E.Y,E.Z); Points[3] = FVector(X,-E.Y,E.Z);
		}
		else
		{
			const double Y = Face == EDeepLevelBuildingVolumeFace::PositiveY ? E.Y : -E.Y;
			Points[0] = FVector(-E.X,Y,-E.Z); Points[1] = FVector(E.X,Y,-E.Z); Points[2] = FVector(E.X,Y,E.Z); Points[3] = FVector(-E.X,Y,E.Z);
		}
		FLinearColor Color = ExposureColor(Rule) * (Face == PreviewStreetFace ? 1.5f : 0.75f);
		Color.A = 1.0f;
		for (FVector& Point : Points) Point = PreviewVolumeTransform.TransformPosition(Point);
		const float Thickness = (Face == PreviewStreetFace ? 6.0f : 3.0f);
		PDI->SetHitProxy(new HDeepLevelBuildingFaceProxy(Face));
		for (int32 Index = 0; Index < 4; ++Index) PDI->DrawLine(Points[Index], Points[(Index + 1) % 4], Color, SDPG_Foreground, Thickness);
		PDI->DrawLine(Points[0], Points[2], Color, SDPG_World, 1.0f); PDI->DrawLine(Points[1], Points[3], Color, SDPG_World, 1.0f);

		FVector Center = (Points[0] + Points[1] + Points[2] + Points[3]) * 0.25f;
		PDI->DrawPoint(Center, Color, 32.0f, SDPG_Foreground);
		PDI->SetHitProxy(nullptr);
	};
	DrawFace(EDeepLevelBuildingVolumeFace::PositiveX, PreviewExposure.PositiveX);
	DrawFace(EDeepLevelBuildingVolumeFace::NegativeX, PreviewExposure.NegativeX);
	DrawFace(EDeepLevelBuildingVolumeFace::PositiveY, PreviewExposure.PositiveY);
	DrawFace(EDeepLevelBuildingVolumeFace::NegativeY, PreviewExposure.NegativeY);
	const FVector Pivot = PrimaryActor.IsValid() ? PrimaryActor->GetActorLocation() : FVector::ZeroVector;
	PDI->DrawPoint(Pivot, FLinearColor::Yellow, 18.0f, SDPG_Foreground);
}

void SDeepLevelBuildingCatalogPreviewViewport::DrawLabels(const FSceneView* View, FCanvas* Canvas) const
{
	if (!bHasPreviewVolume || !View || !Canvas) return;
	const FVector E = PreviewVolumeExtent;

	const auto DrawLabel = [&](const EDeepLevelBuildingVolumeFace Face, const EDeepLevelStreetExposureRule Rule)
	{
		FVector CenterLocal;
		if (Face == EDeepLevelBuildingVolumeFace::PositiveX) CenterLocal = FVector(E.X, 0, 0);
		else if (Face == EDeepLevelBuildingVolumeFace::NegativeX) CenterLocal = FVector(-E.X, 0, 0);
		else if (Face == EDeepLevelBuildingVolumeFace::PositiveY) CenterLocal = FVector(0, E.Y, 0);
		else if (Face == EDeepLevelBuildingVolumeFace::NegativeY) CenterLocal = FVector(0, -E.Y, 0);

		FVector CenterWorld = PreviewVolumeTransform.TransformPosition(CenterLocal);
		FVector2D ScreenPos;
		if (View->ScreenToPixel(View->WorldToScreen(CenterWorld), ScreenPos))
		{
			FString Text;
			switch (Rule)
			{
			case EDeepLevelStreetExposureRule::Required: Text = TEXT("REQUIRED"); break;
			case EDeepLevelStreetExposureRule::Preferred: Text = TEXT("PREFERRED"); break;
			case EDeepLevelStreetExposureRule::Forbidden: Text = TEXT("FORBIDDEN"); break;
			default: Text = TEXT("NEUTRAL"); break;
			}

			FCanvasTextItem TextItem(ScreenPos, FText::FromString(Text), GEngine->GetSmallFont(), ExposureColor(Rule));
			TextItem.bOutlined = true;
			TextItem.OutlineColor = FLinearColor::Black;
			TextItem.Scale = FVector2D(1.25f);
			TextItem.BlendMode = SE_BLEND_Translucent;

			TextItem.bCentreX = true;
			TextItem.bCentreY = true;
			TextItem.Position = ScreenPos - FVector2D(0.0f, 15.0f);
			Canvas->DrawItem(TextItem);
		}
	};
	DrawLabel(EDeepLevelBuildingVolumeFace::PositiveX, PreviewExposure.PositiveX);
	DrawLabel(EDeepLevelBuildingVolumeFace::NegativeX, PreviewExposure.NegativeX);
	DrawLabel(EDeepLevelBuildingVolumeFace::PositiveY, PreviewExposure.PositiveY);
	DrawLabel(EDeepLevelBuildingVolumeFace::NegativeY, PreviewExposure.NegativeY);
}
