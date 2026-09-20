// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Building/DeepLevelBuildingPlacementClosure.h"

#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingFinalClosureTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.FinalClosure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingFinalClosureTest::RunTest(const FString& Parameters)
{
	UDeepLevelBuildingPlacementCatalog* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>();
	FDeepLevelBuildingPlacementDefinition& Definition = Catalog->Buildings.AddDefaulted_GetRef();
	Definition.BuildingClass = AActor::StaticClass();
	Definition.PlacementVolume.Extent = FVector(500.0, 200.0, 100.0);
	Definition.PlacementVolume.Exposure.NegativeY = EDeepLevelStreetExposureRule::Required;

	FDeepLevelBuildingLinePlan Plan;
	for (const double X : {500.0, 2500.0})
	{
		FDeepLevelBuildingLinePlacement& Placement = Plan.Placements.AddDefaulted_GetRef();
		Placement.BuildingClass = AActor::StaticClass();
		Placement.StreetFace = EDeepLevelBuildingVolumeFace::NegativeY;
		Placement.Distance = X;
		Placement.CoverageStart = X - 500.0;
		Placement.CoverageEnd = X + 500.0;
		Placement.PathSample.Location = FVector(X, 0.0, 0.0);
		Placement.PathSample.Forward = FVector::ForwardVector;
		Placement.PathSample.Right = FVector::RightVector;
	}

	const TArray<FVector2D> Polygon = {
		FVector2D(0.0, 0.0), FVector2D(5000.0, 0.0),
		FVector2D(5000.0, 5000.0), FVector2D(0.0, 5000.0)};
	FDeepLevelBuildingFinalClosureStats Stats;
	FText Error;
	TestTrue(TEXT("Final closure succeeds"), FDeepLevelBuildingPlacementClosure::ApplyFinal(
		*Catalog, Polygon, 5.0, Plan, Stats, Error));
	TestTrue(TEXT("Final closure reports no error"), Error.IsEmpty());
	TestEqual(TEXT("Vol.Final runs ten quad phases"), Stats.PhaseCount, 10);
	TestEqual(TEXT("Vol.Final moves both placements"), Stats.MovedPlacementCount, 2);
	TestTrue(TEXT("First placement matches JS Vol.Final"),
		FMath::IsNearlyEqual(Plan.Placements[0].PathSample.Location.X, 999.516796, 0.1));
	TestTrue(TEXT("Second placement matches JS Vol.Final"),
		FMath::IsNearlyEqual(Plan.Placements[1].PathSample.Location.X, 2000.483204, 0.1));
	TestTrue(TEXT("Total shift matches JS Vol.Final"), FMath::IsNearlyEqual(Stats.TotalShift, 999.033592, 0.1));

	FDeepLevelBuildingLinePlan ConcavePlan;
	const auto AddPlacement = [&](const FVector2D& Location, const FVector2D& Forward)
	{
		FDeepLevelBuildingLinePlacement& Placement = ConcavePlan.Placements.AddDefaulted_GetRef();
		Placement.BuildingClass = AActor::StaticClass();
		Placement.StreetFace = EDeepLevelBuildingVolumeFace::NegativeY;
		Placement.PathSample.Location = FVector(Location, 0.0);
		Placement.PathSample.Forward = FVector(Forward, 0.0);
		Placement.PathSample.Right = FVector(-Forward.Y, Forward.X, 0.0);
	};
	AddPlacement({500.0, 0.0}, {1.0, 0.0});
	AddPlacement({2500.0, 0.0}, {1.0, 0.0});
	AddPlacement({5000.0, 500.0}, {0.0, 1.0});
	AddPlacement({3000.0, 3500.0}, {0.0, 1.0});
	const TArray<FVector2D> ConcavePolygon = {
		{0.0, 0.0}, {5000.0, 0.0}, {5000.0, 2000.0},
		{3000.0, 2000.0}, {3000.0, 5000.0}, {0.0, 5000.0}};
	FDeepLevelBuildingFinalClosureStats ConcaveStats;
	TestTrue(TEXT("Concave Vol.Final succeeds"), FDeepLevelBuildingPlacementClosure::ApplyFinal(
		*Catalog, ConcavePolygon, 5.0, ConcavePlan, ConcaveStats, Error));
	TestEqual(TEXT("Vol.Final runs eight concave phases"), ConcaveStats.PhaseCount, 8);
	TestEqual(TEXT("Concave Vol.Final moves all fixture placements"), ConcaveStats.MovedPlacementCount, 4);
	TestTrue(TEXT("Concave Vol.Final performs material closure"), ConcaveStats.TotalShift > 1000.0);
	return true;
}

#endif
