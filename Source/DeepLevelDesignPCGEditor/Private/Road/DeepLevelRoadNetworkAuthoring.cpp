// Copyright <--\, Inc. All Rights Reserved.
#include "Road/DeepLevelRoadNetworkAuthoring.h"

DeepLevelRoadNetworkAuthoring::FState DeepLevelRoadNetworkAuthoring::Read(ADeepLevelRoadNetworkActor& Actor)
{
	FState State; State.City = Actor.CityLayout; State.Catalog = Actor.Catalog;
	State.CollisionProfile = Actor.RoadMeshCollisionProfile.Name; State.Overrides = Actor.CellOverrides;
	TArray<UDeepLevelRoadSplineComponent*> Splines; Actor.GetRoadSplineComponents(Splines);
	for (auto* Spline : Splines)
	{
		auto& Branch = State.Branches.Emplace_GetRef(); Branch.Id = Spline->GetPathName(); Branch.Source = Spline;
		for (int32 Index = 0; Index < Spline->GetNumberOfSplinePoints(); ++Index)
		{ Branch.Points.Add(Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World)); }
	}
	return State;
}
void DeepLevelRoadNetworkAuthoring::Apply(ADeepLevelRoadNetworkActor& Actor, FState& State)
{
	Actor.Modify(); Actor.CityLayout = State.City.Get(); Actor.Catalog = State.Catalog;
	if (State.City.IsValid()) { Actor.SetActorLocation(State.City->GetActorLocation()); }
	Actor.RoadMeshCollisionProfile.Name = State.CollisionProfile; Actor.CellOverrides = State.Overrides;
	TArray<UDeepLevelRoadSplineComponent*> Existing; Actor.GetRoadSplineComponents(Existing);
	for (auto* Spline : Existing)
	{
		if (State.Branches.ContainsByPredicate([Spline](const FBranch& Branch) { return Branch.Source.Get() == Spline; })) { continue; }
		Spline->Modify(); Actor.RemoveInstanceComponent(Spline); Spline->DestroyComponent();
	}
	for (auto& Branch : State.Branches)
	{
		auto* Spline = Branch.Source.Get();
		if (!Spline) { Spline = Actor.CreateRoadBranch(); Branch.Source = Spline; Branch.bGeometryChanged = true; }
		if (Branch.bGeometryChanged) { Spline->Modify(); Spline->SetRoadPathFromWorldPoints(Branch.Points); }
		Branch.Id = Spline->GetPathName();
	}
	Actor.MarkPackageDirty();
	// The owner performs registration, snapshot invalidation and normal editor PCG notification once.
	Actor.PostEditChange();
}
