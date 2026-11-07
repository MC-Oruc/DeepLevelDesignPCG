#include "Road/Traffic/DeepLevelRoadZoneGraph.h"
#include "Road/DeepLevelRoadPCG.h"
#include "ZoneGraphDelegates.h"
#include "ZoneGraphSettings.h"
#include "ZoneGraphSubsystem.h"
#include "ZoneShapeComponent.h"
#include "Engine/World.h"

namespace DeepLevelRoadZoneGraph
{
namespace
{
    const FName OutputTag(TEXT("DeepLevel.Road.Traffic"));
    struct FDirection { FIntPoint Cell; int32 Mask; int32 Opposite; };
    const FDirection Directions[] = {{{1,0},1,4},{{0,1},2,8},{{-1,0},4,1},{{0,-1},8,2}};
    struct FTopology
    {
        TMap<FIntPoint,int32> Cells;
        bool HasEdge(FIntPoint Cell,const FDirection& D) const
        {
            const auto* A=Cells.Find(Cell); const auto* B=Cells.Find(Cell+D.Cell);
            return A && B && (*A&D.Mask) && (*B&D.Opposite);
        }
        int32 Mask(FIntPoint Cell) const
        {
            int32 Result=0; for (const auto& D : Directions) if (HasEdge(Cell,D)) Result|=D.Mask; return Result;
        }
        TArray<FIntPoint> Section(FIntPoint Cell,const FDirection& D) const
        {
            TArray<FIntPoint> Result;
            if (!HasEdge(Cell,D)) return Result;
            Result.Add(Cell);
            const FIntPoint Side(-D.Cell.Y,D.Cell.X);
            for (int32 Sign : {-1,1})
                for (FIntPoint Next=Cell+Side*Sign;HasEdge(Next,D);Next+=Side*Sign) Result.Add(Next);
            return Result;
        }
    };
    using FGroup=TSet<FIntPoint>;
    struct FPort { FVector Position; FVector Outward; int32 Width; };

    void ClearShapes(ADeepLevelRoadNetworkActor& Network)
    {
        TInlineComponentArray<UZoneShapeComponent*> Shapes(&Network);
        for (auto* Shape : Shapes)
            if (Shape->ComponentHasTag(OutputTag)) { Network.RemoveInstanceComponent(Shape); Shape->DestroyComponent(); }
    }
    bool ValidateProfiles(const ADeepLevelRoadNetworkActor& Network,double Width,FText& Error)
    {
        const auto* Settings=GetDefault<UZoneGraphSettings>();
        const auto* Narrow=Settings->GetLaneProfileByRef(Network.TwoWayLaneProfile);
        const auto* Wide=Settings->GetLaneProfileByRef(Network.WideLaneProfile);
        if (!Narrow || !Wide || Narrow->Lanes.Num()!=2 || Wide->Lanes.Num()!=4
            || !FMath::IsNearlyEqual(double(Narrow->GetLanesTotalWidth()),Width,.1)
            || !FMath::IsNearlyEqual(double(Wide->GetLanesTotalWidth()),Width*2,.1))
        {
            Error=NSLOCTEXT("DeepLevelRoadTraffic","Profiles","Traffic requires tile-width 1+1 and double-tile-width 2+2 ZoneGraph lane profiles.");
            return false;
        }
        for (int32 Index=0;Index<6;++Index)
        {
            const auto& Lane=Index<2 ? Narrow->Lanes[Index] : Wide->Lanes[Index-2];
            const auto Expected=(Index==1 || Index>=4) ? EZoneLaneDirection::Backward : EZoneLaneDirection::Forward;
            if (Lane.Direction!=Expected || !FMath::IsNearlyEqual(double(Lane.Width),Width*.5,.1))
            {
                Error=NSLOCTEXT("DeepLevelRoadTraffic","Directions","Profiles must use equal-width lanes ordered Forward/Backward and Forward/Forward/Backward/Backward.");
                return false;
            }
        }
        return true;
    }
    TArray<FGroup> GroupCells(const FTopology& Road)
    {
        TSet<FIntPoint> Complex;
        for (const auto& Cell : Road.Cells)
        {
            const int32 Mask=Road.Mask(Cell.Key);
            if (Mask!=5 && Mask!=10) { Complex.Add(Cell.Key); continue; }
            const int32 Axis=Mask==5 ? 0 : 1;
            if (Road.Section(Cell.Key,Directions[Axis]).Num()!=Road.Section(Cell.Key,Directions[Axis+2]).Num()) Complex.Add(Cell.Key);
        }
        // A junction or taper owns its whole cross-section, including the adjacent straight tile.
        const auto Seeds=Complex.Array();
        for (auto Seed : Seeds)
            for (const auto& D : Directions)
                for (auto Cell : Road.Section(Seed,D)) Complex.Add(Cell);
        TArray<FIntPoint> Ordered; Road.Cells.GetKeys(Ordered);
        Ordered.Sort([](FIntPoint A,FIntPoint B){return A.X!=B.X ? A.X<B.X : A.Y<B.Y;});
        TSet<FIntPoint> Used;
        TArray<FGroup> Groups;
        for (auto Seed : Ordered)
        {
            if (Used.Contains(Seed)) continue;
            FGroup& Group=Groups.AddDefaulted_GetRef(); Group.Add(Seed); Used.Add(Seed);
            if (Complex.Contains(Seed))
            {
                TArray<FIntPoint> Queue{Seed};
                for (int32 Index=0;Index<Queue.Num();++Index)
                    for (const auto& D : Directions)
                    {
                        const FIntPoint Next=Queue[Index]+D.Cell;
                        if (Complex.Contains(Next) && !Used.Contains(Next)) { Used.Add(Next); Group.Add(Next); Queue.Add(Next); }
                    }
            }
            else
            {
                const auto& D=Directions[Road.Mask(Seed)==5 ? 0 : 1];
                for (auto Cell : Road.Section(Seed,D))
                    if (!Complex.Contains(Cell) && !Used.Contains(Cell)) { Used.Add(Cell); Group.Add(Cell); }
            }
        }
        return Groups;
    }
    bool GetPorts(const FTopology& Road,const FGroup& Group,const FDeepLevelCityGrid& Grid,TArray<FPort>& Ports,FText& Error)
    {
        for (const auto& D : Directions)
        {
            TArray<FIntPoint> Edges;
            for (auto Cell : Group) if (Road.HasEdge(Cell,D) && !Group.Contains(Cell+D.Cell)) Edges.Add(Cell);
            const FIntPoint Side(-D.Cell.Y,D.Cell.X);
            auto Dot=[](FIntPoint A,FIntPoint B){return A.X*B.X+A.Y*B.Y;};
            Edges.Sort([&](FIntPoint A,FIntPoint B){return Dot(A,D.Cell)!=Dot(B,D.Cell) ? Dot(A,D.Cell)<Dot(B,D.Cell) : Dot(A,Side)<Dot(B,Side);});
            for (int32 Begin=0;Begin<Edges.Num();)
            {
                int32 End=Begin+1;
                while (End<Edges.Num() && Edges[End]==Edges[End-1]+Side) ++End;
                const int32 Width=End-Begin;
                if (Width>2)
                {
                    Error=NSLOCTEXT("DeepLevelRoadTraffic","Width","This traffic export supports one-tile and two-tile road cross-sections."); return false;
                }
                const FVector Outward(D.Cell.X,D.Cell.Y,0);
                Ports.Add({(Grid.CellToWorld(Edges[Begin])+Grid.CellToWorld(Edges[End-1]))*.5+Outward*Grid.TileSize*.5,Outward,Width});
                Begin=End;
            }
        }
        if (Ports.IsEmpty())
        {
            Error=NSLOCTEXT("DeepLevelRoadTraffic","IsolatedJunction","An isolated junction cluster has no connected road approach."); return false;
        }
        FVector Center=FVector::ZeroVector; for (const auto& P : Ports) Center+=P.Position; Center/=Ports.Num();
        Ports.Sort([Center](const FPort& A,const FPort& B){return FMath::Atan2(A.Position.Y-Center.Y,A.Position.X-Center.X)<FMath::Atan2(B.Position.Y-Center.Y,B.Position.X-Center.X);});
        return true;
    }
    void CreateShape(ADeepLevelRoadNetworkActor& Network,const FDeepLevelCityGrid& Grid,TConstArrayView<FPort> Ports)
    {
        auto* Shape=NewObject<UZoneShapeComponent>(&Network,MakeUniqueObjectName(&Network,UZoneShapeComponent::StaticClass(),TEXT("Traffic")),RF_Transactional);
        Shape->ComponentTags.Add(OutputTag); Shape->bEditableWhenInherited=false;
        Shape->SetCommonLaneProfile(Network.TwoWayLaneProfile);
        Shape->SetShapeType(FZoneShapeType::Polygon);
        Shape->GetMutablePoints().Reset();
        Shape->SetupAttachment(Network.GetRootComponent());
        Shape->SetUsingAbsoluteLocation(true); Shape->SetUsingAbsoluteRotation(true); Shape->SetUsingAbsoluteScale(true);
        Shape->SetRelativeTransform(FTransform::Identity);
        if (Ports.Num()==1)
        {
            Shape->SetShapeType(FZoneShapeType::Spline);
            Shape->SetCommonLaneProfile(Ports[0].Width==1 ? Network.TwoWayLaneProfile : Network.WideLaneProfile);
            Shape->GetMutablePoints().Emplace(Ports[0].Position-Ports[0].Outward*Grid.TileSize);
            Shape->GetMutablePoints().Emplace(Ports[0].Position);
        }
        else for (const auto& Port : Ports)
        {
            auto& Point=Shape->GetMutablePoints().Emplace_GetRef(Port.Position);
            Point.Type=FZoneShapePointType::LaneProfile;
            Point.SetRotationFromForwardAndUp(-Port.Outward,FVector::UpVector);
            Point.LaneProfile=static_cast<uint8>(Shape->AddUniquePerPointLaneProfile(Port.Width==1 ? Network.TwoWayLaneProfile : Network.WideLaneProfile));
        }
        Network.AddInstanceComponent(Shape); Shape->RegisterComponent(); Shape->UpdateShape();
    }
}

bool BuildShapes(ADeepLevelRoadNetworkActor& Network,const FDeepLevelCityGrid& Grid,const FDeepLevelRoadNetworkPlan& Plan,FText& Error)
{
    if (!ValidateProfiles(Network,Grid.TileSize,Error)) return false;
    FTopology Road;
    for (const auto& Cell : Plan.Placements) if (Cell.Kind==EDeepLevelRoadTileKind::Road) Road.Cells.Add(Cell.GridCell,Cell.ConnectionMask);
    TArray<TArray<FPort>> Shapes;
    for (const auto& Group : GroupCells(Road))
    {
        auto& Ports=Shapes.AddDefaulted_GetRef();
        if (!GetPorts(Road,Group,Grid,Ports,Error)) return false;
    }
    Network.Modify(); ClearShapes(Network);
    for (const auto& Ports : Shapes) CreateShape(Network,Grid,Ports);
    TInlineComponentArray<UZoneShapeComponent*> Components(&Network);
    for (auto* Shape : Components) if (Shape->ComponentHasTag(OutputTag)) Shape->UpdateMatingConnectedShapes();
    Network.MarkPackageDirty(); return true;
}
bool Rebuild(ADeepLevelRoadNetworkActor& Network,FText& Error)
{
    UWorld* World=Network.GetWorld();
    if (!World || World->IsGameWorld()) return true;
    auto* Graph=World->GetSubsystem<UZoneGraphSubsystem>();
    if (!Graph) { Error=NSLOCTEXT("DeepLevelRoadTraffic","NoSubsystem","Enable ZoneGraph before generating road traffic lanes."); return false; }
    if (!Network.bGenerateTrafficLanes)
    {
        TInlineComponentArray<UZoneShapeComponent*> Existing(&Network);
        if (!Existing.ContainsByPredicate([](const auto* Shape){return Shape->ComponentHasTag(OutputTag);})) return true;
        Network.Modify(); ClearShapes(Network); UE::ZoneGraphDelegates::OnZoneGraphRequestRebuild.Broadcast(); return true;
    }
    FDeepLevelCityGrid Grid; FDeepLevelRoadNetworkPlan Plan;
    if (!Network.ResolveCityGrid(Grid,Error) || !Network.BuildRoadPlan(Plan,Error) || !BuildShapes(Network,Grid,Plan,Error)) return false;
    UE::ZoneGraphDelegates::OnZoneGraphRequestRebuild.Broadcast(); return true;
}
}
