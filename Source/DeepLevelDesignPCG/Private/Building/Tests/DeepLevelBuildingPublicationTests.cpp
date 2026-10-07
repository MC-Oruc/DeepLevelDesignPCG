// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Building/DeepLevelBuildingLayout.h"
#include "Misc/AutomationTest.h"
#include "PackedLevelActor/PackedLevelActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingPublicationTest,
	"DeepLevelDesignPCG.Building.Publication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingPublicationTest::RunTest(const FString& Parameters)
{
	const FGuid Source(1, 2, 3, 4);
	const FGuid Frontage(5, 6, 7, 8);
	FDeepLevelBuildingLinePlan Plan;
	FDeepLevelBuildingLinePlacement Placement;
	Placement.FrontageId = Frontage;
	Placement.BuildingClass = APackedLevelActor::StaticClass();
	Placement.CoverageStart = 100.0;
	Plan.Placements = {Placement, Placement};
	const TArray<FTransform> Transforms = {FTransform(FVector(100.0, 200.0, 0.0)), FTransform(FVector(300.0, 200.0, 0.0))};
	const auto First = DeepLevelBuildingLayout::BuildPublishedBuildings(Source, Plan, Transforms);
	TestEqual(TEXT("One record per verified output"), First.Num(), 2);
	TestNotEqual(TEXT("Repeated class instances have different identities"), First[0].StableId, First[1].StableId);
	TestTrue(TEXT("Verified actor transform is used"), First[0].Transform.Equals(Transforms[0]));

	Plan.Placements[0].CoverageStart += 25.0;
	Plan.Placements[0].CoverageEnd += 25.0;
	const TArray<FTransform> AlignedTransforms = {FTransform(FVector(125.0, 200.0, 0.0)), Transforms[1]};
	const auto Aligned = DeepLevelBuildingLayout::BuildPublishedBuildings(Source, Plan, AlignedTransforms);
	TestEqual(TEXT("Final alignment preserves building identity"), Aligned[0].StableId, First[0].StableId);
	TestTrue(TEXT("Final alignment updates transform"), Aligned[0].Transform.Equals(AlignedTransforms[0]));
	const auto Duplicated = DeepLevelBuildingLayout::BuildPublishedBuildings(FGuid(9, 10, 11, 12), Plan, AlignedTransforms);
	TestNotEqual(TEXT("Different sources cannot share building identity"), Duplicated[0].StableId, Aligned[0].StableId);

	FDeepLevelBuildingLinePlacement OtherFrontage = Placement;
	OtherFrontage.FrontageId = FGuid(13, 14, 15, 16);
	Plan.Placements.Insert(OtherFrontage, 0);
	const TArray<FTransform> ExtendedTransforms = {FTransform::Identity, AlignedTransforms[0], AlignedTransforms[1]};
	const auto Extended = DeepLevelBuildingLayout::BuildPublishedBuildings(Source, Plan, ExtendedTransforms);
	TestEqual(TEXT("Adding another frontage preserves existing identities"), Extended[1].StableId, Aligned[0].StableId);
	return true;
}

#endif
