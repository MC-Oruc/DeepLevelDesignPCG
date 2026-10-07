// Copyright <--\, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Automation/DeepLevelPCGAutomation.h"
#include "Building/DeepLevelBuildingPCG.h"
#include "Building/DeepLevelBuildingLayoutAuthoring.h"
#include "AdvancedPreviewScene.h"
#include "PackedLevelActor/PackedLevelActor.h"
#include "Engine/World.h"
#include "PCGComponent.h"
#include "Road/DeepLevelRoadPCG.h"
#include "Engine/StaticMesh.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace DeepLevelBuildingAutomationTests
{
	FString Request(UObject& Target, const FString& Ops, const FString& Options = FString())
	{
		return FString::Printf(TEXT("{\"version\":1,\"domain\":\"building\",\"target\":\"%s\",\"operations\":%s%s}"), *Target.GetPathName(), *Ops, *Options);
	}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingDSLCatalogTest, "DeepLevelDesignPCG.Editor.Building.Automation.CatalogBatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelBuildingDSLCatalogTest::RunTest(const FString&)
{
	TStrongObjectPtr<UDeepLevelBuildingPlacementCatalog> Catalog(NewObject<UDeepLevelBuildingPlacementCatalog>(GetTransientPackage(), NAME_None, RF_Transactional));
	const FString Class = APackedLevelActor::StaticClass()->GetPathName();
	const FString Ops = FString::Printf(TEXT(R"JSON([{"op":"building.add","class":"%s","extent":[100,200,300],"calibrated":true,"exposure":{"positiveY":"required"}},
		{"op":"preset.add","preset":"Pair","buildings":["%s","%s"]},{"op":"preset.duplicate","preset":"Pair","name":"Pair2"}])JSON"), *Class, *Class, *Class);
	TestTrue(TEXT("Dry-run authoring succeeds"), DeepLevelPCGAutomation::Execute(DeepLevelBuildingAutomationTests::Request(*Catalog, Ops, TEXT(",\"dryRun\":true"))).bSuccess);
	TestTrue(TEXT("Dry-run does not add live buildings"), Catalog->Buildings.IsEmpty());
	const auto Applied = DeepLevelPCGAutomation::Execute(DeepLevelBuildingAutomationTests::Request(*Catalog, Ops));
	if (!TestTrue(TEXT("Building and preset batch applies"), Applied.bSuccess)) { AddError(DeepLevelPCGAutomation::ToJson(Applied.Report)); return false; }
	TestEqual(TEXT("One building"), Catalog->Buildings.Num(), 1); TestEqual(TEXT("Two presets"), Catalog->Presets.Num(), 2);
	TestEqual(TEXT("Exposure is authored"), Catalog->Buildings[0].PlacementVolume.Exposure.PositiveY, EDeepLevelStreetExposureRule::Required);
	GEditor->UndoTransaction(); TestTrue(TEXT("One Undo removes buildings and presets"), Catalog->Buildings.IsEmpty() && Catalog->Presets.IsEmpty());
	GEditor->RedoTransaction(); TestEqual(TEXT("Redo restores presets"), Catalog->Presets.Num(), 2);
	const auto Dangling = DeepLevelPCGAutomation::Execute(DeepLevelBuildingAutomationTests::Request(*Catalog, FString::Printf(TEXT("[{\"op\":\"building.remove\",\"class\":\"%s\"}]"), *Class)));
	TestFalse(TEXT("Dangling preset references reject batch"), Dangling.bSuccess); TestEqual(TEXT("Rejected removal keeps building"), Catalog->Buildings.Num(), 1);
	TestFalse(TEXT("Duplicate preset names reject batch"), DeepLevelPCGAutomation::Execute(DeepLevelBuildingAutomationTests::Request(*Catalog, TEXT(R"JSON([{"op":"preset.update","preset":"Pair2","name":"Pair"}])JSON"))).bSuccess);
	TestFalse(TEXT("Unknown exposure rules reject batch"), DeepLevelPCGAutomation::Execute(DeepLevelBuildingAutomationTests::Request(*Catalog, FString::Printf(TEXT("[{\"op\":\"building.update\",\"class\":\"%s\",\"exposure\":{\"positiveY\":\"wrong\"}}]"), *Class))).bSuccess);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingDSLLineTest, "DeepLevelDesignPCG.Editor.Building.Automation.LineOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelBuildingDSLLineTest::RunTest(const FString&)
{
	FAdvancedPreviewScene Scene{FPreviewScene::ConstructionValues()}; auto* World = Scene.GetWorld();
	auto* Actor = World->SpawnActor<ADeepLevelPCGBuildingLineActor>(); Actor->PCGComponent->SetGraph(nullptr);
	const auto Before = Actor->BuildingLine->GetLocationAtSplinePoint(1, ESplineCoordinateSpace::World);
	const FString Ops = TEXT(R"JSON([{"op":"line.update","points":[[0,0,0],[4000,0,0]],"closed":false}])JSON");
	TestTrue(TEXT("Line dry-run validates"), DeepLevelPCGAutomation::Execute(DeepLevelBuildingAutomationTests::Request(*Actor, Ops, TEXT(",\"dryRun\":true"))).bSuccess);
	TestEqual(TEXT("Dry-run preserves geometry"), Actor->BuildingLine->GetLocationAtSplinePoint(1, ESplineCoordinateSpace::World), Before);
	TestTrue(TEXT("Line batch applies"), DeepLevelPCGAutomation::Execute(DeepLevelBuildingAutomationTests::Request(*Actor, Ops)).bSuccess);
	TestEqual(TEXT("Authored geometry changed"), Actor->BuildingLine->GetLocationAtSplinePoint(1, ESplineCoordinateSpace::World), FVector(4000,0,0));
	auto* City = World->SpawnActor<ADeepLevelCityLayoutActor>(); City->GridProfile = NewObject<UDeepLevelCityGridProfile>(City);
	TStrongObjectPtr<UDeepLevelBuildingPlacementCatalog> Catalog(NewObject<UDeepLevelBuildingPlacementCatalog>());
	Actor->CityLayout = City; Actor->Catalog = Catalog.Get();
	Actor->BuildingLine->SetSplinePointType(0, ESplinePointType::Curve);
	TestTrue(TEXT("Configuration edits apply"), DeepLevelPCGAutomation::Execute(DeepLevelBuildingAutomationTests::Request(*Actor, TEXT(R"JSON([{"op":"layout.configure","seed":2000}])JSON"))).bSuccess);
	TestEqual(TEXT("Settings edits preserve authored spline type"), Actor->BuildingLine->GetSplinePointType(0), ESplinePointType::Curve);
	TestFalse(TEXT("Roadside operations cannot leak into line owner"), DeepLevelPCGAutomation::Execute(DeepLevelBuildingAutomationTests::Request(*Actor, TEXT(R"JSON([{"op":"frontage.generate"}])JSON"))).bSuccess);
	TestFalse(TEXT("Side-only settings rejected on line"), DeepLevelPCGAutomation::Execute(DeepLevelBuildingAutomationTests::Request(*Actor, TEXT(R"JSON([{"op":"layout.configure","setback":100}])JSON"))).bSuccess);
	TestFalse(TEXT("No graph cannot claim generation"), DeepLevelPCGAutomation::Execute(DeepLevelBuildingAutomationTests::Request(*Actor, TEXT("[]"), TEXT(",\"generate\":true"))).bSuccess);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelBuildingDSLFrontageTest, "DeepLevelDesignPCG.Editor.Building.Automation.FrontageOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelBuildingDSLFrontageTest::RunTest(const FString&)
{
	FAdvancedPreviewScene Scene{FPreviewScene::ConstructionValues()}; auto* World = Scene.GetWorld();
	auto* City = World->SpawnActor<ADeepLevelCityLayoutActor>(); City->GridProfile = NewObject<UDeepLevelCityGridProfile>(City);
	TStrongObjectPtr<UDeepLevelRoadTileCatalog> RoadCatalog(NewObject<UDeepLevelRoadTileCatalog>());
	for (int32 Mask : {0,1,5,3,7,15})
	{
		auto& D = RoadCatalog->Tiles.Emplace_GetRef(); D.TileMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		D.ConnectionMask = Mask; D.bCalibrated = true;
	}
	auto& Approach = RoadCatalog->Tiles.Emplace_GetRef();
	Approach.TileMesh = RoadCatalog->Tiles[0].TileMesh;
	Approach.ConnectionMask = 5; Approach.ApproachJunctionDirectionMask = 1; Approach.bCalibrated = true;
	FText CatalogError;
	if (!TestTrue(TEXT("Road fixture contains all canonical layouts"), RoadCatalog->ValidateForGeneration(CatalogError)))
	{ AddError(CatalogError.ToString()); return false; }
	auto* Road = World->SpawnActorDeferred<ADeepLevelRoadNetworkActor>(ADeepLevelRoadNetworkActor::StaticClass(), FTransform::Identity);
	Road->CityLayout = City; Road->Catalog = RoadCatalog.Get(); Road->PCGComponent->SetGraph(nullptr); Road->FinishSpawning(FTransform::Identity);
	Road->CreateRoadBranch()->SetRoadPathFromWorldPoints({FVector(0,0,0), FVector(4000,0,0)});
	Road->NotifyRoadNetworkChanged();
	auto* Actor = World->SpawnActor<ADeepLevelPCGRoadsideBuildingActor>(); Actor->CityLayout = City; Actor->PCGComponent->SetGraph(nullptr);
	const FString Ops = TEXT(R"JSON([{"op":"frontage.add","points":[[0,0,0],[1000,0,0]],"as":"new"},
		{"op":"frontage.update","frontage":"$new","points":[[0,0,0],[1500,0,0]]}])JSON");
	const auto Dry = DeepLevelPCGAutomation::Execute(DeepLevelBuildingAutomationTests::Request(*Actor, Ops, TEXT(",\"dryRun\":true")));
	TestTrue(TEXT("Manual frontage batch validates"), Dry.bSuccess); TestTrue(TEXT("Dry-run preserves frontage owner"), Actor->FrontageSplines.IsEmpty());
	const auto Result = DeepLevelPCGAutomation::Execute(DeepLevelBuildingAutomationTests::Request(*Actor, Ops));
	if (!TestTrue(TEXT("Manual frontage batch applies"), Result.bSuccess)) { AddError(DeepLevelPCGAutomation::ToJson(Result.Report)); return false; }
	TInlineComponentArray<UDeepLevelRoadsideFrontageSplineComponent*> Frontages(Actor);
	const auto Manual = Frontages.FilterByPredicate([](const auto* F) { return F->Kind == EDeepLevelRoadsideFrontageKind::Add; });
	if (!TestEqual(TEXT("One manual component exists"), Manual.Num(), 1)) { return false; }
	const FString Id = Manual[0]->FrontageId.ToString(EGuidFormats::DigitsWithHyphens);
	TestEqual(TEXT("Alias identifies persisted owner GUID"), Result.Report->GetObjectField(TEXT("ids"))->GetStringField(TEXT("new")), Id);
	TestEqual(TEXT("Staged update was applied to new frontage"), Manual[0]->GetLocationAtSplinePoint(1, ESplineCoordinateSpace::World), FVector(1500,0,0));
	const FString Exclude = FString::Printf(TEXT("[{\"op\":\"frontage.exclude\",\"frontage\":\"%s\",\"excluded\":true}]"), *Id);
	TestFalse(TEXT("Manual frontage cannot use automatic exclusion"), DeepLevelPCGAutomation::Execute(DeepLevelBuildingAutomationTests::Request(*Actor, Exclude)).bSuccess);
	const FString Remove = FString::Printf(TEXT("[{\"op\":\"frontage.remove\",\"frontage\":\"%s\"}]"), *Id);
	TestTrue(TEXT("Owned manual frontage can be removed"), DeepLevelPCGAutomation::Execute(DeepLevelBuildingAutomationTests::Request(*Actor, Remove)).bSuccess);
	TInlineComponentArray<UDeepLevelRoadsideFrontageSplineComponent*> Remaining(Actor);
	TestFalse(TEXT("Removal leaves no manual frontage"), Remaining.ContainsByPredicate([](const auto* F) { return F->Kind != EDeepLevelRoadsideFrontageKind::Automatic; }));
	if (!TestFalse(TEXT("Automatic frontage ownership is preserved"), Remaining.IsEmpty())) { return false; }
	UDeepLevelRoadsideFrontageSplineComponent* Source = Remaining[0];
	Source->SetWorldTransform(FTransform(FRotator(0, 30, 0), FVector(200, 300, 100)));
	Source->SetSplinePointType(0, ESplinePointType::Curve);
	const FVector SourcePoint = Source->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
	const FVector SourceTangent = Source->GetTangentAtSplinePoint(0, ESplineCoordinateSpace::World);
	Actor->CreateFrontageOverride(*Source, false);
	TInlineComponentArray<UDeepLevelRoadsideFrontageSplineComponent*> CopiedFrontages(Actor);
	const auto Copies = CopiedFrontages.FilterByPredicate([](const auto* F) { return F->Kind == EDeepLevelRoadsideFrontageKind::Add; });
	if (!TestEqual(TEXT("GUI copy creates one manual frontage"), Copies.Num(), 1)) { return false; }
	TestTrue(TEXT("GUI copy preserves transformed source position"), Copies[0]->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World).Equals(SourcePoint));
	TestTrue(TEXT("GUI copy preserves transformed source tangent"), Copies[0]->GetTangentAtSplinePoint(0, ESplineCoordinateSpace::World).Equals(SourceTangent));
	return true;
}
#endif
