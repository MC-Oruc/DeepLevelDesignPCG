// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "City/DeepLevelCityLayout.h"
#include "Misc/AutomationTest.h"
#include "PackedLevelActor/PackedLevelActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityBuildingSnapshotTest,
	"DeepLevelDesignPCG.Editor.City.BuildingSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityBuildingSnapshotTest::RunTest(const FString& Parameters)
{
	const FDeepLevelCityGrid Grid{FVector::ZeroVector, 500.0, 4};
	FDeepLevelCityLayoutFragment Fragment;
	Fragment.SourceGuid = FGuid(1, 2, 3, 4);
	Fragment.SourceRevision = 5;
	FDeepLevelCityBuilding& Building = Fragment.Buildings.Emplace_GetRef();
	Building.StableId = FDeepLevelCityStableId::MakeBuildingId(Fragment.SourceGuid, FGuid(), 0);
	Building.BuildingClass = APackedLevelActor::StaticClass();
	Building.Transform = FTransform(FRotator(0.0, 45.0, 0.0), FVector(100.0, 200.0, 300.0));
	FText Error;
	TSharedPtr<const FDeepLevelCityLayoutSnapshot> First;
	if (!TestTrue(TEXT("Building-only fragment builds without anchors or occupancy"),
		FDeepLevelCityLayoutBuilder::Build(Grid, {Fragment}, First, Error))) { return false; }
	TestEqual(TEXT("One building is published"), First->GetBuildings().Num(), 1);
	const FDeepLevelCityBuilding& Published = First->GetBuildings()[0];
	TestEqual(TEXT("Source identity comes from fragment"), Published.SourceGuid, Fragment.SourceGuid);
	TestEqual(TEXT("Source revision comes from fragment"), Published.SourceRevision, Fragment.SourceRevision);
	TestTrue(TEXT("Class survives snapshot composition"), Published.BuildingClass == Building.BuildingClass);
	TestTrue(TEXT("Complete transform survives snapshot composition"), Published.Transform.Equals(Building.Transform));

	TSet<FIntPoint> Dirty;
	FDeepLevelCityLayoutBuilder::FindDirtyChunks(nullptr, *First, Dirty);
	TestTrue(TEXT("Initial building dirties its chunk without an anchor"), Dirty.Contains(FIntPoint::ZeroValue));
	FDeepLevelCityLayoutBuilder::FindDirtyChunks(First.Get(), *First, Dirty);
	TestEqual(TEXT("Unchanged building keeps chunks clean"), Dirty.Num(), 0);

	Building.Transform.SetLocation(FVector(4500.0, 0.0, 300.0));
	TSharedPtr<const FDeepLevelCityLayoutSnapshot> Moved;
	if (!TestTrue(TEXT("Moved building builds"), FDeepLevelCityLayoutBuilder::Build(Grid, {Fragment}, Moved, Error))) { return false; }
	TestEqual(TEXT("Moving preserves placement identity"), Moved->GetBuildings()[0].StableId, Published.StableId);
	TestTrue(TEXT("Previous snapshot stays immutable"), Published.Transform.GetLocation().Equals(FVector(100.0, 200.0, 300.0)));
	FDeepLevelCityLayoutBuilder::FindDirtyChunks(First.Get(), *Moved, Dirty);
	TestEqual(TEXT("Movement invalidates only old and new chunks"), Dirty.Num(), 2);
	TestTrue(TEXT("Old chunk is dirty"), Dirty.Contains(FIntPoint::ZeroValue));
	TestTrue(TEXT("New chunk is dirty"), Dirty.Contains(FIntPoint(2, 0)));

	Building.BuildingClass = AActor::StaticClass();
	TSharedPtr<const FDeepLevelCityLayoutSnapshot> Replaced;
	if (!TestTrue(TEXT("Class replacement builds"), FDeepLevelCityLayoutBuilder::Build(Grid, {Fragment}, Replaced, Error))) { return false; }
	FDeepLevelCityLayoutBuilder::FindDirtyChunks(Moved.Get(), *Replaced, Dirty);
	TestEqual(TEXT("Class replacement invalidates the building chunk"), Dirty.Num(), 1);
	TestTrue(TEXT("Replacement chunk is dirty"), Dirty.Contains(FIntPoint(2, 0)));

	Building.Transform.SetRotation(FRotator(0.0, 90.0, 0.0).Quaternion());
	TSharedPtr<const FDeepLevelCityLayoutSnapshot> Rotated;
	if (!TestTrue(TEXT("Rotation builds"), FDeepLevelCityLayoutBuilder::Build(Grid, {Fragment}, Rotated, Error))) { return false; }
	FDeepLevelCityLayoutBuilder::FindDirtyChunks(Replaced.Get(), *Rotated, Dirty);
	TestEqual(TEXT("Rotation invalidates the building chunk"), Dirty.Num(), 1);

	Fragment.Buildings.Reset();
	TSharedPtr<const FDeepLevelCityLayoutSnapshot> Empty;
	if (!TestTrue(TEXT("Empty generated source builds"), FDeepLevelCityLayoutBuilder::Build(Grid, {Fragment}, Empty, Error))) { return false; }
	FDeepLevelCityLayoutBuilder::FindDirtyChunks(Rotated.Get(), *Empty, Dirty);
	TestEqual(TEXT("Removal invalidates the former chunk"), Dirty.Num(), 1);
	TestTrue(TEXT("Removed building chunk is dirty"), Dirty.Contains(FIntPoint(2, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityBuildingValidationTest,
	"DeepLevelDesignPCG.Editor.City.BuildingSnapshotValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityBuildingValidationTest::RunTest(const FString& Parameters)
{
	FDeepLevelCityLayoutFragment Fragment;
	Fragment.SourceGuid = FGuid(1, 2, 3, 4);
	FDeepLevelCityBuilding Valid;
	Valid.StableId = FDeepLevelCityStableId::MakeBuildingId(Fragment.SourceGuid, FGuid(), 0);
	Valid.BuildingClass = APackedLevelActor::StaticClass();
	Fragment.Buildings.Add(Valid);
	Fragment.Buildings.Add(Valid);
	TSharedPtr<const FDeepLevelCityLayoutSnapshot> Snapshot;
	FText Error;
	TestFalse(TEXT("Duplicate building IDs are rejected"), FDeepLevelCityLayoutBuilder::Build({}, {Fragment}, Snapshot, Error));
	Fragment.Buildings = {Valid};
	Fragment.Buildings[0].BuildingClass.Reset();
	TestFalse(TEXT("Missing class is rejected"), FDeepLevelCityLayoutBuilder::Build({}, {Fragment}, Snapshot, Error));
	Fragment.Buildings[0] = Valid;
	Fragment.Buildings[0].StableId.Invalidate();
	TestFalse(TEXT("Missing identity is rejected"), FDeepLevelCityLayoutBuilder::Build({}, {Fragment}, Snapshot, Error));
	Fragment.Buildings[0] = Valid;
	Fragment.Buildings[0].Transform.SetRotation(FQuat(0.0, 0.0, 0.0, 0.0));
	TestFalse(TEXT("Invalid transform is rejected"), FDeepLevelCityLayoutBuilder::Build({}, {Fragment}, Snapshot, Error));
	TestFalse(TEXT("Rejected snapshot is not published"), Snapshot.IsValid());
	return true;
}

#endif
