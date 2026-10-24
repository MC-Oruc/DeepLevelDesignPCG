#include "Building/DeepLevelBuildingPreviewPresentation.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "EditorViewportClient.h"
#include "Engine/Engine.h"
#include "SceneManagement.h"
#include "SceneView.h"

namespace DeepLevelBuildingPreviewPresentation
{
namespace
{
constexpr EDeepLevelBuildingVolumeFace Faces[] = {EDeepLevelBuildingVolumeFace::PositiveX, EDeepLevelBuildingVolumeFace::NegativeX, EDeepLevelBuildingVolumeFace::PositiveY, EDeepLevelBuildingVolumeFace::NegativeY};
FVector Normal(EDeepLevelBuildingVolumeFace Face)
{
	switch (Face)
	{
	case EDeepLevelBuildingVolumeFace::PositiveX: return FVector::ForwardVector;
	case EDeepLevelBuildingVolumeFace::NegativeX: return -FVector::ForwardVector;
	case EDeepLevelBuildingVolumeFace::PositiveY: return FVector::RightVector;
	default: return -FVector::RightVector;
	}
}
int32 Priority(EDeepLevelStreetExposureRule Rule)
{
	switch (Rule)
	{
	case EDeepLevelStreetExposureRule::Required: return 3;
	case EDeepLevelStreetExposureRule::Preferred: return 2;
	case EDeepLevelStreetExposureRule::Neutral: return 1;
	default: return 0;
	}
}
}

FLinearColor ExposureColor(EDeepLevelStreetExposureRule Rule)
{
	switch (Rule)
	{
	case EDeepLevelStreetExposureRule::Required: return FLinearColor(0.1f, 1.0f, 0.2f);
	case EDeepLevelStreetExposureRule::Preferred: return FLinearColor(0.1f, 0.5f, 1.0f);
	case EDeepLevelStreetExposureRule::Forbidden: return FLinearColor(1.0f, 0.1f, 0.1f);
	default: return FLinearColor(0.65f, 0.65f, 0.65f);
	}
}

FText ExposureText(EDeepLevelStreetExposureRule Rule)
{
	switch (Rule)
	{
	case EDeepLevelStreetExposureRule::Required: return NSLOCTEXT("DeepLevelBuildingPreview", "Required", "REQUIRED");
	case EDeepLevelStreetExposureRule::Preferred: return NSLOCTEXT("DeepLevelBuildingPreview", "Preferred", "PREFERRED");
	case EDeepLevelStreetExposureRule::Forbidden: return NSLOCTEXT("DeepLevelBuildingPreview", "Forbidden", "FORBIDDEN");
	default: return NSLOCTEXT("DeepLevelBuildingPreview", "Neutral", "NEUTRAL");
	}
}

FVector ViewDirection(const FDeepLevelBuildingPlacementVolume& Volume, const FTransform& ActorTransform)
{
	int32 BestPriority = -1;
	TArray<FVector, TInlineAllocator<4>> BestNormals;
	for (const auto Face : Faces)
	{
		const int32 Rank = Priority(FDeepLevelBuildingPlacementGeometry::GetExposureRule(Volume.Exposure, Face));
		if (Rank > BestPriority) { BestNormals.Reset(); BestPriority = Rank; }
		if (Rank == BestPriority) { BestNormals.Add(Normal(Face)); }
	}
	FVector Direction = FVector::ZeroVector;
	for (const FVector& N : BestNormals) { Direction += N; }
	if (Direction.IsNearlyZero())
	{
		Direction = BestNormals[0];
		for (int32 Index = 1; Index < BestNormals.Num(); ++Index)
		{
			if (FMath::Abs(FVector::DotProduct(Direction, BestNormals[Index])) < 0.1) { Direction += BestNormals[Index]; break; }
		}
	}
	return ActorTransform.TransformVectorNoScale(Volume.Rotation.RotateVector(Direction.GetSafeNormal())).GetSafeNormal();
}

void Focus(FEditorViewportClient& Client, const FBox& Bounds, const FDeepLevelBuildingPlacementVolume& Volume, const FTransform& ActorTransform)
{
	Client.SetViewRotation((-ViewDirection(Volume, ActorTransform) - FVector::UpVector * 0.3).Rotation());
	Client.FocusViewportOnBox(Bounds, true);
	Client.Invalidate();
}

void DrawExposure(FPrimitiveDrawInterface* PDI, const FDeepLevelBuildingPlacementVolume& Volume)
{
	if (!PDI) { return; }
	const FTransform Transform(Volume.Rotation, Volume.Center);
	for (const auto Face : Faces)
	{
		const FVector N = Normal(Face);
		const FVector Center = N * Volume.Extent;
		const FVector Side = FMath::Abs(N.X) > 0.5 ? FVector(0, Volume.Extent.Y, 0) : FVector(Volume.Extent.X, 0, 0);
		const FVector Up(0, 0, Volume.Extent.Z);
		FVector Points[] = {Center - Side - Up, Center + Side - Up, Center + Side + Up, Center - Side + Up};
		for (FVector& Point : Points) { Point = Transform.TransformPosition(Point); }
		const FLinearColor Color = ExposureColor(FDeepLevelBuildingPlacementGeometry::GetExposureRule(Volume.Exposure, Face));
		for (int32 Index = 0; Index < 4; ++Index) { PDI->DrawLine(Points[Index], Points[(Index + 1) % 4], Color, SDPG_Foreground, 2.0f); }
	}
}

void DrawLabels(const FSceneView* View, FCanvas* Canvas, const FDeepLevelBuildingPlacementVolume& Volume)
{
	if (!View || !Canvas || !GEngine) { return; }
	const FTransform Transform(Volume.Rotation, Volume.Center);
	for (const auto Face : Faces)
	{
		const FVector N = Normal(Face);
		const FVector Center = Transform.TransformPosition(N * Volume.Extent);
		if (FVector::DotProduct(Transform.TransformVectorNoScale(N), View->ViewMatrices.GetViewOrigin() - Center) <= 0) { continue; }
		FVector2D Position;
		if (!View->ScreenToPixel(View->WorldToScreen(Center), Position)) { continue; }
		const auto Rule = FDeepLevelBuildingPlacementGeometry::GetExposureRule(Volume.Exposure, Face);
		FCanvasTextItem Item(Position, ExposureText(Rule), GEngine->GetSmallFont(), ExposureColor(Rule));
		Item.bOutlined = true; Item.OutlineColor = FLinearColor::Black; Item.bCentreX = true; Item.bCentreY = true;
		Item.Scale = FVector2D(1.25f); Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}
}
}
