// Copyright <--\, Inc. All Rights Reserved.

#include "Building/DeepLevelBuildingPlacementClosure.h"
#include "Building/DeepLevelBuildingLayout.h"
#include "Building/DeepLevelBuildingVolFinalSolver.h"

#define LOCTEXT_NAMESPACE "DeepLevelBuildingPlacementClosure"

namespace
{
	const FDeepLevelBuildingPlacementDefinition* FindDefinition(
		const UDeepLevelBuildingPlacementCatalog& Catalog,
		const FDeepLevelBuildingLinePlacement& Placement)
	{
		return Catalog.Buildings.FindByPredicate([&Placement](const FDeepLevelBuildingPlacementDefinition& Entry)
		{
			return Entry.BuildingClass == Placement.BuildingClass;
		});
	}

	bool ConvertPlacement(
		const UDeepLevelBuildingPlacementCatalog& Catalog,
		const FDeepLevelBuildingLinePlacement& Source,
		const int32 SourceIndex,
		DeepLevelBuildingVolFinal::FPlacement& Out)
	{
		const FDeepLevelBuildingPlacementDefinition* Definition = FindDefinition(Catalog, Source);
		if (!Definition) { return false; }
		const FTransform Transform = FDeepLevelBuildingPlacementGeometry::BuildActorTransform(
			*Definition, Source.StreetFace, Source.PathSample.Location,
			Source.PathSample.Forward, Source.PathSample.Right);
		DeepLevelBuildingLayoutGeometry::FFootprint Corners;
		DeepLevelBuildingLayoutGeometry::MakeFootprintCorners(Definition->PlacementVolume, Transform, Corners);
		const FVector2D Center2D = (Corners[0] + Corners[1] + Corners[2] + Corners[3]) * 0.25;
		const FVector2D Forward2D = (Corners[1] - Corners[0]).GetSafeNormal();
		const FVector2D Right2D = (Corners[3] - Corners[0]).GetSafeNormal();
		Out.SourceIndex = SourceIndex;
		Out.Distance = Source.Distance;
		Out.CoverageStart = Source.CoverageStart;
		Out.CoverageEnd = Source.CoverageEnd;
		Out.PathSample.Location = Source.PathSample.Location;
		Out.PathSample.Forward = Source.PathSample.Forward;
		Out.PathSample.Right = Source.PathSample.Right;
		Out.Footprint.Center = FVector(Center2D, Definition->PlacementVolume.Extent.Z);
		Out.Footprint.Forward = FVector(Forward2D, 0.0);
		Out.Footprint.Right = FVector(Right2D, 0.0);
		Out.Footprint.HalfWidth = FVector2D::Distance(Corners[0], Corners[1]) * 0.5;
		Out.Footprint.HalfDepth = FVector2D::Distance(Corners[0], Corners[3]) * 0.5;
		Out.Footprint.MinZ = 0.0;
		Out.Footprint.MaxZ = Definition->PlacementVolume.Extent.Z * 2.0;
		Out.Center = Out.Footprint.Center;
		Out.Facade.Start = Source.PathSample.Location - Source.PathSample.Forward * Out.Footprint.HalfWidth;
		Out.Facade.End = Source.PathSample.Location + Source.PathSample.Forward * Out.Footprint.HalfWidth;
		return true;
	}
}

bool FDeepLevelBuildingPlacementClosure::ApplyFinal(
	const UDeepLevelBuildingPlacementCatalog& Catalog,
	const TArray<FVector2D>& BlockPolygon,
	const double BoundaryMargin,
	FDeepLevelBuildingLinePlan& InOutPlan,
	FDeepLevelBuildingFinalClosureStats& OutStats,
	FText& OutError)
{
	(void)BoundaryMargin;
	OutStats = {};
	OutError = FText::GetEmpty();
	TArray<DeepLevelBuildingVolFinal::FPlacement> Source;
	Source.Reserve(InOutPlan.Placements.Num());
	for (int32 Index = 0; Index < InOutPlan.Placements.Num(); ++Index)
	{
		DeepLevelBuildingVolFinal::FPlacement Placement;
		if (!ConvertPlacement(Catalog, InOutPlan.Placements[Index], Index, Placement))
		{
			OutError = LOCTEXT("MissingVolFinalDefinition", "Vol.Final could not resolve a placement definition.");
			return false;
		}
		Source.Add(MoveTemp(Placement));
	}
	const DeepLevelBuildingVolFinal::FResult Result = DeepLevelBuildingVolFinal::Solve(BlockPolygon, Source);
	if (Result.Placements.Num() != InOutPlan.Placements.Num())
	{
		OutError = LOCTEXT("InvalidVolFinalResult", "Vol.Final returned an invalid placement count.");
		return false;
	}
	for (const DeepLevelBuildingVolFinal::FPlacement& Placement : Result.Placements)
	{
		FDeepLevelBuildingLinePlacement& Target = InOutPlan.Placements[Placement.SourceIndex];
		Target.Distance = Placement.Distance;
		Target.CoverageStart = Placement.CoverageStart;
		Target.CoverageEnd = Placement.CoverageEnd;
		Target.PathSample.Location = Placement.PathSample.Location;
	}
	OutStats.PhaseCount = Result.Phases.Num();
	OutStats.MovedPlacementCount = Result.BuildingsMovedCount;
	OutStats.TotalShift = Result.TotalShiftCm;
	return true;
}

#undef LOCTEXT_NAMESPACE
