// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "City/Decoration/DeepLevelCityBuildingDecorationProfile.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "PackedLevelActor/PackedLevelActor.h"

namespace DeepLevelCityBuildingDecorationTests
{
	UDeepLevelCityBuildingDecorationProfile* MakeProfile()
	{
		auto* Profile = NewObject<UDeepLevelCityBuildingDecorationProfile>();
		Profile->BuildingClass = APackedLevelActor::StaticClass();
		FDeepLevelCityBuildingDecorationVariant& Variant = Profile->Variants.Emplace_GetRef();
		Variant.VariantGuid = FGuid(10, 1, 1, 1);
		FDeepLevelCityBuildingDecorationEntry Mesh;
		Mesh.EntryGuid = FGuid(11, 1, 1, 1);
		Mesh.Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		Mesh.LocalTransform = FTransform(FRotator(0.0, 30.0, 0.0), FVector(3000.0, 20.0, 200.0), FVector(0.5));
		Variant.Entries.Add(Mesh);
		FDeepLevelCityBuildingDecorationEntry Light;
		Light.EntryGuid = FGuid(12, 1, 1, 1);
		Light.Output = EDeepLevelCityDecorationOutput::Actor;
		Light.ActorClass = APointLight::StaticClass();
		Light.LocalTransform.SetLocation(FVector(40.0, 50.0, 250.0));
		Variant.Entries.Add(Light);
		FDeepLevelCityBuildingDecorationEntry Decal;
		Decal.EntryGuid = FGuid(13, 1, 1, 1);
		Decal.Output = EDeepLevelCityDecorationOutput::Decal;
		Decal.DecalMaterial = UMaterial::GetDefaultMaterial(MD_DeferredDecal);
		Decal.DecalSize = FVector(10.0, 30.0, 40.0);
		Variant.Entries.Add(Decal);
		FDeepLevelCityBuildingDecorationVariant Empty;
		Empty.VariantGuid = FGuid(14, 1, 1, 1);
		Empty.SelectionWeight = 0.0;
		Profile->Variants.Add(Empty);
		return Profile;
	}

	FDeepLevelCityLayoutFragment MakeBuildings()
	{
		FDeepLevelCityLayoutFragment Fragment;
		Fragment.SourceGuid = FGuid(1, 2, 3, 4);
		for (int32 Index = 0; Index < 2; ++Index)
		{
			FDeepLevelCityBuilding& Building = Fragment.Buildings.Emplace_GetRef();
			Building.StableId = FDeepLevelCityStableId::MakeBuildingId(Fragment.SourceGuid, FGuid(), Index);
			Building.BuildingClass = APackedLevelActor::StaticClass();
			Building.Transform = FTransform(FRotator(0.0, 90.0, 0.0), FVector(Index * 5000.0, 0.0, 0.0), FVector(2.0));
		}
		return Fragment;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityBuildingDecorationResolveTest,
	"DeepLevelDesignPCG.Editor.City.BuildingDecoration.Resolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityBuildingDecorationResolveTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelCityBuildingDecorationTests;
	auto* Profile = MakeProfile();
	auto* Set = NewObject<UDeepLevelCityDecorationSet>();
	Set->BuildingProfiles.Add(Profile);
	FDeepLevelCityLayoutFragment Fragment = MakeBuildings();
	FText Error;
	TSharedPtr<const FDeepLevelCityLayoutSnapshot> Snapshot;
	if (!TestTrue(TEXT("Building snapshot builds"), FDeepLevelCityLayoutBuilder::Build({}, {Fragment}, Snapshot, Error))) { return false; }
	TArray<FDeepLevelCityResolvedDecoration> First;
	if (!TestTrue(TEXT("Profile resolves through existing City resolver"), FDeepLevelCityDecorationResolver::Resolve(*Snapshot, *Set, 77, First, Error))) { return false; }
	TestEqual(TEXT("Both buildings receive all three entries together"), First.Num(), 6);
	TSet<FGuid> Ids;
	for (const auto& Placement : First)
	{
		Ids.Add(Placement.StableId);
		TestEqual(TEXT("Complete arrangement shares selected variant"), Placement.VariantId, Profile->Variants[0].VariantGuid);
		const auto* Building = Fragment.Buildings.FindByPredicate([&Placement](const auto& Item) { return Item.StableId == Placement.BuildingId; });
		const auto* Entry = Profile->Variants[0].Entries.FindByPredicate([&Placement](const auto& Item) { return Item.EntryGuid == Placement.EntryGuid; });
		if (!TestNotNull(TEXT("Placement has building identity"), Building) || !TestNotNull(TEXT("Placement has entry identity"), Entry)) { return false; }
		TestTrue(TEXT("Local rotation, scale and position compose with building"), Placement.Transform.Equals(Entry->LocalTransform * Building->Transform));
		TestEqual(TEXT("Chunk follows decoration position"), Placement.Chunk, Snapshot->GetGrid().CellToChunk(Snapshot->GetGrid().WorldToCell(Placement.Transform.GetLocation())));
	}
	TestEqual(TEXT("Repeated buildings have independent output identities"), Ids.Num(), 6);
	TArray<FDeepLevelCityResolvedDecoration> Repeated;
	FDeepLevelCityDecorationResolver::Resolve(*Snapshot, *Set, 77, Repeated, Error);
	TestEqual(TEXT("Repeat preserves output count"), Repeated.Num(), First.Num());
	for (int32 Index = 0; Index < First.Num() && Index < Repeated.Num(); ++Index)
	{
		TestEqual(TEXT("Repeat preserves stable output identity"), Repeated[Index].StableId, First[Index].StableId);
	}

	FDeepLevelCityDecorationOverride Remove;
	Remove.Mode = EDeepLevelCityDecorationOverrideMode::Remove;
	Remove.PlacementId = First[0].StableId;
	TestTrue(TEXT("Existing removal override supports building output"), FDeepLevelCityDecorationResolver::Resolve(*Snapshot, *Set, 77, Repeated, Error, {Remove}));
	TestEqual(TEXT("Removal affects only selected decoration"), Repeated.Num(), 5);
	Profile->Variants[0].SelectionWeight = 0.0;
	Profile->Variants[1].SelectionWeight = 1.0;
	TestTrue(TEXT("Undecorated variant is supported"), FDeepLevelCityDecorationResolver::Resolve(*Snapshot, *Set, 77, Repeated, Error));
	TestEqual(TEXT("Empty arrangement emits no outputs"), Repeated.Num(), 0);
	Profile->Variants[0].SelectionWeight = 1.0;
	Fragment.Buildings[0].BuildingClass = AActor::StaticClass();
	Fragment.Buildings[1].BuildingClass = AActor::StaticClass();
	FDeepLevelCityLayoutBuilder::Build({}, {Fragment}, Snapshot, Error);
	TestTrue(TEXT("Unmapped class is left undecorated"), FDeepLevelCityDecorationResolver::Resolve(*Snapshot, *Set, 77, Repeated, Error));
	TestEqual(TEXT("Profile matches exact class only"), Repeated.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityBuildingDecorationSelectionTest,
	"DeepLevelDesignPCG.Editor.City.BuildingDecoration.Selection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityBuildingDecorationSelectionTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelCityBuildingDecorationTests;
	auto* Profile = MakeProfile();
	Profile->Variants[1].SelectionWeight = 1.0;
	auto* Set = NewObject<UDeepLevelCityDecorationSet>();
	Set->BuildingProfiles.Add(Profile);
	FText Error;
	TSharedPtr<const FDeepLevelCityLayoutSnapshot> Snapshot;
	FDeepLevelCityLayoutBuilder::Build({}, {MakeBuildings()}, Snapshot, Error);
	bool bSawDecorated = false, bSawEmpty = false;
	for (int32 Seed = 0; Seed < 32; ++Seed)
	{
		TArray<FDeepLevelCityResolvedDecoration> First, Reordered;
		TestTrue(TEXT("Weighted arrangement resolves"), FDeepLevelCityDecorationResolver::Resolve(*Snapshot, *Set, Seed, First, Error));
		bSawDecorated |= !First.IsEmpty();
		bSawEmpty |= First.Num() < 6;
		Profile->Variants.Swap(0, 1);
		TestTrue(TEXT("Reordered profile resolves"), FDeepLevelCityDecorationResolver::Resolve(*Snapshot, *Set, Seed, Reordered, Error));
		TestEqual(TEXT("Authoring order cannot change selected arrangements"), Reordered.Num(), First.Num());
		for (int32 Index = 0; Index < First.Num() && Index < Reordered.Num(); ++Index)
		{
			TestEqual(TEXT("Reordering preserves output IDs"), Reordered[Index].StableId, First[Index].StableId);
		}
	}
	TestTrue(TEXT("Seeds can select decorated arrangement"), bSawDecorated);
	TestTrue(TEXT("Seeds can select undecorated arrangement"), bSawEmpty);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityBuildingDecorationValidationTest,
	"DeepLevelDesignPCG.Editor.City.BuildingDecoration.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityBuildingDecorationValidationTest::RunTest(const FString& Parameters)
{
	using namespace DeepLevelCityBuildingDecorationTests;
	auto* Profile = MakeProfile();
	FText Error;
	TestTrue(TEXT("Valid profile passes"), Profile->Validate(Error));
	auto* Set = NewObject<UDeepLevelCityDecorationSet>();
	Set->BuildingProfiles = {Profile, Profile};
	TestFalse(TEXT("Same class cannot have multiple profiles"), Set->Validate(Error));
	Set->BuildingProfiles = {nullptr};
	TestFalse(TEXT("Missing profile fails"), Set->Validate(Error));
	Profile->Variants[0].SelectionWeight = -1.0;
	TestFalse(TEXT("Negative selection weight fails"), Profile->Validate(Error));
	Profile->Variants[0].SelectionWeight = 0.0;
	TestFalse(TEXT("No selectable variant fails"), Profile->Validate(Error));
	Profile->Variants[0].SelectionWeight = 1.0;
	Profile->Variants[0].Entries[0].Mesh.Reset();
	TestFalse(TEXT("Missing output fails"), Profile->Validate(Error));
	Profile = MakeProfile();
	Profile->Variants[0].Entries[2].DecalSize.X = 0.0;
	TestFalse(TEXT("Zero decal projection depth fails"), Profile->Validate(Error));
	Profile = MakeProfile();
	Profile->Variants[1].VariantGuid = Profile->Variants[0].VariantGuid;
	TestFalse(TEXT("Duplicate variant IDs fail"), Profile->Validate(Error));
	Profile = MakeProfile();
	Profile->Variants[0].Entries[1].EntryGuid = Profile->Variants[0].Entries[0].EntryGuid;
	TestFalse(TEXT("Duplicate entry IDs fail"), Profile->Validate(Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelCityBuildingDecorationAuthoringTest,
	"DeepLevelDesignPCG.Editor.City.BuildingDecoration.AuthoringIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelCityBuildingDecorationAuthoringTest::RunTest(const FString& Parameters)
{
	auto* Profile = DeepLevelCityBuildingDecorationTests::MakeProfile();
	const FGuid VariantId = Profile->Variants[0].VariantGuid;
	const FGuid EntryId = Profile->Variants[0].Entries[0].EntryGuid;
	const FDeepLevelCityBuildingDecorationVariant CopiedVariant = Profile->Variants[0];
	Profile->Variants.Add(CopiedVariant);
	FPropertyChangedEvent Event(nullptr);
	Profile->PostEditChangeProperty(Event);
	TestEqual(TEXT("Editing preserves existing variant ID"), Profile->Variants[0].VariantGuid, VariantId);
	TestEqual(TEXT("Editing preserves existing entry ID"), Profile->Variants[0].Entries[0].EntryGuid, EntryId);
	TestEqual(TEXT("Unrelated editing does not silently replace duplicate identity"), Profile->Variants.Last().VariantGuid, VariantId);
	FText Error;
	TestFalse(TEXT("Duplicate identities remain explicit validation errors"), Profile->Validate(Error));
	return true;
}

#endif
