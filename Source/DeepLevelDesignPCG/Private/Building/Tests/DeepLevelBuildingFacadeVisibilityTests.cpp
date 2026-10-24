// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Building/DeepLevelBuildingFacadeVisibility.h"
#include "City/DeepLevelCityLayout.h"
#include "Misc/AutomationTest.h"

namespace
{
	constexpr uint8 FaceBit(const EDeepLevelBuildingVolumeFace Face)
	{
		return static_cast<uint8>(1u << static_cast<uint8>(Face));
	}

	FDeepLevelCityBuilding MakeBuilding(const int32 Id, const FVector& Location, const FVector& Extent,
		const FRotator& Rotation = FRotator::ZeroRotator, const uint8 ForbiddenFaces = 0)
	{
		FDeepLevelCityBuilding Building;
		Building.StableId = FGuid(Id, 0, 0, 1);
		Building.Transform = FTransform(Rotation, Location);
		Building.FacadeGeometry.LocalFrame = FTransform::Identity;
		Building.FacadeGeometry.Extent = Extent;
		Building.FacadeGeometry.ForbiddenFaces = ForbiddenFaces;
		return Building;
	}

	bool HasFace(const FDeepLevelCityBuilding& Building, const EDeepLevelBuildingVolumeFace Face)
	{
		return Building.VisibleFacades.ContainsByPredicate([Face](const FDeepLevelBuildingFacadeSpan& Span)
		{
			return Span.Face == Face;
		});
	}

	bool HasFaceSpan(const FDeepLevelCityBuilding& Building, const EDeepLevelBuildingVolumeFace Face,
		const double Start, const double End, const double Tolerance = 0.01)
	{
		return Building.VisibleFacades.ContainsByPredicate([=](const FDeepLevelBuildingFacadeSpan& Span)
		{
			return Span.Face == Face && FMath::IsNearlyEqual(Span.Start, Start, Tolerance)
				&& FMath::IsNearlyEqual(Span.End, End, Tolerance);
		});
	}

	bool ResolveBuildings(TArray<FDeepLevelCityBuilding>& Buildings)
	{
		FDeepLevelCityGrid Grid;
		Grid.TileSize = 100.0;
		TMap<FIntPoint, FDeepLevelCityCellState> Cells;
		FText Error;
		return DeepLevelBuildingFacades::Resolve(Buildings, Grid, Cells, Error) && Error.IsEmpty();
	}

	TArray<FDeepLevelCityBuilding> MakeCourtyardRing(const FVector& Offset = FVector::ZeroVector,
		const FRotator& Rotation = FRotator::ZeroRotator)
	{
		const auto Place = [&Offset, &Rotation](const int32 Id, const FVector& Local, const FVector& Extent)
		{
			const FVector Location = Rotation.RotateVector(Local) + Offset;
			return MakeBuilding(Id, Location, Extent, Rotation);
		};
		return {
			Place(1, FVector(0, 400, 0), FVector(500, 100, 100)),
			Place(2, FVector(0, -400, 0), FVector(500, 100, 100)),
			Place(3, FVector(400, 0, 0), FVector(100, 300, 100)),
			Place(4, FVector(-400, 0, 0), FVector(100, 300, 100))};
	}

	const FDeepLevelCityBuilding* FindBuilding(const TArray<FDeepLevelCityBuilding>& Buildings, const int32 Id)
	{
		return Buildings.FindByPredicate([Id](const FDeepLevelCityBuilding& Building)
		{
			return Building.StableId == FGuid(Id, 0, 0, 1);
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingFacadeVisibilityAdjacencyTest,
	"DeepLevelDesignPCG.Runtime.Building.FacadeVisibility.Adjacency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingFacadeVisibilityAdjacencyTest::RunTest(const FString&)
{
	TArray<FDeepLevelCityBuilding> Isolated = {MakeBuilding(1, FVector::ZeroVector, FVector(100, 100, 100))};
	TestTrue(TEXT("Isolated facade resolves"), ResolveBuildings(Isolated));
	TestEqual(TEXT("All four isolated faces are visible"), Isolated[0].VisibleFacades.Num(), 4);
	TestTrue(TEXT("Isolated face has a normalized full span"), HasFaceSpan(Isolated[0], EDeepLevelBuildingVolumeFace::PositiveX, 0.0, 1.0));

	TArray<FDeepLevelCityBuilding> Touching = {
		MakeBuilding(1, FVector::ZeroVector, FVector(100, 100, 100)),
		MakeBuilding(2, FVector(200, 0, 0), FVector(100, 100, 100))};
	TestTrue(TEXT("Touching pair resolves"), ResolveBuildings(Touching));
	TestFalse(TEXT("Touching neighbor hides the shared face"), HasFace(Touching[0], EDeepLevelBuildingVolumeFace::PositiveX));
	TestTrue(TEXT("Opposite face stays visible"), HasFace(Touching[0], EDeepLevelBuildingVolumeFace::NegativeX));
	TestTrue(TEXT("Shared face is marked as neighboring"), (Touching[0].NeighborFaces & FaceBit(EDeepLevelBuildingVolumeFace::PositiveX)) != 0);

	TArray<FDeepLevelCityBuilding> Partial = {
		MakeBuilding(1, FVector::ZeroVector, FVector(100, 100, 100)),
		MakeBuilding(2, FVector(200, 25, 0), FVector(100, 50, 100))};
	TestTrue(TEXT("Partial adjacency resolves"), ResolveBuildings(Partial));
	TestTrue(TEXT("Uncovered face interval remains visible"), HasFaceSpan(Partial[0], EDeepLevelBuildingVolumeFace::PositiveX, 0.0, 0.375));
	TestTrue(TEXT("Covered face interval is omitted"), HasFaceSpan(Partial[0], EDeepLevelBuildingVolumeFace::PositiveX, 0.875, 1.0));
	TestTrue(TEXT("A point on the uncovered interval is visible"), DeepLevelBuildingFacades::IsPointVisible(
		Partial[0], EDeepLevelBuildingVolumeFace::PositiveX, FVector(100, -50, 0)));
	TestFalse(TEXT("A point on the covered interval is hidden"), DeepLevelBuildingFacades::IsPointVisible(
		Partial[0], EDeepLevelBuildingVolumeFace::PositiveX, FVector(100, 50, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingFacadeVisibilityEnclosureTest,
	"DeepLevelDesignPCG.Runtime.Building.FacadeVisibility.Enclosure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingFacadeVisibilityEnclosureTest::RunTest(const FString&)
{
	TArray<FDeepLevelCityBuilding> ClosedRing = MakeCourtyardRing();
	TestTrue(TEXT("Closed courtyard ring resolves"), ResolveBuildings(ClosedRing));
	const auto* EastWall = FindBuilding(ClosedRing, 3);
	TestNotNull(TEXT("East courtyard wall exists"), EastWall);
	if (EastWall)
	{
		TestTrue(TEXT("Closed ring keeps the east wall exterior face visible"), HasFace(*EastWall, EDeepLevelBuildingVolumeFace::PositiveX));
		TestFalse(TEXT("Closed ring hides the east wall courtyard-facing face"), HasFace(*EastWall, EDeepLevelBuildingVolumeFace::NegativeX));
	}

	TArray<FDeepLevelCityBuilding> OpenRing = MakeCourtyardRing();
	OpenRing.RemoveAll([](const FDeepLevelCityBuilding& Building) { return Building.StableId == FGuid(4, 0, 0, 1); });
	TestTrue(TEXT("Ring with an opening resolves"), ResolveBuildings(OpenRing));
	const auto* EastWallWithOpening = FindBuilding(OpenRing, 3);
	TestNotNull(TEXT("East wall survives opening"), EastWallWithOpening);
	if (EastWallWithOpening)
	{
		TestTrue(TEXT("Opening exposes the formerly enclosed courtyard-facing face"), HasFace(*EastWallWithOpening, EDeepLevelBuildingVolumeFace::NegativeX));
	}

	TArray<FDeepLevelCityBuilding> CourtyardBuilding = MakeCourtyardRing();
	CourtyardBuilding.Add(MakeBuilding(5, FVector::ZeroVector, FVector(50, 50, 50)));
	TestTrue(TEXT("Building inside closed courtyard resolves"), ResolveBuildings(CourtyardBuilding));
	const auto* InnerBuilding = FindBuilding(CourtyardBuilding, 5);
	TestNotNull(TEXT("Inner building exists"), InnerBuilding);
	if (InnerBuilding)
	{
		TestEqual(TEXT("Closed ring hides every exterior facade of inner building"), InnerBuilding->VisibleFacades.Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingFacadeVisibilityStabilityTest,
	"DeepLevelDesignPCG.Runtime.Building.FacadeVisibility.Stability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingFacadeVisibilityStabilityTest::RunTest(const FString&)
{
	const FRotator Rotation(0.0, 37.0, 0.0);
	const FVector Translation(1200000.0, -830000.0, 0.0);
	TArray<FDeepLevelCityBuilding> Ordered = MakeCourtyardRing(Translation, Rotation);
	TestTrue(TEXT("Translated rotated layout resolves"), ResolveBuildings(Ordered));

	TArray<FDeepLevelCityBuilding> Shuffled = MakeCourtyardRing(Translation, Rotation);
	Algo::Reverse(Shuffled);
	TestTrue(TEXT("Shuffled translated layout resolves"), ResolveBuildings(Shuffled));
	for (const FDeepLevelCityBuilding& Building : Ordered)
	{
		const FDeepLevelCityBuilding* Other = FindBuilding(Shuffled, Building.StableId.A);
		TestNotNull(TEXT("Stable building id matches after shuffle"), Other);
		if (!Other) { continue; }
		TestEqual(TEXT("Visible span counts are input-order independent"), Other->VisibleFacades.Num(), Building.VisibleFacades.Num());
		for (const FDeepLevelBuildingFacadeSpan& Span : Building.VisibleFacades)
		{
			TestTrue(TEXT("Resolved face span is stable after shuffle"), HasFaceSpan(*Other, Span.Face, Span.Start, Span.End));
		}
	}

	TArray<FDeepLevelCityBuilding> Forbidden = {
		MakeBuilding(1, FVector::ZeroVector, FVector(100, 100, 100), FRotator::ZeroRotator,
			FaceBit(EDeepLevelBuildingVolumeFace::PositiveX)),
		MakeBuilding(2, FVector(200, 0, 0), FVector(100, 100, 100))};
	TestTrue(TEXT("Forbidden neighbor geometry resolves"), ResolveBuildings(Forbidden));
	TestTrue(TEXT("Forbidden face still records geometric neighbor"), (Forbidden[0].NeighborFaces & FaceBit(EDeepLevelBuildingVolumeFace::PositiveX)) != 0);
	TestFalse(TEXT("Forbidden face is never published visible"), HasFace(Forbidden[0], EDeepLevelBuildingVolumeFace::PositiveX));
	TestFalse(TEXT("Forbidden face point is never reported visible"), DeepLevelBuildingFacades::IsPointVisible(
		Forbidden[0], EDeepLevelBuildingVolumeFace::PositiveX, FVector(100, 0, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingFacadeVisibilityValidationTest,
	"DeepLevelDesignPCG.Runtime.Building.FacadeVisibility.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingFacadeVisibilityValidationTest::RunTest(const FString&)
{
	TArray<FDeepLevelCityBuilding> Pitched = {
		MakeBuilding(1, FVector::ZeroVector, FVector(100, 100, 100), FRotator(90.0, 0.0, 0.0))};
	FDeepLevelCityGrid Grid;
	TMap<FIntPoint, FDeepLevelCityCellState> Cells;
	FText Error;
	TestFalse(TEXT("A ninety-degree pitched footprint is rejected"), DeepLevelBuildingFacades::Resolve(Pitched, Grid, Cells, Error));
	TestFalse(TEXT("Pitch rejection reports an explicit validation error"), Error.IsEmpty());
	TestTrue(TEXT("Pitch rejection identifies the upright volume requirement"), Error.ToString().Contains(TEXT("upright")));
	TestTrue(TEXT("Rejected pitched building publishes no spans"), Pitched[0].VisibleFacades.IsEmpty());
	for (const FDeepLevelBuildingFacadeSpan& Span : Pitched[0].VisibleFacades)
	{
		TestFalse(TEXT("Rejected geometry cannot publish NaN span values"), FMath::IsNaN(Span.Start) || FMath::IsNaN(Span.End));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingFacadeVisibilityRoadCourtyardTest,
	"DeepLevelDesignPCG.Runtime.Building.FacadeVisibility.RoadSeededCourtyard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingFacadeVisibilityRoadCourtyardTest::RunTest(const FString&)
{
	FDeepLevelCityGrid Grid;
	Grid.TileSize = 100.0;
	const FIntPoint CourtyardCell(1, 1);
	const auto ResolveWithOccupancy = [&Grid, &CourtyardCell](const EDeepLevelCityOccupancy Occupancy,
		TArray<FDeepLevelCityBuilding>& Buildings, FText& Error)
	{
		FDeepLevelCityCellState State;
		State.Cell = CourtyardCell;
		State.OccupancyMask = static_cast<int32>(Occupancy);
		TMap<FIntPoint, FDeepLevelCityCellState> Cells;
		Cells.Add(CourtyardCell, State);
		return DeepLevelBuildingFacades::Resolve(Buildings, Grid, Cells, Error);
	};

	TArray<FDeepLevelCityBuilding> SidewalkCourtyard = MakeCourtyardRing();
	SidewalkCourtyard.Add(MakeBuilding(5, FVector::ZeroVector, FVector(50, 50, 50)));
	FText Error;
	TestTrue(TEXT("Sidewalk-only courtyard layout resolves"), ResolveWithOccupancy(EDeepLevelCityOccupancy::Sidewalk, SidewalkCourtyard, Error));
	const auto* SidewalkInner = FindBuilding(SidewalkCourtyard, 5);
	TestNotNull(TEXT("Sidewalk courtyard building exists"), SidewalkInner);
	if (SidewalkInner)
	{
		TestTrue(TEXT("Sidewalk occupancy alone does not expose courtyard facades"), SidewalkInner->VisibleFacades.IsEmpty());
	}

	TArray<FDeepLevelCityBuilding> RoadCourtyard = MakeCourtyardRing();
	RoadCourtyard.Add(MakeBuilding(5, FVector::ZeroVector, FVector(50, 50, 50)));
	TestTrue(TEXT("Road-seeded courtyard layout resolves"), ResolveWithOccupancy(EDeepLevelCityOccupancy::Road, RoadCourtyard, Error));
	const auto* RoadInner = FindBuilding(RoadCourtyard, 5);
	TestNotNull(TEXT("Road courtyard building exists"), RoadInner);
	if (RoadInner)
	{
		TestTrue(TEXT("Road occupancy exposes courtyard facades"), !RoadInner->VisibleFacades.IsEmpty());
	}
	return true;
}

#endif
