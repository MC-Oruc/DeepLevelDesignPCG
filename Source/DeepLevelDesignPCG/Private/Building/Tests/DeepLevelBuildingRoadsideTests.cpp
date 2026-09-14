// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Building/DeepLevelBuildingLayout.h"
#include "Misc/AutomationTest.h"

namespace DeepLevelBuildingRoadsideTests
{
	FDeepLevelCityLayoutFragment MakeRoad()
	{
		FDeepLevelCityLayoutFragment Road;
		Road.SourceGuid = FGuid(1, 2, 3, 4);
		for (int32 X = 0; X < 6; ++X)
		{
			Road.Cells.Add({FIntPoint(X, 0), static_cast<int32>(EDeepLevelCityOccupancy::Road), {}});
			Road.Cells.Add({FIntPoint(X, 1), static_cast<int32>(EDeepLevelCityOccupancy::Sidewalk), {}});
			Road.Cells.Add({FIntPoint(X, 2), static_cast<int32>(EDeepLevelCityOccupancy::Sidewalk), {}});
			for (int32 Y = 1; Y <= 2; ++Y)
			{
				FDeepLevelCityAnchor& Surface = Road.Anchors.Emplace_GetRef();
				Surface.StableId = FDeepLevelCityStableId::MakeAnchorId(
					Road.SourceGuid, FString::Printf(TEXT("%d:%d"), X, Y), TEXT("SidewalkSurface"));
				Surface.Geometry = EDeepLevelCityAnchorGeometry::Surface;
				Surface.Transform = FTransform(FVector(X * 500.0, Y * 500.0, 0.0));
				Surface.OccupiedCells.Add(FIntPoint(X, Y));
				Surface.Tags.AddTag(DeepLevelCityTags::Anchor_Sidewalk_Surface);
			}
			FDeepLevelCityAnchor& Anchor = Road.Anchors.Emplace_GetRef();
			Anchor.StableId = FDeepLevelCityStableId::MakeAnchorId(Road.SourceGuid, FString::FromInt(X), TEXT("SidewalkEdge"));
			Anchor.Geometry = EDeepLevelCityAnchorGeometry::Segment;
			Anchor.Transform = FTransform(FVector(X * 500.0, 325.0, 0));
			Anchor.Extent = FVector(250, 0, 0);
			Anchor.OccupiedCells.Add(FIntPoint(X, 1));
			Anchor.Tags.AddTag(DeepLevelCityTags::Anchor_Sidewalk_Edge);
		}
		return Road;
	}

	FDeepLevelCityLayoutFragment MakeBlockLoop()
	{
		FDeepLevelCityLayoutFragment Road;
		Road.SourceGuid = FGuid(20, 21, 22, 23);
		const FIntPoint SidewalkCells[] = {FIntPoint(0, 1), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(-1, 0)};
		const FIntPoint Steps[] = {FIntPoint(0, -1), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(1, 0)};
		const FVector Tangents[] = {FVector(-1, 0, 0), FVector(0, 1, 0), FVector(1, 0, 0), FVector(0, -1, 0)};
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Road.Cells.Add({SidewalkCells[Index], static_cast<int32>(EDeepLevelCityOccupancy::Sidewalk), {}});
			Road.Cells.Add({SidewalkCells[Index] - Steps[Index], static_cast<int32>(EDeepLevelCityOccupancy::Road), {}});
			FDeepLevelCityAnchor& Surface = Road.Anchors.Emplace_GetRef();
			Surface.StableId = FDeepLevelCityStableId::MakeAnchorId(Road.SourceGuid, FString::FromInt(Index), TEXT("SidewalkSurface"));
			Surface.Geometry = EDeepLevelCityAnchorGeometry::Surface;
			Surface.Transform = FTransform(FVector(SidewalkCells[Index].X * 500.0, SidewalkCells[Index].Y * 500.0, 0.0));
			Surface.OccupiedCells.Add(SidewalkCells[Index]);
			Surface.Tags.AddTag(DeepLevelCityTags::Anchor_Sidewalk_Surface);
			FDeepLevelCityAnchor& Anchor = Road.Anchors.Emplace_GetRef();
			Anchor.StableId = FDeepLevelCityStableId::MakeAnchorId(Road.SourceGuid, FString::FromInt(Index), TEXT("SidewalkEdge"));
			Anchor.Geometry = EDeepLevelCityAnchorGeometry::Segment;
			Anchor.Transform = FTransform(FRotationMatrix::MakeFromXZ(Tangents[Index], FVector::UpVector).ToQuat());
			Anchor.Extent = FVector(250, 0, 0);
			Anchor.OccupiedCells.Add(SidewalkCells[Index]);
			Anchor.Tags.AddTag(DeepLevelCityTags::Anchor_Sidewalk_Edge);
		}
		return Road;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingRoadsideSplineTest,
	"DeepLevelDesignPCG.Building.Roadside.FrontageSplines", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingRoadsideSplineTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelBuildingRoadsideTests;
	FDeepLevelCityLayoutFragment Road = MakeRoad();
	FDeepLevelCityGrid Grid;
	TSharedPtr<const FDeepLevelCityLayoutSnapshot> Base;
	FText Error;
	const TArray<FDeepLevelCityLayoutFragment> Fragments = {Road};
	if (!TestTrue(TEXT("Road snapshot builds"), FDeepLevelCityLayoutBuilder::Build(Grid, Fragments, Base, Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	TArray<DeepLevelBuildingRoadside::FFrontageSpline> Splines;
	if (!TestTrue(TEXT("Road produces frontage splines"), DeepLevelBuildingRoadside::BuildFrontageSplines(*Base, Splines, Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	TestEqual(TEXT("Continuous sidewalk becomes one spline"), Splines.Num(), 1);
	if (Splines.Num() == 1)
	{
		TestFalse(TEXT("Straight frontage is open"), Splines[0].bClosed);
		TestEqual(TEXT("Straight frontage keeps endpoints only"), Splines[0].Points.Num(), 2);
		for (const FVector& Point : Splines[0].Points)
		{
			TestEqual(TEXT("Spline follows the block-side sidewalk boundary"), Point.Y, 1250.0);
		}
	}
	Splines.Reset();
	TestTrue(TEXT("Setback frontage builds"),
		DeepLevelBuildingRoadside::BuildFrontageSplines(*Base, Splines, Error, 200.0));
	if (Splines.Num() == 1)
	{
		for (const FVector& Point : Splines[0].Points)
		{
			TestEqual(TEXT("Setback moves frontage into the block"), Point.Y, 1450.0);
		}
	}
	Splines.Reset();
	TestTrue(TEXT("Negative setback frontage builds"),
		DeepLevelBuildingRoadside::BuildFrontageSplines(*Base, Splines, Error, -200.0));
	if (Splines.Num() == 1)
	{
		for (const FVector& Point : Splines[0].Points)
		{
			TestEqual(TEXT("Negative setback moves frontage toward the road"), Point.Y, 1050.0);
		}
	}
	const TArray<FDeepLevelCityLayoutFragment> LoopFragments = {MakeBlockLoop()};
	if (!TestTrue(TEXT("Block loop snapshot builds"), FDeepLevelCityLayoutBuilder::Build(Grid, LoopFragments, Base, Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	Splines.Reset();
	TestTrue(TEXT("Connected corners produce frontage"), DeepLevelBuildingRoadside::BuildFrontageSplines(*Base, Splines, Error));
	TestEqual(TEXT("One block boundary becomes one spline"), Splines.Num(), 1);
	if (Splines.Num() == 1)
	{
		TestTrue(TEXT("Block boundary is closed"), Splines[0].bClosed);
		TestEqual(TEXT("Block boundary keeps four corners"), Splines[0].Points.Num(), 4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingFootprintContactTest,
	"DeepLevelDesignPCG.Building.Roadside.FootprintContact", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingFootprintContactTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelBuildingLayoutGeometry;
	const FFootprint A = {FVector2D(0,0), FVector2D(100,0), FVector2D(100,100), FVector2D(0,100)};
	FFootprint B = A;
	for (FVector2D& Corner : B) { Corner.X += 100.0; }
	TestFalse(TEXT("Shared boundary is allowed"), FootprintsOverlap(A, B));
	for (FVector2D& Corner : B) { Corner.X -= 1.0; }
	TestTrue(TEXT("Positive-area overlap is rejected"), FootprintsOverlap(A, B));
	return true;
}

#endif
