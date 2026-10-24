// Copyright <--\, Inc. All Rights Reserved.

#include "City/Decoration/DeepLevelCityBuildingDecorationEditor.h"
#include "City/Decoration/DeepLevelCityDecorationAuthoring.h"
#include "Building/DeepLevelBuildingPreviewPresentation.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Editor.h"
#include "CanvasTypes.h"
#include "Engine/Engine.h"
#include "EditorModeManager.h"
#include "Engine/DecalActor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Slate/SceneViewport.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "UnrealWidget.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "DeepLevelCityBuildingDecorationViewport"

struct HDeepLevelCityDecorationHandle : public HHitProxy
{
	DECLARE_HIT_PROXY();
	FGuid EntryId;
	explicit HDeepLevelCityDecorationHandle(const FGuid& Id) : HHitProxy(HPP_UI), EntryId(Id) {}
};
IMPLEMENT_HIT_PROXY(HDeepLevelCityDecorationHandle, HHitProxy);

struct HDeepLevelCityDecorationPivot : public HHitProxy
{
	DECLARE_HIT_PROXY();
	FGuid Member;
	explicit HDeepLevelCityDecorationPivot(const FGuid& Id) : HHitProxy(HPP_UI), Member(Id) {}
};
IMPLEMENT_HIT_PROXY(HDeepLevelCityDecorationPivot, HHitProxy);


FDeepLevelCityBuildingDecorationViewportClient::FDeepLevelCityBuildingDecorationViewportClient(
	const TSharedRef<FAdvancedPreviewScene>& Scene, const TSharedRef<SEditorViewport>& ViewportWidget, TWeakPtr<FDeepLevelCityBuildingDecorationEditor> InEditor)
	: FEditorViewportClient(nullptr, &Scene.Get(), ViewportWidget), SceneOwner(Scene), Editor(InEditor)
{
	bSetListenerPosition = false;
	// This viewport transforms document entries through FWidget/InputWidgetDelta, not ITF actor gizmos.
	GetModeTools()->SetSupportsViewportITF(false);
	Widget->SetSnapEnabled(true);
	EngineShowFlags.SetSelectionOutline(true);
	EngineShowFlags.SetModeWidgets(true);
	SetRealtime(true);
}

void FDeepLevelCityBuildingDecorationViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);
	const auto Pinned = Editor.Pin();
	const auto ViewportWidget = EditorViewportWidget.Pin();
	if (Pinned && ViewportWidget) { StaticCastSharedPtr<SDeepLevelCityBuildingDecorationViewport>(ViewportWidget)->DrawSelection(PDI, Pinned->GetSelectedEntryIds()); }
	if (!Pinned) { return; }
	if (ViewportWidget) { StaticCastSharedPtr<SDeepLevelCityBuildingDecorationViewport>(ViewportWidget)->DrawSymmetryPreview(PDI); }
	for (const auto& Pair : Pinned->GetSymmetryPairs())
	{
		const auto& Selected = Pinned->GetSelectedEntryIds();
		if (!Selected.Contains(Pair.First) && !Selected.Contains(Pair.Second)) { continue; }
		const auto* First = Pinned->GetDecoration(Pair.First);
		const auto* Second = Pinned->GetDecoration(Pair.Second);
		if (!First || !Second) { continue; }
		const FVector Center = Pinned->GetSymmetryCenter(Pair);
		const FLinearColor Color = Pinned->IsSymmetryPivotSelected() ? FLinearColor::Yellow : FLinearColor(0.2f, 0.8f, 1.0f);
		PDI->DrawLine(First->LocalTransform.GetLocation(), Center, Color, SDPG_Foreground);
		PDI->DrawLine(Second->LocalTransform.GetLocation(), Center, Color, SDPG_Foreground);
		PDI->SetHitProxy(new HDeepLevelCityDecorationPivot(Pair.First));
		PDI->DrawPoint(Center, Color, 20, SDPG_Foreground);
		DrawWireDiamond(PDI, FTranslationMatrix(Center), 14, Color, SDPG_Foreground);
		PDI->SetHitProxy(nullptr);
	}
}

FVector FDeepLevelCityBuildingDecorationViewportClient::GetWidgetLocation() const
{
	if (const auto Pinned = Editor.Pin())
	{
		return Pinned->GetSelectionTransform().GetLocation();
	}
	return FVector::ZeroVector;
}

void FDeepLevelCityBuildingDecorationViewportClient::DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{
	FEditorViewportClient::DrawCanvas(InViewport, View, Canvas);
	if (const auto Preview = EditorViewportWidget.Pin())
	{
		const auto DecorationViewport = StaticCastSharedPtr<SDeepLevelCityBuildingDecorationViewport>(Preview);
		DecorationViewport->DrawExposureLabels(&View, &Canvas);
		DecorationViewport->DrawSymmetryAxisLabels(&View, &Canvas);
	}
}

FMatrix FDeepLevelCityBuildingDecorationViewportClient::GetWidgetCoordSystem() const
{
	if (GetWidgetCoordSystemSpace() == COORD_Local)
	{
		if (const auto Pinned = Editor.Pin())
		{
			return FQuatRotationMatrix(Pinned->GetSelectionTransform().GetRotation());
		}
	}
	return FMatrix::Identity;
}

UE::Widget::EWidgetMode FDeepLevelCityBuildingDecorationViewportClient::GetWidgetMode() const
{
	const auto Pinned = Editor.Pin();
	return Pinned && Pinned->CanTransformSelection() ? FEditorViewportClient::GetWidgetMode() : UE::Widget::WM_None;
}

bool FDeepLevelCityBuildingDecorationViewportClient::CanSetWidgetMode(UE::Widget::EWidgetMode NewMode) const
{
	const auto Pinned = Editor.Pin();
	return Pinned && Pinned->CanTransformSelection()
		&& (NewMode == UE::Widget::WM_Translate || NewMode == UE::Widget::WM_Rotate || NewMode == UE::Widget::WM_Scale);
}

bool FDeepLevelCityBuildingDecorationViewportClient::InputWidgetDelta(FViewport* InViewport, EAxisList::Type Axis,
	FVector& Drag, FRotator& Rotation, FVector& Scale)
{
	if (Axis == EAxisList::None || (Drag.IsNearlyZero() && Rotation.IsNearlyZero() && Scale.IsNearlyZero())) { return false; }
	const auto Pinned = Editor.Pin();
	if (!Pinned || !Pinned->CanTransformSelection()) { return false; }
	FHitResult Hit;
	const FHitResult* SurfaceHit = nullptr;
	if (Pinned->IsSurfacePlacementEnabled() && !Drag.IsNearlyZero())
	{
		FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(InViewport, GetScene(), EngineShowFlags));
		FSceneView* View = CalcSceneView(&Family);
		if (!View || !TraceBuilding(*View, InViewport->GetMouseX(), InViewport->GetMouseY(), Hit))
		{
			Pinned->ReportPreviewError(LOCTEXT("NoDragSurface", "No building or decoration surface under the cursor."));
			return true;
		}
		SurfaceHit = &Hit;
	}
	Pinned->ApplyWidgetDelta(Drag, Rotation, Scale, SurfaceHit);
	Invalidate();
	return true;
}

void FDeepLevelCityBuildingDecorationViewportClient::TrackingStopped()
{
	if (const auto Pinned = Editor.Pin()) { Pinned->EndWidgetDrag(); }
	FEditorViewportClient::TrackingStopped();
}

bool FDeepLevelCityBuildingDecorationViewportClient::TraceBuilding(FSceneView& View, int32 X, int32 Y, FHitResult& Hit) const
{
	const auto ViewportWidget = EditorViewportWidget.Pin();
	if (!ViewportWidget) { return false; }
		FViewportCursorLocation Cursor(&View, const_cast<FDeepLevelCityBuildingDecorationViewportClient*>(this), X, Y);
	return StaticCastSharedPtr<SDeepLevelCityBuildingDecorationViewport>(ViewportWidget)->TraceSurface(
		Cursor.GetOrigin(), Cursor.GetOrigin() + Cursor.GetDirection() * 1000000.0, Hit);
}

void FDeepLevelCityBuildingDecorationViewportClient::ProcessClick(FSceneView& View, HHitProxy* Proxy,
	FKey Key, EInputEvent Event, uint32 X, uint32 Y)
{
	const auto Pinned = Editor.Pin();
	const auto ViewportWidget = EditorViewportWidget.Pin();
	// FEditorViewportClient dispatches normal clicks on release, after excluding drags.
	if (!Pinned || !ViewportWidget || (Event != IE_Released && Event != IE_DoubleClick)
		|| (Key != EKeys::LeftMouseButton && Key != EKeys::RightMouseButton)) { return; }
	if (IsAltPressed()) { return; }
	if (Pinned->IsPlacingSymmetry())
	{
		if (Key == EKeys::RightMouseButton) { Pinned->CancelSymmetryPlacement(); }
		else if (UpdateSymmetryCursor(View, X, Y)) { Pinned->CommitSymmetryPlacement(); }
		Invalidate();
		return;
	}
	if (Proxy && Proxy->IsA(HWidgetAxis::StaticGetType())) { return; }
	if (Proxy && Proxy->IsA(HDeepLevelCityDecorationPivot::StaticGetType()))
	{
		Pinned->SelectSymmetryPivot(static_cast<HDeepLevelCityDecorationPivot*>(Proxy)->Member);
		Invalidate();
		if (Key == EKeys::LeftMouseButton) { return; }
	}
	FGuid Id;
	if (Proxy && Proxy->IsA(HDeepLevelCityDecorationHandle::StaticGetType()))
	{
		Id = static_cast<HDeepLevelCityDecorationHandle*>(Proxy)->EntryId;
	}
	else if (Proxy && Proxy->IsA(HActor::StaticGetType()))
	{
		Id = StaticCastSharedPtr<SDeepLevelCityBuildingDecorationViewport>(ViewportWidget)->FindEntryForActor(static_cast<HActor*>(Proxy)->Actor.Get());
	}
	if (Key == EKeys::LeftMouseButton) { Pinned->SelectEntry(Id, IsCtrlPressed(), IsShiftPressed()); }
	else if (Id.IsValid() && !Pinned->GetSelectedEntryIds().Contains(Id)) { Pinned->SelectEntry(Id); }
	Invalidate();
	if (Key == EKeys::RightMouseButton)
	{
		FSlateApplication::Get().PushMenu(ViewportWidget.ToSharedRef(), FWidgetPath(),
			Pinned->BuildSelectionMenu().ToSharedRef(), FSlateApplication::Get().GetCursorPos(),
			FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
	}
}

bool FDeepLevelCityBuildingDecorationViewportClient::UpdateSymmetryCursor(FSceneView& View, int32 X, int32 Y)
{
	const auto Pinned = Editor.Pin();
	if (!Pinned || !Pinned->IsPlacingSymmetry()) { return false; }
	const FVector SelectionCenter = Pinned->GetSelectionTransform().GetLocation();
	FVector Center;
	FHitResult Hit;
	if (TraceBuilding(View, X, Y, Hit))
	{
		// Keep the source group's facade standoff while placing on its supporting surface.
		Center = Hit.ImpactPoint + Hit.ImpactNormal * FVector::DotProduct(SelectionCenter - Hit.ImpactPoint, Hit.ImpactNormal);
	}
	else
	{
		const FViewportCursorLocation Cursor(&View, this, X, Y);
		const FVector Normal = View.GetViewDirection();
		const double Denominator = FVector::DotProduct(Cursor.GetDirection(), Normal);
		if (FMath::Abs(Denominator) < SMALL_NUMBER) { return false; }
		const double Distance = FVector::DotProduct(SelectionCenter - Cursor.GetOrigin(), Normal) / Denominator;
		if (Distance <= 0.0) { return false; }
		Center = Cursor.GetOrigin() + Cursor.GetDirection() * Distance;
	}
	return Pinned->UpdateSymmetryPlacement(Center, &View, IsShiftPressed());
}

void FDeepLevelCityBuildingDecorationViewportClient::MouseMove(FViewport* InViewport, int32 X, int32 Y)
{
	FEditorViewportClient::MouseMove(InViewport, X, Y);
	const auto Pinned = Editor.Pin();
	if (!Pinned || !Pinned->IsPlacingSymmetry() || IsAltPressed()) { return; }
	FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(InViewport, GetScene(), EngineShowFlags));
	if (FSceneView* View = CalcSceneView(&Family)) { UpdateSymmetryCursor(*View, X, Y); }
}

bool FDeepLevelCityBuildingDecorationViewportClient::InputKey(const FInputKeyEventArgs& Args)
{
	const auto Pinned = Editor.Pin();
	if (Pinned && Pinned->IsPlacingSymmetry() && Args.Event == IE_Pressed)
	{
		if (Args.Key == EKeys::Escape) { Pinned->CancelSymmetryPlacement(); return true; }
		if (Args.Key == EKeys::X || Args.Key == EKeys::Y || Args.Key == EKeys::Z)
		{
			Pinned->ToggleSymmetryPlacementAxis(Args.Key == EKeys::X ? EAxis::X : Args.Key == EKeys::Y ? EAxis::Y : EAxis::Z);
			return true;
		}
	}
	const bool bHandled = FEditorViewportClient::InputKey(Args);
	if (Pinned && Pinned->IsPlacingSymmetry() && (Args.Key == EKeys::LeftShift || Args.Key == EKeys::RightShift))
	{
		Pinned->RefreshSymmetryPlacement();
	}
	return bHandled;
}

void FDeepLevelCityBuildingDecorationViewportClient::LostFocus(FViewport* InViewport)
{
	if (const auto Pinned = Editor.Pin()) { Pinned->CancelSymmetryPlacement(); }
	FEditorViewportClient::LostFocus(InViewport);
}

void SDeepLevelCityBuildingDecorationViewport::Construct(const FArguments& Args)
{
	Editor = Args._Editor;
	PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
	PreviewScene->SetFloorVisibility(true, true);
	SEditorViewport::Construct(SEditorViewport::FArguments());
	// SetWidgetMode requires the scene viewport created by SEditorViewport.
	Client->SetViewMode(VMI_Lit);
	Client->SetWidgetMode(UE::Widget::WM_Translate);
	Client->SetInitialViewTransform(LVT_Perspective, FVector(-2500, -3500, 2200), FRotator(-20, 45, 0), DEFAULT_ORTHOZOOM);
}

SDeepLevelCityBuildingDecorationViewport::~SDeepLevelCityBuildingDecorationViewport()
{
	GetSceneViewport()->SetViewportClient(TStrongPtrVariant<FViewportClient>(nullptr));
	ClearDecorations();
	if (BuildingActor.IsValid()) { PreviewScene->GetWorld()->DestroyActor(BuildingActor.Get()); }
	Client.Reset(); PreviewScene.Reset();
}

TSharedRef<FEditorViewportClient> SDeepLevelCityBuildingDecorationViewport::MakeEditorViewportClient()
{
	Client = MakeShared<FDeepLevelCityBuildingDecorationViewportClient>(PreviewScene.ToSharedRef(), SharedThis(this), Editor);
	return Client.ToSharedRef();
}

AActor* SDeepLevelCityBuildingDecorationViewport::Spawn(UClass* Class, const FTransform& Transform)
{
	if (!Class || Class->HasAnyClassFlags(CLASS_Abstract) || !Class->IsChildOf(AActor::StaticClass())) { return nullptr; }
	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	Params.bTemporaryEditorActor = true;
	AActor* Actor = PreviewScene->GetWorld()->SpawnActor<AActor>(Class, Transform, Params);
	if (Actor) { Actor->SetActorTickEnabled(false); }
	return Actor;
}

void SDeepLevelCityBuildingDecorationViewport::ApplyActorTransform(AActor* Actor, const FTransform& Transform)
{
	if (!Actor) { return; }
	const bool bWasMirrored = Actor->GetActorTransform().GetDeterminant() < 0.0;
	Actor->SetActorTransform(Transform);
	if (bWasMirrored != (Actor->GetActorTransform().GetDeterminant() < 0.0))
	{
		// Cached mesh draw commands retain their winding when a live transform changes handedness.
		TInlineComponentArray<UPrimitiveComponent*> Components;
		Actor->GetComponents(Components, true);
		for (auto* Component : Components) { Component->MarkRenderStateDirty(); }
	}
}

void SDeepLevelCityBuildingDecorationViewport::ClearDecorations()
{
	ClearSymmetryPreview();
	for (const auto& Pair : DecorationActors)
	{
		if (Pair.Value.IsValid()) { PreviewScene->GetWorld()->DestroyActor(Pair.Value.Get()); }
	}
	DecorationActors.Reset();
	DecorationSurfaces.Reset();
	PreviewEntries.Reset();
}

void SDeepLevelCityBuildingDecorationViewport::BindCommands()
{
	SEditorViewport::BindCommands();
	if (const auto Pinned = Editor.Pin()) { Pinned->BindViewportCommands(*CommandList); }
}

void SDeepLevelCityBuildingDecorationViewport::OnFocusViewportToSelection()
{
	const auto Pinned = Editor.Pin();
	Focus(Pinned && Pinned->GetSelectedEntry());
}

AActor* SDeepLevelCityBuildingDecorationViewport::GetEntryActor(const FGuid& Id) const
{
	const auto* Actor = DecorationActors.Find(Id);
	return Actor ? Actor->Get() : nullptr;
}

AActor* SDeepLevelCityBuildingDecorationViewport::SpawnEntry(const FDeepLevelCityBuildingDecorationEntry& Entry)
{
	switch (Entry.Output)
	{
	case EDeepLevelCityDecorationOutput::Mesh:
		if (auto* Actor = Cast<AStaticMeshActor>(Spawn(AStaticMeshActor::StaticClass(), Entry.LocalTransform)))
		{
			Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Actor->GetStaticMeshComponent()->SetStaticMesh(Entry.Mesh.LoadSynchronous());
			return Actor;
		}
		break;
	case EDeepLevelCityDecorationOutput::Actor:
		return Spawn(Entry.ActorClass.LoadSynchronous(), Entry.LocalTransform);
	case EDeepLevelCityDecorationOutput::Decal:
		if (auto* Actor = Cast<ADecalActor>(Spawn(ADecalActor::StaticClass(), Entry.LocalTransform)))
		{
			// SpawnActor composes the decal root's default downward rotation with the spawn pose.
			Actor->SetActorTransform(Entry.LocalTransform);
			Actor->GetDecal()->DecalSize = Entry.DecalSize;
			Actor->SetDecalMaterial(Entry.DecalMaterial.LoadSynchronous());
			return Actor;
		}
		break;
	}
	return nullptr;
}

void SDeepLevelCityBuildingDecorationViewport::Synchronize(const TSoftClassPtr<AActor>& BuildingClass,
	const UDeepLevelCityBuildingDecorationProfile* Profile, const FGuid& VariantId)
{
	const auto Pinned = Editor.Pin();
	if (Pinned) { Pinned->ClearPreviewErrors(); }
	UClass* Class = BuildingClass.LoadSynchronous();
	const bool bBuildingChanged = !BuildingActor.IsValid() || BuildingActor->GetClass() != Class;
	if (bBuildingChanged)
	{
		ClearDecorations();
		if (BuildingActor.IsValid()) { PreviewScene->GetWorld()->DestroyActor(BuildingActor.Get()); }
		BuildingActor = Spawn(Class, FTransform::Identity);
		Surface.Build(BuildingActor.Get());
		if (!BuildingActor.IsValid() && Pinned && !BuildingClass.IsNull())
		{
			Pinned->ReportPreviewError(LOCTEXT("BuildingSpawn", "Cannot create the selected building preview."));
		}
	}
	const auto* Variant = Profile && VariantId.IsValid() ? DeepLevelCityDecorationAuthoring::FindVariant(*Profile, VariantId) : nullptr;
	TSet<FGuid> Active;
	if (Variant)
	{
		for (const auto& Entry : Variant->Entries)
		{
			FText Error;
			if (!Entry.EntryGuid.IsValid() || Active.Contains(Entry.EntryGuid)) { continue; }
			if (!Entry.Validate(Error)) { continue; }
			Active.Add(Entry.EntryGuid);
			const auto* Old = PreviewEntries.Find(Entry.EntryGuid);
			AActor* Actor = GetEntryActor(Entry.EntryGuid);
			const bool bSameOutput = Old && Old->Output == Entry.Output && Old->Mesh == Entry.Mesh
				&& Old->ActorClass == Entry.ActorClass && Old->DecalMaterial == Entry.DecalMaterial;
			if (Actor && !bSameOutput)
			{
				PreviewScene->GetWorld()->DestroyActor(Actor);
				Actor = nullptr;
				DecorationActors.Remove(Entry.EntryGuid);
				DecorationSurfaces.Remove(Entry.EntryGuid);
			}
			if (!Actor)
			{
				Actor = SpawnEntry(Entry);
				if (Actor)
				{
					DecorationActors.Add(Entry.EntryGuid, Actor);
					DecorationSurfaces.FindOrAdd(Entry.EntryGuid).Build(Actor);
				}
				else if (Pinned)
				{
					Pinned->ReportPreviewError(FText::Format(LOCTEXT("SpawnEntry", "Cannot spawn decoration '{0}' in the preview."), FText::FromName(Entry.Name)), Entry.EntryGuid);
				}
			}
			else
			{
				if (!Old->LocalTransform.Equals(Entry.LocalTransform))
				{
					ApplyActorTransform(Actor, Entry.LocalTransform);
					DecorationSurfaces.FindChecked(Entry.EntryGuid).UpdateActorTransform(Entry.LocalTransform);
				}
				if (auto* Decal = Cast<ADecalActor>(Actor); Decal && !Decal->GetDecal()->DecalSize.Equals(Entry.DecalSize))
				{
					Decal->GetDecal()->DecalSize = Entry.DecalSize;
					Decal->GetDecal()->MarkRenderStateDirty();
					Decal->GetDecal()->UpdateBounds();
				}
			}
			PreviewEntries.Add(Entry.EntryGuid, Entry);
		}
	}
	for (auto It = DecorationActors.CreateIterator(); It; ++It)
	{
		if (Active.Contains(It.Key())) { continue; }
		if (It.Value().IsValid()) { PreviewScene->GetWorld()->DestroyActor(It.Value().Get()); }
		PreviewEntries.Remove(It.Key());
		DecorationSurfaces.Remove(It.Key());
		It.RemoveCurrent();
	}
	if (bBuildingChanged && BuildingActor.IsValid())
	{
		if (PlacementGuide.IsSet())
		{
			FBox Bounds = BuildingActor->GetComponentsBoundingBox(true, true);
			if (!Bounds.IsValid || Bounds.GetExtent().IsNearlyZero())
			{
				const auto& Volume = PlacementGuide.GetValue();
				Bounds = FBox(-Volume.Extent, Volume.Extent).TransformBy(FTransform(Volume.Rotation, Volume.Center));
			}
			DeepLevelBuildingPreviewPresentation::Focus(*Client, Bounds, PlacementGuide.GetValue());
		}
		else { Focus(false); }
	}
	Client->Invalidate();
}

void SDeepLevelCityBuildingDecorationViewport::SetPlacementGuide(const FDeepLevelBuildingPlacementDefinition* Definition, bool bShowBounds)
{
	PlacementGuide = Definition ? TOptional<FDeepLevelBuildingPlacementVolume>(Definition->PlacementVolume) : TOptional<FDeepLevelBuildingPlacementVolume>();
	bDrawPlacementBounds = bShowBounds;
	Client->Invalidate();
}

void SDeepLevelCityBuildingDecorationViewport::DrawExposureLabels(const FSceneView* View, FCanvas* Canvas) const
{
	if (PlacementGuide.IsSet()) { DeepLevelBuildingPreviewPresentation::DrawLabels(View, Canvas, PlacementGuide.GetValue()); }
}

void SDeepLevelCityBuildingDecorationViewport::UpdateEntry(const FGuid& Id, const FTransform& Transform)
{
	const auto* Existing = PreviewEntries.Find(Id);
	if (Existing && Existing->LocalTransform.Equals(Transform)) { return; }
	const auto* Actor = DecorationActors.Find(Id);
	if (Actor && Actor->IsValid() && !Actor->Get()->GetActorTransform().Equals(Transform)) { ApplyActorTransform(Actor->Get(), Transform); }
	if (auto* Entry = PreviewEntries.Find(Id)) { Entry->LocalTransform = Transform; }
	if (auto* Geometry = DecorationSurfaces.Find(Id)) { Geometry->UpdateActorTransform(Transform); }
	Client->Invalidate();
}

FGuid SDeepLevelCityBuildingDecorationViewport::FindEntryForActor(const AActor* Actor) const
{
	for (const AActor* Candidate = Actor; Candidate; Candidate = Candidate->GetParentActor())
	{
		for (const auto& Pair : DecorationActors) { if (Pair.Value.Get() == Candidate) { return Pair.Key; } }
	}
	return FGuid();
}

void SDeepLevelCityBuildingDecorationViewport::DrawSelection(FPrimitiveDrawInterface* PDI, TConstArrayView<FGuid> Ids) const
{
	if (PlacementGuide.IsSet())
	{
		const auto& Guide = PlacementGuide.GetValue();
		DeepLevelBuildingPreviewPresentation::DrawExposure(PDI, Guide);
		if (bDrawPlacementBounds) { DrawWireBox(PDI, FTransform(Guide.Rotation, Guide.Center).ToMatrixWithScale(),
			FBox(-Guide.Extent, Guide.Extent), FLinearColor(0.2f, 1.0f, 0.4f), SDPG_Foreground);
		}
	}
	for (const auto& Pair : DecorationActors)
	{
		if (!Pair.Value.IsValid()) { continue; }
		PDI->SetHitProxy(new HDeepLevelCityDecorationHandle(Pair.Key));
		PDI->DrawPoint(Pair.Value->GetActorLocation(), Ids.Contains(Pair.Key) ? FLinearColor::Yellow : FLinearColor(0.2f, 0.7f, 1.0f), 12, SDPG_Foreground);
		PDI->SetHitProxy(nullptr);
	}
	for (const FGuid& Id : Ids)
	{
		AActor* Actor = GetEntryActor(Id);
		if (!Actor) { continue; }
		if (const auto* Decal = Cast<ADecalActor>(Actor))
		{
			const FVector Size = Decal->GetDecal()->DecalSize;
			DrawWireBox(PDI, Actor->GetActorTransform().ToMatrixWithScale(), FBox(-Size, Size), FLinearColor::Yellow, SDPG_Foreground);
			continue;
		}
		const FBox Bounds = Actor->GetComponentsBoundingBox(true, true);
		if (Bounds.IsValid) { DrawWireBox(PDI, Bounds, FLinearColor::Yellow, SDPG_Foreground); }
	}
}

void SDeepLevelCityBuildingDecorationViewport::Focus(bool bSelection)
{
	FBox Bounds(ForceInit);
	if (bSelection)
	{
		if (const auto Pinned = Editor.Pin())
		{
			for (const FGuid& Id : Pinned->GetSelectedEntryIds())
			{
				if (AActor* Actor = GetEntryActor(Id)) { Bounds += Actor->GetComponentsBoundingBox(true, true); }
			}
			if (Pinned->IsSymmetryPivotSelected())
			{
				if (const auto* Pair = Pinned->GetSelectedSymmetryPair())
				{
					Bounds += Pinned->GetSymmetryCenter(*Pair);
					if (AActor* Actor = GetEntryActor(Pair->Second)) { Bounds += Actor->GetComponentsBoundingBox(true, true); }
				}
			}
		}
	}
	if (!Bounds.IsValid && BuildingActor.IsValid()) { Bounds = BuildingActor->GetComponentsBoundingBox(true, true); }
	if (!Bounds.IsValid) { return; }
	if (Bounds.GetExtent().IsNearlyZero()) { Bounds = Bounds.ExpandBy(100); }
	Client->FocusViewportOnBox(Bounds, true);
	Client->Invalidate();
}

FReply SDeepLevelCityBuildingDecorationViewport::OnDragOver(const FGeometry&, const FDragDropEvent& Event)
{
	return Event.GetOperationAs<FAssetDragDropOp>() ? FReply::Handled() : FReply::Unhandled();
}

FReply SDeepLevelCityBuildingDecorationViewport::OnDrop(const FGeometry& Geometry, const FDragDropEvent& Event)
{
	const auto Operation = Event.GetOperationAs<FAssetDragDropOp>();
	const auto Pinned = Editor.Pin();
	if (!Operation || !Pinned || !Client->Viewport) { return FReply::Unhandled(); }
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Client->Viewport, Client->GetScene(), Client->EngineShowFlags));
	FSceneView* View = Client->CalcSceneView(&ViewFamily);
	if (!View) { return FReply::Handled(); }
	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	const FVector2D Size = Geometry.GetLocalSize();
	const FIntPoint Pixels = Client->Viewport->GetSizeXY();
	FHitResult Hit;
	if (Size.X > 0 && Size.Y > 0 && Client->TraceBuilding(*View, Local.X * Pixels.X / Size.X, Local.Y * Pixels.Y / Size.Y, Hit))
	{
		Pinned->AddDroppedAssets(Operation->GetAssets(), Hit.ImpactPoint, Hit.ImpactNormal);
	}
	else { Pinned->ReportPreviewError(LOCTEXT("DropMissedSurface", "Drop onto a supported building render surface. Static meshes and packed instances do not require gameplay collision.")); }
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE


bool SDeepLevelCityBuildingDecorationViewport::TraceSurface(const FVector& Start, const FVector& End, FHitResult& Hit) const
{
	bool bFound = Surface.Trace(Start, End, Hit);
	const auto Pinned = Editor.Pin();
	const FGuid Excluded = Pinned ? Pinned->GetSelectedEntryId() : FGuid();
	const auto* Linked = Pinned ? Pinned->GetSelectedSymmetryPair() : nullptr;
	for (const auto& Pair : DecorationSurfaces)
	{
		if (Pair.Key == Excluded || (Linked && (Pair.Key == Linked->First || Pair.Key == Linked->Second))) { continue; }
		FHitResult Candidate;
		if (Pair.Value.Trace(Start, End, Candidate) && (!bFound || Candidate.Distance < Hit.Distance))
		{
			Hit = Candidate;
			bFound = true;
		}
	}
	return bFound;
}

bool SDeepLevelCityBuildingDecorationViewport::GetSurfacePlacementTransform(const FGuid& Id,
	const FTransform& Desired, const FVector& Point, const FVector& Normal, FTransform& Result) const
{
	const auto* Entry = PreviewEntries.Find(Id);
	const FVector Direction = Normal.GetSafeNormal();
	if (!Entry || Direction.IsNearlyZero()) { return false; }
	const auto& Settings = GetDefault<ULevelEditorViewportSettings>()->SnapToSurface;
	FTransform Pose = Desired;
	const FVector Target = Point + Direction * Settings.SnapOffsetExtent;
	if (Entry->Output == EDeepLevelCityDecorationOutput::Decal)
	{
		Pose.SetLocation(Target);
		Pose.SetRotation(FRotationMatrix::MakeFromX(-Direction).ToQuat());
		Result = Pose;
		return true;
	}
	if (Settings.bSnapRotation)
	{
		Pose.SetRotation(FQuat::FindBetweenNormals(Pose.GetRotation().GetUpVector(), Direction) * Pose.GetRotation());
	}
	const auto* Geometry = DecorationSurfaces.Find(Id);
	return Geometry && Geometry->GetContactTransform(Pose, Target, Direction, Result);
}
