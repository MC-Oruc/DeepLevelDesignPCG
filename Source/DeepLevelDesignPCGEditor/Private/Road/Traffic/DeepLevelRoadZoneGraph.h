#pragma once
#include "CoreMinimal.h"
class ADeepLevelRoadNetworkActor;
struct FDeepLevelCityGrid;
struct FDeepLevelRoadNetworkPlan;
namespace DeepLevelRoadZoneGraph
{
    bool Rebuild(ADeepLevelRoadNetworkActor& Network, FText& Error);
    bool BuildShapes(ADeepLevelRoadNetworkActor& Network, const FDeepLevelCityGrid& Grid,
        const FDeepLevelRoadNetworkPlan& Plan, FText& Error);
}
