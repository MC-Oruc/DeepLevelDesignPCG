// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Building/DeepLevelBuildingVolFinalSolver.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingVolFinalCoreTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.VolFinalCore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingVolFinalCoreTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelBuildingVolFinal;
	const TArray<FVector2D> ConcavePolygon = {
		{0.0, 0.0}, {5000.0, 0.0}, {5000.0, 2000.0},
		{3000.0, 2000.0}, {3000.0, 5000.0}, {0.0, 5000.0}};
	FFootprint Inside;
	Inside.Center = FVector(500.0, 200.0, 100.0);
	Inside.HalfWidth = 500.0;
	Inside.HalfDepth = 200.0;
	Inside.MinZ = 0.0;
	Inside.MaxZ = 200.0;
	TestTrue(TEXT("Boundary-inclusive footprint matches portable JS"), FootprintContained(Inside, ConcavePolygon));

	FFootprint CrossingCut = Inside;
	CrossingCut.Center = FVector(3500.0, 2500.0, 100.0);
	CrossingCut.HalfWidth = 750.0;
	CrossingCut.HalfDepth = 750.0;
	TestFalse(TEXT("Concave cut crossing matches portable JS"), FootprintContained(CrossingCut, ConcavePolygon));

	FFootprint Touching = Inside;
	Touching.Center.X = 1500.0;
	TestFalse(TEXT("One-centimeter SAT contact is not overlap"), FootprintsOverlap(Inside, Touching));
	Touching.Center.X = 1498.5;
	TestTrue(TEXT("Sub-tolerance penetration is overlap"), FootprintsOverlap(Inside, Touching));

	FPlacement Placement;
	Placement.Center = Inside.Center;
	Placement.Footprint = Inside;
	Placement.PathSample.Location = FVector(500.0, 0.0, 0.0);
	Placement.PathSample.Forward = FVector::ForwardVector;
	Placement.Distance = 500.0;
	Placement.CoverageStart = 0.0;
	Placement.CoverageEnd = 1000.0;
	const FPlacement Moved = Translate(Placement, 125.0, 40.0);
	TestEqual(TEXT("Translate updates path distance"), Moved.Distance, 625.0);
	TestTrue(TEXT("Translate updates path sample"), Moved.PathSample.Location.Equals(FVector(625.0, 40.0, 0.0)));
	return true;
}

#endif
