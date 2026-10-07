// Copyright <--\, Inc. All Rights Reserved.

#include "City/Decoration/DeepLevelCityBuildingDecorationEditor.h"
#include "City/Decoration/DeepLevelCityDecorationAuthoring.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Editor.h"
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

FDeepLevelCityBuildingDecorationViewportClient::FDeepLevelCityBuildingDecorationViewportClient(
	const TSharedRef<FAdvancedPreviewScene>& Scene, const TSharedRef<SEditorViewport>& ViewportWidget, TWeakPtr<FDeepLevelCityBuildingDecorationEditor> InEditor)
	: FEditorViewportClient(nullptr, &Scene.Get(), ViewportWidget), SceneOwner(Scene), Editor(InEditor)
{
	bSetListenerPosition = false;
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
	if (Pinned && ViewportWidget) { StaticCastSharedPtr<SDeepLevelCityBuildingDecorationViewport>(ViewportWidget)->DrawSelection(PDI, Pinned->GetSelectedEntryId()); }
	if (!Pinned) { return; }
	if (const auto* Pair = Pinned->GetSelectedSymmetryPair())
	{
		const auto* First = Pinned->GetDecoration(Pair->First);
		const auto* Second = Pinned->GetDecoration(Pair->Second);
		if (First && Second)
		{
			PDI->DrawLine(First->LocalTransform.GetLocation(), Second->LocalTransform.GetLocation(), FLinearColor(0.2f, 0.8f, 1.0f), SDPG_Foreground);
			PDI->DrawPoint(Pair->PlaneOrigin, FLinearColor(0.2f, 0.8f, 1.0f), 8, SDPG_Foreground);
		}
	}
}

FVector FDeepLevelCityBuildingDecorationViewportClient::GetWidgetLocation() const
{
	if (const auto Pinned = Editor.Pin())
	{
		if (const auto* Entry = Pinned->GetSelectedEntry()) { return Entry->LocalTransform.GetLocation(); }
	}
	return FVector::ZeroVector;
}

FMatrix FDeepLevelCityBuildingDecorationViewportClient::GetWidgetCoordSystem() const
{
	if (GetWidgetCoordSystemSpace() == COORD_Local)
	{
		if (const auto Pinned = Editor.Pin())
		{
			if (const auto* Entry = Pinned->GetSelectedEntry()) { return FQuatRotationMatrix(Entry->LocalTransform.GetRotation()); }
		}
	}
	return FMatrix::Identity;
}

UE::Widget::EWidgetMode FDeepLevelCityBuildingDecorationViewportClient::GetWidgetMode() const
{
	const auto Pinned = Editor.Pin();
	return Pinned && Pinned->CanTransformSelection() ? FEditorViewportClient::GetWidgetMode() : UE::Widget::WM_None;
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
	if (IsAltPressed() || (Proxy && Proxy->IsA(HWidgetAxis::StaticGetType()))) { return; }
	FGuid Id;
	if (Proxy && Proxy->IsA(HDeepLevelCityDecorationHandle::StaticGetType()))
	{
		Id = static_cast<HDeepLevelCityDecorationHandle*>(Proxy)->EntryId;
	}
	else if (Proxy && Proxy->IsA(HActor::StaticGetType()))
	{
		Id = StaticCastSharedPtr<SDeepLevelCityBuildingDecorationViewport>(ViewportWidget)->FindEntryForActor(static_cast<HActor*>(Proxy)->Actor.Get());
	}
	if (Key == EKeys::LeftMouseButton || Id.IsValid()) { Pinned->SelectEntry(Id); }
	Invalidate();
	if (Key == EKeys::RightMouseButton)
	{
		FSlateApplication::Get().PushMenu(ViewportWidget.ToSharedRef(), FWidgetPath(),
			Pinned->BuildSelectionMenu().ToSharedRef(), FSlateApplication::Get().GetCursorPos(),
			FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
	}
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

void SDeepLevelCityBuildingDecorationViewport::ClearDecorations()
{
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
					Actor->SetActorTransform(Entry.LocalTransform);
					DecorationSurfaces.FindChecked(Entry.EntryGuid).UpdateActorTransform(Entry.LocalTransform);
				}
				if (auto* Decal = Cast<ADecalActor>(Actor)) { Decal->GetDecal()->DecalSize = Entry.DecalSize; }
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
	if (bBuildingChanged && BuildingActor.IsValid()) { Focus(false); }
	Client->Invalidate();
}

void SDeepLevelCityBuildingDecorationViewport::SetPlacementGuide(const FDeepLevelBuildingPlacementDefinition* Definition)
{
	PlacementGuide = Definition ? TOptional<FDeepLevelBuildingPlacementVolume>(Definition->PlacementVolume) : TOptional<FDeepLevelBuildingPlacementVolume>();
	Client->Invalidate();
}

void SDeepLevelCityBuildingDecorationViewport::UpdateEntry(const FGuid& Id, const FTransform& Transform)
{
	const auto* Actor = DecorationActors.Find(Id);
	if (Actor && Actor->IsValid()) { Actor->Get()->SetActorTransform(Transform); }
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

void SDeepLevelCityBuildingDecorationViewport::DrawSelection(FPrimitiveDrawInterface* PDI, const FGuid& Id) const
{
	if (PlacementGuide.IsSet())
	{
		const auto& Guide = PlacementGuide.GetValue();
		DrawWireBox(PDI, FTransform(Guide.Rotation, Guide.Center).ToMatrixWithScale(),
			FBox(-Guide.Extent, Guide.Extent), FLinearColor(0.2f, 1.0f, 0.4f), SDPG_Foreground);
	}
	for (const auto& Pair : DecorationActors)
	{
		if (!Pair.Value.IsValid()) { continue; }
		PDI->SetHitProxy(new HDeepLevelCityDecorationHandle(Pair.Key));
		PDI->DrawPoint(Pair.Value->GetActorLocation(), Pair.Key == Id ? FLinearColor::Yellow : FLinearColor(0.2f, 0.7f, 1.0f), 12, SDPG_Foreground);
		PDI->SetHitProxy(nullptr);
	}
	const auto* Selected = DecorationActors.Find(Id);
	if (!Selected || !Selected->IsValid()) { return; }
	AActor* Actor = Selected->Get();
	if (const auto* Decal = Cast<ADecalActor>(Actor))
	{
		const FVector Size = Decal->GetDecal()->DecalSize;
		DrawWireBox(PDI, Actor->GetActorTransform().ToMatrixWithScale(), FBox(-Size, Size), FLinearColor::Yellow, SDPG_Foreground);
		return;
	}
	const FBox Bounds = Actor->GetComponentsBoundingBox(true, true);
	if (Bounds.IsValid) { DrawWireBox(PDI, Bounds, FLinearColor::Yellow, SDPG_Foreground); }
}

void SDeepLevelCityBuildingDecorationViewport::Focus(bool bSelection)
{
	AActor* Actor = BuildingActor.Get();
	if (bSelection)
	{
		if (const auto Pinned = Editor.Pin())
		{
			const auto* Selected = DecorationActors.Find(Pinned->GetSelectedEntryId());
			if (Selected && Selected->IsValid()) { Actor = Selected->Get(); }
		}
	}
	if (!Actor) { return; }
	FBox Bounds = Actor->GetComponentsBoundingBox(true, true);
	if (!Bounds.IsValid || Bounds.GetExtent().IsNearlyZero()) { Bounds = FBox(Actor->GetActorLocation() - FVector(100), Actor->GetActorLocation() + FVector(100)); }
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
