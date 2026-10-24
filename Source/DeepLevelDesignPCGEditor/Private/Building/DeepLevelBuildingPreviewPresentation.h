#pragma once

#include "Building/DeepLevelBuildingPCG.h"

class FCanvas;
class FEditorViewportClient;
class FPrimitiveDrawInterface;
class FSceneView;

namespace DeepLevelBuildingPreviewPresentation
{
FLinearColor ExposureColor(EDeepLevelStreetExposureRule Rule);
FText ExposureText(EDeepLevelStreetExposureRule Rule);
FVector ViewDirection(const FDeepLevelBuildingPlacementVolume& Volume, const FTransform& ActorTransform = FTransform::Identity);
void Focus(FEditorViewportClient& Client, const FBox& Bounds, const FDeepLevelBuildingPlacementVolume& Volume, const FTransform& ActorTransform = FTransform::Identity);
void DrawExposure(FPrimitiveDrawInterface* PDI, const FDeepLevelBuildingPlacementVolume& Volume);
void DrawLabels(const FSceneView* View, FCanvas* Canvas, const FDeepLevelBuildingPlacementVolume& Volume);
}
