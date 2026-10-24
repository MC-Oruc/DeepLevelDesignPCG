// Copyright <--\, Inc. All Rights Reserved.
#include "City/Decoration/DeepLevelCityBuildingDecorationEditor.h"
#include "City/Decoration/DeepLevelCityDecorationAuthoring.h"
#include "Components/DecalComponent.h"
#include "Components/LightComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/World.h"
#include "Editor.h"
#include "SnappingUtils.h"
#include "CanvasTypes.h"
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SceneManagement.h"
#include "SceneView.h"

#define LOCTEXT_NAMESPACE "DeepLevelCityDecorationSymmetryPreview"

TConstArrayView<FDeepLevelCityDecorationSymmetryPair> FDeepLevelCityBuildingDecorationEditor::GetSymmetryPairs() const
{
	const auto* Variant = Document->GetVariant();
	return Variant ? MakeArrayView(Variant->SymmetryPairs) : TConstArrayView<FDeepLevelCityDecorationSymmetryPair>();
}

namespace
{
FVector SymmetryAxis(EAxis::Type Axis)
{
	return Axis == EAxis::X ? FVector::ForwardVector : Axis == EAxis::Y ? FVector::RightVector : FVector::UpVector;
}

FLinearColor SymmetryAxisColor(EAxis::Type Axis)
{
	return Axis == EAxis::X ? FLinearColor(1, 0.15f, 0.12f) : Axis == EAxis::Y ? FLinearColor(0.2f, 1, 0.2f) : FLinearColor(0.2f, 0.45f, 1);
}

FText SymmetryAxisText(EAxis::Type Axis)
{
	return FText::AsCultureInvariant(Axis == EAxis::X ? TEXT("X") : Axis == EAxis::Y ? TEXT("Y") : TEXT("Z"));
}

EAxis::Type FindNearbySymmetryAxis(const FVector& Origin, const FVector& Point, const FQuat& Basis,
	const FSceneView& View, EAxis::Type Previous)
{
	FVector2D CursorPixel, OriginPixel;
	if (!View.WorldToPixel(Point, CursorPixel) || !View.WorldToPixel(Origin, OriginPixel)) { return EAxis::None; }
	const auto Distance = [&](EAxis::Type Axis)
	{
		const FVector Direction = Basis.RotateVector(SymmetryAxis(Axis));
		const FVector OnAxis = Origin + Direction * FVector::DotProduct(Point - Origin, Direction);
		FVector2D AxisPixel;
		if (!View.WorldToPixel(OnAxis, AxisPixel) || FVector2D::Distance(OriginPixel, AxisPixel) < 5.0) { return TNumericLimits<double>::Max(); }
		return static_cast<double>(FVector2D::Distance(CursorPixel, AxisPixel));
	};
	// A wider release radius prevents the cursor from flickering in and out of a guide.
	if (Previous != EAxis::None && Distance(Previous) <= 22.0) { return Previous; }
	EAxis::Type Closest = EAxis::None;
	double Best = 14.0;
	for (const auto Axis : {EAxis::X, EAxis::Y, EAxis::Z})
	{
		const double Pixels = Distance(Axis);
		if (Pixels < Best) { Best = Pixels; Closest = Axis; }
	}
	return Closest;
}
}

FQuat FDeepLevelCityBuildingDecorationEditor::GetSymmetryPlacementBasis() const
{
	return Preview->GetClient()->GetWidgetCoordSystemSpace() == COORD_Local
		? Document->GetSelectionTransform().GetRotation() : FQuat::Identity;
}

void FDeepLevelCityBuildingDecorationEditor::RefreshSymmetryPlacement()
{
	if (!IsPlacingSymmetry()) { return; }
	const auto Client = Preview->GetClient();
	if (!Client->Viewport) { return; }
	FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(Client->Viewport, Client->GetScene(), Client->EngineShowFlags));
	if (FSceneView* View = Client->CalcSceneView(&Family))
	{
		Client->UpdateSymmetryCursor(*View, Client->Viewport->GetMouseX(), Client->Viewport->GetMouseY());
	}
	Client->Invalidate();
}

void FDeepLevelCityBuildingDecorationEditor::BeginSymmetryPlacement(EAxis::Type Axis)
{
	if (!Document->CanPlaceSymmetry()) { return; }
	Document->EndDrag();
	SymmetryPlacementAxis = Axis;
	SymmetrySnappedAxis = EAxis::None;
	bSymmetryFreeAxis = false;
	SymmetryPlacementCenter.Reset();
	Preview->ClearSymmetryPreview();
	OperationError = FText::GetEmpty();
	RefreshSymmetryPlacement();
}

void FDeepLevelCityBuildingDecorationEditor::ToggleSymmetryPlacementAxis(EAxis::Type Axis)
{
	if (!IsPlacingSymmetry()) { return; }
	SymmetryPlacementAxis = SymmetryPlacementAxis.GetValue() == Axis ? EAxis::None : Axis;
	SymmetrySnappedAxis = EAxis::None;
	RefreshSymmetryPlacement();
}

bool FDeepLevelCityBuildingDecorationEditor::UpdateSymmetryPlacement(const FVector& RawCenter, const FSceneView* View, bool bFreeAxis)
{
	if (!IsPlacingSymmetry()) { return false; }
	const FVector Origin = Document->GetSelectionTransform().GetLocation();
	const FQuat Basis = GetSymmetryPlacementBasis();
	EAxis::Type Axis = bFreeAxis ? EAxis::None : SymmetryPlacementAxis.GetValue();
	if (!bFreeAxis && Axis == EAxis::None && View)
	{
		Axis = FindNearbySymmetryAxis(Origin, RawCenter, Basis, *View, SymmetrySnappedAxis);
	}
	SymmetrySnappedAxis = Axis;
	bSymmetryFreeAxis = bFreeAxis;
	FVector Delta = Basis.UnrotateVector(RawCenter - Origin);
	if (Axis != EAxis::None)
	{
		const FVector Direction = SymmetryAxis(Axis);
		Delta = Direction * FVector::DotProduct(Delta, Direction);
	}
	// Use the same global switch and step as the native viewport toolbar.
	FSnappingUtils::SnapPointToGrid(Delta, FVector::ZeroVector);
	const FVector Center = Origin + Basis.RotateVector(Delta);
	const FVector Normal = Axis == EAxis::None ? (Center - Origin).GetSafeNormal() : Basis.RotateVector(SymmetryAxis(Axis));
	TArray<FDeepLevelCityBuildingDecorationEntry> Copies;
	FText Error;
	if ((Center - Origin).IsNearlyZero() || !Document->BuildSymmetryPreview(Center, Normal, Copies, Error) || !Preview->SetSymmetryPreview(Copies))
	{
		SymmetryPlacementCenter.Reset();
		Preview->ClearSymmetryPreview();
		Preview->GetClient()->Invalidate();
		return false;
	}
	SymmetryPlacementCenter = Center;
	SymmetryPlacementNormal = Normal;
	Preview->GetClient()->Invalidate();
	return true;
}

FDeepLevelCityDecorationViewportShortcut FDeepLevelCityBuildingDecorationEditor::GetViewportShortcut(int32 Row) const
{
	if (IsPlacingSymmetry())
	{
		switch (Row)
		{
		case 0: return {FText::AsCultureInvariant(TEXT("X / Y / Z")), LOCTEXT("MirrorAxisHelp", "Lock / unlock mirror axis")};
		case 1: return {FText::AsCultureInvariant(TEXT("Shift")), LOCTEXT("MirrorFreeHelp", "Hold for free mirror placement")};
		case 2: return {FText::AsCultureInvariant(TEXT("LMB")), LOCTEXT("MirrorConfirmHelp", "Confirm mirror placement")};
		case 3: return {FText::AsCultureInvariant(TEXT("Esc / RMB")), LOCTEXT("MirrorCancelHelp", "Cancel mirror preview")};
		default: return {};
		}
	}
	if (!Document->GetEntry()) { return {}; }
	if (Row == 0) { return {FText::AsCultureInvariant(TEXT("RMB")), LOCTEXT("SymmetryActionsHelp", "Decoration / symmetry actions")}; }
	if (Row == 1 && Document->GetSymmetryPair())
	{
		return {LOCTEXT("CenterPointKey", "Cyan point"), LOCTEXT("CenterPointHelp", "Edit the linked pair center")};
	}
	return {};
}

FText FDeepLevelCityBuildingDecorationEditor::GetViewportStatus() const
{
	if (!Document->GetVariant()) { return FText::GetEmpty(); }
	const FText Grid = FSnappingUtils::IsSnapToGridEnabled()
		? FText::Format(LOCTEXT("GridStep", "Grid snap: {0} cm"), FText::AsNumber(GEditor->GetGridSize()))
		: LOCTEXT("GridOff", "Grid snap: off");
	const FText Space = Preview->GetClient()->GetWidgetCoordSystemSpace() == COORD_Local
		? LOCTEXT("LocalAxes", "Local axes") : LOCTEXT("WorldAxes", "World axes");
	if (IsPlacingSymmetry())
	{
		FText Mode = LOCTEXT("AutoAxes", "Auto axis attraction");
		if (bSymmetryFreeAxis) { Mode = LOCTEXT("FreeAxis", "Free placement (Shift)"); }
		else if (SymmetryPlacementAxis.GetValue() != EAxis::None)
		{
			Mode = FText::Format(LOCTEXT("LockedAxis", "Locked to {0}"), SymmetryAxisText(SymmetryPlacementAxis.GetValue()));
		}
		else if (SymmetrySnappedAxis != EAxis::None)
		{
			Mode = FText::Format(LOCTEXT("AttractedAxis", "Snapped to {0}"), SymmetryAxisText(SymmetrySnappedAxis));
		}
		const FText State = HasSymmetryPreview() ? LOCTEXT("MirrorReady", "Mirror preview") : LOCTEXT("MirrorPickCenter", "Choose mirror center");
		return FText::Format(LOCTEXT("MirrorViewportStatus", "{0}\n{1}\n{2}\n{3}"), State, Mode, Space, Grid);
	}
	const FText Selection = Document->IsSymmetryPivotSelected() ? LOCTEXT("CenterSelectedStatus", "Symmetry center selected")
		: FText::Format(LOCTEXT("SelectionCountStatus", "{0} selected"), FText::AsNumber(Document->GetSelectedEntryIds().Num()));
	return FText::Format(LOCTEXT("DecorationViewportStatus", "{0}\n{1}\n{2}"), Selection, Space, Grid);
}

bool FDeepLevelCityBuildingDecorationEditor::CommitSymmetryPlacement()
{
	if (!IsPlacingSymmetry() || !SymmetryPlacementCenter.IsSet()) { return false; }
	const FVector Center = SymmetryPlacementCenter.GetValue();
	const FVector Normal = SymmetryPlacementNormal;
	CancelSymmetryPlacement();
	FText Error;
	if (Document->CreateSymmetryAt(Center, Normal, Error)) { return true; }
	OperationError = Error;
	RefreshIssues();
	return false;
}

void FDeepLevelCityBuildingDecorationEditor::CancelSymmetryPlacement()
{
	if (!IsPlacingSymmetry()) { return; }
	SymmetryPlacementAxis.Reset();
	SymmetryPlacementCenter.Reset();
	SymmetrySnappedAxis = EAxis::None;
	bSymmetryFreeAxis = false;
	Preview->ClearSymmetryPreview();
	Preview->GetClient()->Invalidate();
}

AActor* SDeepLevelCityBuildingDecorationViewport::GetSymmetryPreviewActor(const FGuid& Source) const
{
	const auto* Actor = SymmetryPreviewActors.Find(Source);
	return Actor ? Actor->Get() : nullptr;
}

bool SDeepLevelCityBuildingDecorationViewport::SetSymmetryPreview(TConstArrayView<FDeepLevelCityBuildingDecorationEntry> Copies)
{
	UMaterialInterface* WireMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/EditorMaterials/Dataflow/M_Dataflow_Color_Wireframe_TwoSided.M_Dataflow_Color_Wireframe_TwoSided"));
	if (!WireMaterial) { return false; }
	for (const auto& Copy : Copies)
	{
		AActor* Actor = GetSymmetryPreviewActor(Copy.EntryGuid);
		if (!Actor)
		{
			Actor = SpawnEntry(Copy);
			if (!Actor) { ClearSymmetryPreview(); return false; }
			SymmetryPreviewActors.Add(Copy.EntryGuid, Actor);
			Actor->SetActorEnableCollision(false);
			auto* Material = UMaterialInstanceDynamic::Create(WireMaterial, Actor);
			Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.1f, 0.8f, 1.0f));
			Material->SetScalarParameterValue(TEXT("Opacity"), 0.35f);
			TArray<UPrimitiveComponent*> Components;
			Actor->GetComponents(Components, true);
			for (auto* Component : Components)
			{
				Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Component->SetGenerateOverlapEvents(false);
				Component->SetCastShadow(false);
				Component->SetCanEverAffectNavigation(false);
				if (auto* Mesh = Cast<UMeshComponent>(Component))
				{
					for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot) { Mesh->SetMaterial(Slot, Material); }
				}
			}
			TArray<ULightComponent*> Lights;
			Actor->GetComponents(Lights, true);
			for (auto* Light : Lights) { Light->SetVisibility(false); }
			TArray<UDecalComponent*> Decals;
			Actor->GetComponents(Decals, true);
			for (auto* Decal : Decals) { Decal->SetVisibility(false); }
		}
		ApplyActorTransform(Actor, Copy.LocalTransform);
	}
	SymmetryPreviewEntries.Reset();
	SymmetryPreviewEntries.Append(Copies.GetData(), Copies.Num());
	return true;
}

void SDeepLevelCityBuildingDecorationViewport::ClearSymmetryPreview()
{
	for (const auto& Item : SymmetryPreviewActors)
	{
		if (Item.Value.IsValid()) { PreviewScene->GetWorld()->DestroyActor(Item.Value.Get()); }
	}
	SymmetryPreviewActors.Reset();
	SymmetryPreviewEntries.Reset();
}

void SDeepLevelCityBuildingDecorationViewport::DrawSymmetryPreview(FPrimitiveDrawInterface* PDI) const
{
	const auto Pinned = Editor.Pin();
	if (!Pinned || !Pinned->IsPlacingSymmetry()) { return; }
	const FVector Origin = Pinned->GetSelectionTransform().GetLocation();
	const FQuat Basis = Pinned->GetSymmetryPlacementBasis();
	const double Length = FMath::Max(100000.0, FVector::Distance(Client->GetViewLocation(), Origin) * 20.0);
	for (const auto Axis : {EAxis::X, EAxis::Y, EAxis::Z})
	{
		const FVector Direction = Basis.RotateVector(SymmetryAxis(Axis));
		const bool bActive = Pinned->GetSymmetrySnappedAxis() == Axis;
		PDI->DrawLine(Origin - Direction * Length, Origin + Direction * Length,
			SymmetryAxisColor(Axis), SDPG_Foreground, bActive ? 3.0f : 1.0f);
	}
	PDI->DrawPoint(Origin, FLinearColor::White, 8, SDPG_Foreground);
	if (!Pinned->HasSymmetryPreview()) { return; }
	const FLinearColor Color(0.1f, 0.8f, 1.0f);
	const FVector Center = Pinned->GetSymmetryPlacementCenter();
	const FVector Normal = Pinned->GetSymmetryPlacementNormal();
	DrawWireBox(PDI, FTransform(FRotationMatrix::MakeFromX(Normal).ToQuat(), Center).ToMatrixWithScale(),
		FBox(FVector(0, -100, -100), FVector(0, 100, 100)), Color, SDPG_Foreground);
	PDI->DrawLine(Center - Normal * 40, Center + Normal * 40, Color, SDPG_Foreground);
	PDI->DrawPoint(Center, FLinearColor::Yellow, 18, SDPG_Foreground);
	for (const auto& Copy : SymmetryPreviewEntries)
	{
		const auto* Source = Pinned->GetDecoration(Copy.EntryGuid);
		if (!Source) { continue; }
		PDI->DrawLine(Source->LocalTransform.GetLocation(), Copy.LocalTransform.GetLocation(), Color, SDPG_Foreground);
		const FVector Midpoint = (Source->LocalTransform.GetLocation() + Copy.LocalTransform.GetLocation()) * 0.5;
		PDI->DrawPoint(Midpoint, Color, 10, SDPG_Foreground);
		if (Copy.Output == EDeepLevelCityDecorationOutput::Decal)
		{
			DrawWireBox(PDI, Copy.LocalTransform.ToMatrixWithScale(), FBox(-Copy.DecalSize, Copy.DecalSize), Color, SDPG_Foreground);
		}
		else if (AActor* Actor = GetSymmetryPreviewActor(Copy.EntryGuid))
		{
			const FBox Bounds = Actor->GetComponentsBoundingBox(true, true);
			if (Bounds.IsValid) { DrawWireBox(PDI, Bounds, Color, SDPG_Foreground); }
		}
	}
}

void SDeepLevelCityBuildingDecorationViewport::DrawSymmetryAxisLabels(const FSceneView* View, FCanvas* Canvas) const
{
	const auto Pinned = Editor.Pin();
	if (!Pinned || !Pinned->IsPlacingSymmetry() || !View || !Canvas) { return; }
	const FVector Origin = Pinned->GetSelectionTransform().GetLocation();
	const FQuat Basis = Pinned->GetSymmetryPlacementBasis();
	const double Length = FMath::Max(500.0, FVector::Distance(Client->GetViewLocation(), Origin) * 0.5);
	for (const auto Axis : {EAxis::X, EAxis::Y, EAxis::Z})
	{
		FVector2D Pixel;
		if (View->WorldToPixel(Origin + Basis.RotateVector(SymmetryAxis(Axis)) * Length * 0.4, Pixel)
			&& View->UnscaledViewRect.Contains(FIntPoint(Pixel.X, Pixel.Y)))
		{
			Canvas->DrawShadowedString(Pixel.X + 6, Pixel.Y + 6, *SymmetryAxisText(Axis).ToString(), GEngine->GetSmallFont(), SymmetryAxisColor(Axis));
		}
	}
}

#undef LOCTEXT_NAMESPACE
