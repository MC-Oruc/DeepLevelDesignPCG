#include "ZoneGraphDelegates.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Road/Traffic/DeepLevelRoadZoneGraph.h"
#include "Road/DeepLevelRoadPCG.h"
#include "ZoneGraphSettings.h"
#include "ZoneGraphSubsystem.h"
#include "ZoneGraphBuilder.h"
#include "ZoneGraphAStar.h"
#include "ZoneShapeComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelRoadZoneGraphTest,"DeepLevelDesignPCG.Editor.RoadNetwork.ZoneGraph",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelRoadZoneGraphTest::RunTest(const FString&)
{
    auto* Settings=GetMutableDefault<UZoneGraphSettings>();
    auto* Property=FindFProperty<FArrayProperty>(UZoneGraphSettings::StaticClass(),TEXT("LaneProfiles"));
    auto& Profiles=*Property->ContainerPtrToValuePtr<TArray<FZoneLaneProfile>>(Settings);
    const auto Original=Profiles;
    UWorld* World=UWorld::CreateWorld(EWorldType::Editor,false);
    ON_SCOPE_EXIT { World->DestroyWorld(false); Profiles=Original; };
    FZoneLaneProfile TwoWay; TwoWay.Name=TEXT("RoadTestTwoWay");
    TwoWay.Lanes.SetNum(2); TwoWay.Lanes[0].Width=250; TwoWay.Lanes[1].Width=250;
    TwoWay.Lanes[1].Direction=EZoneLaneDirection::Backward;
    FZoneLaneProfile Wide=TwoWay; Wide.ID=FGuid::NewGuid(); Wide.Name=TEXT("RoadTestWide");
    Wide.Lanes.SetNum(4); for (int32 I=0;I<4;++I) { Wide.Lanes[I].Width=250; Wide.Lanes[I].Direction=I<2 ? EZoneLaneDirection::Forward : EZoneLaneDirection::Backward; }
    Profiles.Add(TwoWay); Profiles.Add(Wide);
    auto* Network=World->SpawnActor<ADeepLevelRoadNetworkActor>();
    Network->TwoWayLaneProfile=FZoneLaneProfileRef(TwoWay); Network->WideLaneProfile=FZoneLaneProfileRef(Wide);
    auto* Graph=World->GetSubsystem<UZoneGraphSubsystem>();
    if (!TestNotNull(TEXT("Native ZoneGraph subsystem"),Graph)) return false;
    FDeepLevelCityGrid Grid;
    FDeepLevelRoadNetworkPlan Plan;
    auto AddRow=[&](int32 Y)
    {
        for (int32 X=0;X<=6;++X)
        {
            auto& Cell=Plan.Placements.AddDefaulted_GetRef(); Cell.GridCell={X,Y};
            Cell.ConnectionMask=(X>0?4:0)|(X<6?1:0);
        }
    };
    auto DirectionAt=[&](FVector Position)
    {
        FZoneGraphLaneLocation Lane; float Distance=0;
        const bool Found=Graph->FindNearestLane(FBox(Position-FVector(20),Position+FVector(20)),{},Lane,Distance);
        TestTrue(TEXT("Baked vehicle lane exists at sample"),Found);
        return Lane.Direction.X;
    };
    AddRow(0);
    FText Error;
    if (!TestTrue(TEXT("Export single road"),DeepLevelRoadZoneGraph::BuildShapes(*Network,Grid,Plan,Error))) return false;
    UE::ZoneGraphDelegates::OnZoneGraphRequestRebuild.Broadcast();
    TestTrue(TEXT("Single tile right lane travels +X"),DirectionAt(FVector(1000,125,0))>.9);
    TestTrue(TEXT("Single tile opposite lane travels -X"),DirectionAt(FVector(1000,-125,0))<-.9);
    AddRow(1);
    if (!TestTrue(TEXT("Export double road"),DeepLevelRoadZoneGraph::BuildShapes(*Network,Grid,Plan,Error))) return false;
    UE::ZoneGraphDelegates::OnZoneGraphRequestRebuild.Broadcast();
    TestTrue(TEXT("Lower tile lane one travels -X"),DirectionAt(FVector(1000,-125,0))<-.9);
    TestTrue(TEXT("Lower tile lane two travels -X"),DirectionAt(FVector(1000,125,0))<-.9);
    TestTrue(TEXT("Upper tile lane one travels +X"),DirectionAt(FVector(1000,375,0))>.9);
    TestTrue(TEXT("Upper tile lane two travels +X"),DirectionAt(FVector(1000,625,0))>.9);
    TestEqual(TEXT("Rebuild replaces owned shapes"),Graph->GetBuilder().GetRegisteredZoneShapeComponents().Num(),7);
    FZoneGraphLaneLocation Lane; float Distance=0;
    Graph->FindNearestLane(FBox(FVector(980,360,-10),FVector(1020,390,10)),{},Lane,Distance);
    TArray<FZoneGraphLinkedLane> Links;
    Graph->GetLinkedLanes(Lane.LaneHandle,EZoneLaneLinkType::Outgoing,EZoneLaneLinkFlags::None,EZoneLaneLinkFlags::None,Links);
    TestFalse(TEXT("Native bake connects neighbouring road cells"),Links.IsEmpty());
    // A 2+2 road narrowing into a 1+1 road must remain connected in both directions.
    for (auto& Cell : Plan.Placements) if (Cell.GridCell==FIntPoint(6,1)) Cell.ConnectionMask|=1;
    for (int32 X=7;X<=9;++X)
    {
        auto& Cell=Plan.Placements.AddDefaulted_GetRef(); Cell.GridCell={X,1}; Cell.ConnectionMask=4|(X<9?1:0);
    }
    if (!TestTrue(TEXT("Export narrowing road"),DeepLevelRoadZoneGraph::BuildShapes(*Network,Grid,Plan,Error))) return false;
    UE::ZoneGraphDelegates::OnZoneGraphRequestRebuild.Broadcast();
    FZoneGraphLaneLocation Start,Goal;
    Graph->FindNearestLane(FBox(FVector(3980,360,-10),FVector(4020,390,10)),{},Start,Distance);
    Graph->FindNearestLane(FBox(FVector(980,110,-10),FVector(1020,140,10)),{},Goal,Distance);
    const auto* Storage=Graph->GetZoneGraphStorage(Start.LaneHandle.DataHandle);
    if (!TestNotNull(TEXT("Baked narrowing data"),Storage)) return false;
    FZoneGraphAStarWrapper Wrapper(*Storage); FZoneGraphAStar Search(Wrapper);
    FZoneGraphPathFilter PathFilter(*Storage,Start,Goal); TArray<int32> Path;
    TestEqual(TEXT("Native A* traverses the narrowing in the return direction"),
        Search.FindPath(FZoneGraphAStarNode(Start.LaneHandle.Index,Start.Position),FZoneGraphAStarNode(Goal.LaneHandle.Index,Goal.Position),PathFilter,Path),SearchSuccess);
    Network->bGenerateTrafficLanes=false;
    TestTrue(TEXT("Disable removes owned generated shapes"),DeepLevelRoadZoneGraph::Rebuild(*Network,Error));
    return true;
}
#endif
