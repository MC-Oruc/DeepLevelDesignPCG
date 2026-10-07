// Copyright <--\, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Automation/DeepLevelPCGAutomation.h"
#include "Automation/MCP/DeepLevelPCGToolset.h"
#include "Road/DeepLevelRoadPCG.h"
#include "AdvancedPreviewScene.h"
#include "Engine/World.h"
#include "PCGComponent.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"

namespace DeepLevelRoadAutomationTests
{
	FString Request(UObject& Target, const FString& Ops, const FString& Options = FString())
	{
		return FString::Printf(TEXT("{\"version\":1,\"domain\":\"road\",\"target\":\"%s\",\"operations\":%s%s}"), *Target.GetPathName(), *Ops, *Options);
	}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelRoadDSLCatalogTest, "DeepLevelDesignPCG.Editor.Road.Automation.CatalogBatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelRoadDSLCatalogTest::RunTest(const FString&)
{
	TStrongObjectPtr<UDeepLevelRoadTileCatalog> Catalog(NewObject<UDeepLevelRoadTileCatalog>(GetTransientPackage(), NAME_None, RF_Transactional));
	const FString Ops = TEXT(R"JSON([{"op":"tile.add","mesh":"/Engine/BasicShapes/Cube.Cube","connections":5},
		{"op":"tile.autoFit","tile":0,"tileSize":500},{"op":"tile.update","tile":0,"weight":2},{"op":"catalog.configure","sidewalkWidth":3}])JSON");
	const auto Dry = DeepLevelPCGAutomation::Execute(DeepLevelRoadAutomationTests::Request(*Catalog, Ops, TEXT(",\"dryRun\":true")));
	TestTrue(TEXT("Dry run validates a staged tile batch"), Dry.bSuccess); TestTrue(TEXT("Dry run preserves target"), Catalog->Tiles.IsEmpty());
	const auto Applied = DeepLevelPCGAutomation::Execute(DeepLevelRoadAutomationTests::Request(*Catalog, Ops));
	if (!TestTrue(TEXT("Catalog batch applies"), Applied.bSuccess)) { AddError(DeepLevelPCGAutomation::ToJson(Applied.Report)); return false; }
	TestEqual(TEXT("One tile created"), Catalog->Tiles.Num(), 1); TestTrue(TEXT("AutoFit calibrated tile"), Catalog->Tiles[0].bCalibrated);
	TestEqual(TEXT("Sidewalk settings are included in the same batch"), Catalog->SidewalkWidthInTiles, 3);
	TestEqual(TEXT("Tile geometry follows shared AutoFit"), Catalog->Tiles[0].PlacementVolume.Extent, FVector(250,250,50));
	GEditor->UndoTransaction(); TestTrue(TEXT("Single undo removes all staged tiles"), Catalog->Tiles.IsEmpty()); TestEqual(TEXT("Undo restores width"), Catalog->SidewalkWidthInTiles, 2);
	GEditor->RedoTransaction(); TestEqual(TEXT("Redo restores batch"), Catalog->Tiles.Num(), 1);
	const auto Bad = DeepLevelPCGAutomation::Execute(DeepLevelRoadAutomationTests::Request(*Catalog, TEXT(R"JSON([{"op":"catalog.configure","sidewalkWidth":4},{"op":"tile.update","tile":0,"junction":3}])JSON")));
	TestFalse(TEXT("Malformed connectivity rejects whole batch"), Bad.bSuccess); TestEqual(TEXT("Rejected batch does not change width"), Catalog->SidewalkWidthInTiles, 3);
	TestFalse(TEXT("Non-object operation is rejected without assertion"), DeepLevelPCGAutomation::Execute(DeepLevelRoadAutomationTests::Request(*Catalog, TEXT("[7]"))).bSuccess);
	TestFalse(TEXT("Fractional indices rejected"), DeepLevelPCGAutomation::Execute(DeepLevelRoadAutomationTests::Request(*Catalog, TEXT(R"JSON([{"op":"tile.remove","tile":0.5}])JSON"))).bSuccess);
	TestFalse(TEXT("Unknown fields rejected"), DeepLevelPCGAutomation::Execute(DeepLevelRoadAutomationTests::Request(*Catalog, TEXT(R"JSON([{"op":"tile.update","tile":0,"typo":1}])JSON"))).bSuccess);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelRoadDSLNetworkTest, "DeepLevelDesignPCG.Editor.Road.Automation.NetworkOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelRoadDSLNetworkTest::RunTest(const FString&)
{
	FAdvancedPreviewScene Scene{FPreviewScene::ConstructionValues()}; UWorld* World = Scene.GetWorld();
	auto* City = World->SpawnActor<ADeepLevelCityLayoutActor>(); City->GridProfile = NewObject<UDeepLevelCityGridProfile>(City);
	auto* Actor = World->SpawnActorDeferred<ADeepLevelRoadNetworkActor>(ADeepLevelRoadNetworkActor::StaticClass(), FTransform::Identity);
	Actor->CityLayout = City; Actor->PCGComponent->SetGraph(nullptr); Actor->FinishSpawning(FTransform::Identity);
	auto* Foreign = World->SpawnActorDeferred<ADeepLevelRoadNetworkActor>(ADeepLevelRoadNetworkActor::StaticClass(), FTransform::Identity);
	Foreign->PCGComponent->SetGraph(nullptr); Foreign->FinishSpawning(FTransform::Identity);
	Foreign->CreateRoadBranch()->SetRoadPathFromWorldPoints({FVector(0,0,0), FVector(1000,0,0)});
	const FString Add = TEXT(R"JSON([{"op":"branch.add","points":[[0,0,0],[1000,0,0]],"as":"new"},{"op":"branch.update","branch":"$new","points":[[0,0,0],[1500,0,0]]}])JSON");
	const auto Dry = DeepLevelPCGAutomation::Execute(DeepLevelRoadAutomationTests::Request(*Actor, Add, TEXT(",\"dryRun\":true")));
	TestTrue(TEXT("Branch aliases resolve within staged batch"), Dry.bSuccess);
	TArray<UDeepLevelRoadSplineComponent*> Before; Actor->GetRoadSplineComponents(Before);
	const auto* Call = UDeepLevelPCGToolset::Execute(DeepLevelRoadAutomationTests::Request(*Actor, Add));
	if (!TestTrue(TEXT("No graph means authoring completes immediately"), Call->bIsComplete)) { return false; }
	TestTrue(TEXT("Native Execute applies branch batch"), Call->Value.Success);
	TArray<UDeepLevelRoadSplineComponent*> After; Actor->GetRoadSplineComponents(After); TestEqual(TEXT("One owned branch added"), After.Num(), Before.Num()+1);
	TArray<UDeepLevelRoadSplineComponent*> ForeignSplines; Foreign->GetRoadSplineComponents(ForeignSplines);
	if (!ForeignSplines.IsEmpty())
	{
		const FString Bad = FString::Printf(TEXT("[{\"op\":\"branch.remove\",\"branch\":\"%s\"}]"), *ForeignSplines[0]->GetPathName());
		TestFalse(TEXT("Foreign branch cannot be mutated"), DeepLevelPCGAutomation::Execute(DeepLevelRoadAutomationTests::Request(*Actor, Bad, TEXT(",\"dryRun\":true"))).bSuccess);
	}
	const auto Cell = UDeepLevelPCGToolset::Execute(DeepLevelRoadAutomationTests::Request(*Actor, TEXT(R"JSON([{"op":"cell.set","x":1,"y":2,"mode":"remove"}])JSON")));
	TestTrue(TEXT("Cell override applies"), Cell->bIsComplete && Cell->Value.Success); TestEqual(TEXT("Override persisted on owning actor"), Actor->CellOverrides.Num(), 1);
	TestFalse(TEXT("Off-grid geometry rejected"), DeepLevelPCGAutomation::Execute(DeepLevelRoadAutomationTests::Request(*Actor, TEXT(R"JSON([{"op":"branch.add","points":[[1,0,0],[1000,0,0]]}])JSON"), TEXT(",\"dryRun\":true"))).bSuccess);
	TestFalse(TEXT("Diagonal geometry rejected"), DeepLevelPCGAutomation::Execute(DeepLevelRoadAutomationTests::Request(*Actor, TEXT(R"JSON([{"op":"branch.add","points":[[0,0,0],[1000,1000,0]]}])JSON"), TEXT(",\"dryRun\":true"))).bSuccess);
	auto* MovedCity = World->SpawnActor<ADeepLevelCityLayoutActor>();
	MovedCity->SetActorLocation(FVector(5000, 0, 0)); MovedCity->GridProfile = NewObject<UDeepLevelCityGridProfile>(MovedCity);
	TStrongObjectPtr<UDeepLevelRoadTileCatalog> Catalog(NewObject<UDeepLevelRoadTileCatalog>());
	const FString Configure = FString::Printf(TEXT("[{\"op\":\"network.configure\",\"city\":\"%s\",\"catalog\":\"%s\"}]"), *MovedCity->GetPathName(), *Catalog->GetPathName());
	const auto Moved = UDeepLevelPCGToolset::Execute(DeepLevelRoadAutomationTests::Request(*Actor, Configure));
	if (!TestTrue(TEXT("City reassignment completes"), Moved->bIsComplete && Moved->Value.Success)) { return false; }
	TSharedPtr<FJsonObject> Report;
	const auto Reader = TJsonReaderFactory<>::Create(Moved->Value.Report);
	if (!TestTrue(TEXT("Completed state report is JSON"), FJsonSerializer::Deserialize(Reader, Report))) { return false; }
	TArray<UDeepLevelRoadSplineComponent*> MovedBranches; Actor->GetRoadSplineComponents(MovedBranches);
	const auto& ReportedBranches = Report->GetArrayField(TEXT("branches"));
	for (const auto* Branch : MovedBranches)
	{
		const auto* Entry = ReportedBranches.FindByPredicate([Branch](const auto& J) { return J->AsObject()->GetStringField(TEXT("id")) == Branch->GetPathName(); });
		if (!TestNotNull(TEXT("Report contains current branch"), Entry)) { continue; }
		const auto& Point = (*Entry)->AsObject()->GetArrayField(TEXT("points"))[0]->AsArray();
		TestEqual(TEXT("Report reflects transformed world coordinates"), Point[0]->AsNumber(), Branch->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World).X);
	}
	TestEqual(TEXT("Road actor follows City origin"), Actor->GetActorLocation().X, 5000.0);
	return true;
}
#endif
