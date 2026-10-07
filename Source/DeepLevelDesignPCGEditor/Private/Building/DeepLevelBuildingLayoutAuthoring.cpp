// Copyright <--\, Inc. All Rights Reserved.
#include "Building/DeepLevelBuildingLayoutAuthoring.h"

namespace
{
	TArray<FVector> ReadPoints(const USplineComponent& Spline)
	{
		TArray<FVector> Points;
		for (int32 Index = 0; Index < Spline.GetNumberOfSplinePoints(); ++Index) { Points.Add(Spline.GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World)); }
		return Points;
	}
	void SetPoints(USplineComponent& Spline, const TArray<FVector>& Points, bool bClosed)
	{
		Spline.Modify(); Spline.ClearSplinePoints(false);
		for (const auto& P : Points) { Spline.AddSplinePoint(P, ESplineCoordinateSpace::World, false); }
		for (int32 Index = 0; Index < Points.Num(); ++Index) { Spline.SetSplinePointType(Index, ESplinePointType::Linear, false); }
		Spline.SetClosedLoop(bClosed, false); Spline.UpdateSpline(); Spline.bSplineHasBeenEdited = true;
	}
}
DeepLevelBuildingLayoutAuthoring::FState DeepLevelBuildingLayoutAuthoring::Read(AActor& Actor)
{
	FState State;
	if (auto* Line = Cast<ADeepLevelPCGBuildingLineActor>(&Actor))
	{
		State.City = Line->CityLayout; State.Catalog = Line->Catalog; State.Seed = Line->RandomSeed;
		State.Variety = Line->VarietyStrength; State.CornerPreference = Line->CornerPreference;
		State.CornerMask = Line->BuildingLine->CornerPlacementMask; State.LinePoints = ReadPoints(*Line->BuildingLine); State.bClosed = Line->BuildingLine->IsClosedLoop();
	}
	if (auto* Side = Cast<ADeepLevelPCGRoadsideBuildingActor>(&Actor))
	{
		State.City = Side->CityLayout; State.Catalog = Side->Catalog; State.Seed = Side->RandomSeed;
		State.Variety = Side->VarietyStrength; State.CornerPreference = Side->CornerPreference; State.CornerMask = Side->CornerPlacementMask;
		State.Setback = Side->FrontageSetback; State.DepthTolerance = Side->FrontageDepthTolerance; State.BoundaryMargin = Side->BlockBoundaryMargin;
		TInlineComponentArray<UDeepLevelRoadsideFrontageSplineComponent*> Frontages(Side);
		for (auto* Spline : Frontages)
		{
			auto& F = State.Frontages.Emplace_GetRef(); F.Id = Spline->FrontageId; F.ReplacedId = Spline->ReplacedFrontageId;
			F.Source = Spline; F.Kind = Spline->Kind; F.Points = ReadPoints(*Spline); F.bClosed = Spline->IsClosedLoop(); F.bExcluded = Spline->bExcluded;
		}
	}
	return State;
}
void DeepLevelBuildingLayoutAuthoring::Apply(AActor& Actor, FState& State, bool bRebuildFrontages)
{
	Actor.Modify();
	if (auto* Line = Cast<ADeepLevelPCGBuildingLineActor>(&Actor))
	{
		Line->CityLayout = State.City.Get(); Line->Catalog = State.Catalog; Line->RandomSeed = State.Seed;
		Line->VarietyStrength = State.Variety; Line->CornerPreference = State.CornerPreference;
		Line->BuildingLine->Modify();
		if (State.bLineChanged) { SetPoints(*Line->BuildingLine, State.LinePoints, State.bClosed); }
		Line->BuildingLine->CornerPlacementMask = State.CornerMask;
		Line->PostEditChange();
	}
	if (auto* Side = Cast<ADeepLevelPCGRoadsideBuildingActor>(&Actor))
	{
		Side->CityLayout = State.City.Get(); Side->Catalog = State.Catalog; Side->RandomSeed = State.Seed;
		Side->VarietyStrength = State.Variety; Side->CornerPreference = State.CornerPreference; Side->CornerPlacementMask = State.CornerMask;
		Side->FrontageSetback = State.Setback; Side->FrontageDepthTolerance = State.DepthTolerance; Side->BlockBoundaryMargin = State.BoundaryMargin;
		TInlineComponentArray<UDeepLevelRoadsideFrontageSplineComponent*> Existing(Side);
		for (auto* Spline : Existing)
		{
			if (Spline->Kind != EDeepLevelRoadsideFrontageKind::Automatic && !State.Frontages.ContainsByPredicate([Spline](const FFrontage& F) { return F.Source.Get() == Spline; }))
			{ Side->RemoveFrontageOverride(*Spline, false); }
		}
		for (auto& F : State.Frontages)
		{
			if (F.Kind == EDeepLevelRoadsideFrontageKind::Automatic)
			{
				if (F.bExcluded) { Side->ExcludedFrontageIds.AddUnique(F.Id); } else { Side->ExcludedFrontageIds.Remove(F.Id); }
				continue;
			}
			if (auto* Spline = F.Source.Get()) { if (F.bGeometryChanged) { SetPoints(*Spline, F.Points, F.bClosed); } }
			else
			{
				auto* Created = Side->CreateManualFrontage(F.Points, F.bClosed, F.ReplacedId, false);
				Created->FrontageId = F.Id; F.Source = Created;
			}
		}
		Side->PostEditChange();
		if (bRebuildFrontages) { Side->GenerateFrontageSplines(); }
	}
	Actor.MarkPackageDirty();
}
