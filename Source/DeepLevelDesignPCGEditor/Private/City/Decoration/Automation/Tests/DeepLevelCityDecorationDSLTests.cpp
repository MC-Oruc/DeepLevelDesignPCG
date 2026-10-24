// Copyright <--\, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Automation/DeepLevelPCGAutomation.h"
#include "Automation/MCP/DeepLevelPCGToolset.h"
#include "City/Decoration/DeepLevelCityDecorationDocument.h"
#include "City/Decoration/DeepLevelCityDecorationAuthoring.h"
#include "PackedLevelActor/PackedLevelActor.h"
#include "Engine/StaticMesh.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/PackageName.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	struct FFixture
	{
		TStrongObjectPtr<UDeepLevelCityBuildingDecorationProfile> Profile{NewObject<UDeepLevelCityBuildingDecorationProfile>(GetTransientPackage(), NAME_None, RF_Transient)};
		TStrongObjectPtr<UDeepLevelBuildingPlacementCatalog> Catalog{NewObject<UDeepLevelBuildingPlacementCatalog>(GetTransientPackage(), NAME_None, RF_Transient)};
		TStrongObjectPtr<UDeepLevelCityDecorationSet> Set;
		FString SetPackagePath;
		explicit FFixture(bool bAssigned = true)
		{
			SetPackagePath = FString::Printf(TEXT("/Game/Developers/DecorationDSLTests/%s/DA_Set"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
			Set.Reset(NewObject<UDeepLevelCityDecorationSet>(CreatePackage(*SetPackagePath), TEXT("DA_Set"), RF_Transient));
			Profile->BuildingClass = APackedLevelActor::StaticClass();
			auto& Building = Catalog->Buildings.Emplace_GetRef();
			Building.BuildingClass = Profile->BuildingClass;
			Building.bCalibrated = true;
			Set->BuildingCatalog = Catalog.Get();
			if (bAssigned) { Set->BuildingProfiles.Add(Profile.Get()); }
		}
		FString Request(const FString& Operations, const FString& Options = FString()) const
		{
			return FString::Printf(TEXT("{\"version\":1,\"domain\":\"decoration\",\"set\":\"%s\",\"building\":\"%s\",\"operations\":%s%s}"),
				*Set->GetPathName(), *Profile->BuildingClass.ToString(), *Operations, *Options);
		}
		FString NewProfilePath() const { return FPackageName::GetLongPackagePath(SetPackagePath) / (TEXT("Buildings/DA_TestProfile_") + FPackageName::GetShortName(SetPackagePath)); }
	};
	const TCHAR* Arrangement = TEXT(R"JSON([
		{"op":"variant.add","name":"Facade","as":"v"},
		{"op":"entry.add","variant":"$v","asset":"/Engine/BasicShapes/Cube.Cube","location":[10,20,100],"as":"a"},
		{"op":"entry.add","variant":"$v","asset":"/Engine/BasicShapes/Sphere.Sphere","location":[10,-20,100],"as":"b"},
		{"op":"link.create","variant":"$v","entry":"$a","target":"$b","axis":"Y","syncScale":false,"rotationMode":"copy","rotationOffset":[10,0,0]},
		{"op":"entry.update","variant":"$v","entry":"$a","location":[40,30,200],"rotation":[0,20,0],"scale":[2,2,2]}
	])JSON");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelPCGDSLBatchTest, "DeepLevelDesignPCG.Editor.City.DecorationAutomation.BatchUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelPCGDSLBatchTest::RunTest(const FString&)
{
	FFixture F;
	FDeepLevelCityDecorationDocument Observer;
	Observer.Open(F.Set.Get());
	int32 Notifications = 0;
	Observer.OnChanged.AddLambda([&](EDeepLevelCityDecorationChange, FGuid) { ++Notifications; });
	const auto Result = DeepLevelPCGAutomation::Execute(F.Request(Arrangement));
	if (!TestTrue(TEXT("Batch commits through the document"), Result.bSuccess)) { AddError(DeepLevelPCGAutomation::ToJson(Result.Report)); return false; }
	TestTrue(TEXT("Application is reported"), Result.Report->GetBoolField(TEXT("applied")));
	TestFalse(TEXT("Saving is explicit"), Result.Report->GetBoolField(TEXT("saved")));
	TestTrue(TEXT("Existing documents receive the external change"), Notifications > 0);
	if (!TestEqual(TEXT("One arrangement is created"), F.Profile->Variants.Num(), 1)) { return false; }
	const auto& Variant = F.Profile->Variants[0];
	TestEqual(TEXT("Different assets are preserved"), Variant.Entries.Num(), 2);
	TestEqual(TEXT("One transform link is created"), Variant.SymmetryPairs.Num(), 1);
	const FGuid VariantId = Variant.VariantGuid;
	const FGuid SourceId = Variant.Entries[0].EntryGuid;
	TestEqual(TEXT("Linked location follows the calibrated plane"), Variant.Entries[1].LocalTransform.GetLocation(), FVector(40,-30,200));
	TestTrue(TEXT("Copy rotation includes target offset"), Variant.Entries[1].LocalTransform.GetRotation().Equals(FRotator(10,20,0).Quaternion(), 0.0001));
	TestEqual(TEXT("Disabled scale sync preserves target scale"), Variant.Entries[1].LocalTransform.GetScale3D(), FVector::OneVector);
	TestTrue(TEXT("Linked assets can differ"), Variant.Entries[0].Mesh != Variant.Entries[1].Mesh);
	Observer.Undo();
	TestTrue(TEXT("One undo removes the entire batch, including staged operations"), F.Profile->Variants.IsEmpty());
	Observer.Redo();
	TestEqual(TEXT("One redo restores the complete batch"), F.Profile->Variants.Num(), 1);
	TestEqual(TEXT("Redo preserves variant identity"), F.Profile->Variants[0].VariantGuid, VariantId);
	TestEqual(TEXT("Redo preserves entry identity"), F.Profile->Variants[0].Entries[0].EntryGuid, SourceId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelPCGDSLAtomicTest, "DeepLevelDesignPCG.Editor.City.DecorationAutomation.AtomicAndDryRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelPCGDSLAtomicTest::RunTest(const FString&)
{
	FFixture F;
	const auto Dry = DeepLevelPCGAutomation::Execute(F.Request(Arrangement, TEXT(",\"dryRun\":true")));
	TestTrue(TEXT("Dry run validates the complete arrangement"), Dry.bSuccess && Dry.Report->GetBoolField(TEXT("validated")));
	TestFalse(TEXT("Dry run never commits"), Dry.Report->GetBoolField(TEXT("applied")));
	TestTrue(TEXT("Dry run leaves live variants untouched"), F.Profile->Variants.IsEmpty());
	const auto Rejected = DeepLevelPCGAutomation::Execute(F.Request(TEXT(R"JSON([
		{"op":"variant.add","as":"v"},
		{"op":"entry.add","variant":"$v","asset":"/Engine/BasicShapes/Cube.Cube","as":"a"},
		{"op":"entry.update","variant":"$v","entry":"$a","scale":[0,1,1]}
	])JSON")));
	TestFalse(TEXT("Late invalid transform rejects the batch"), Rejected.bSuccess);
	TestEqual(TEXT("Failure identifies the operation"), Rejected.Report->GetIntegerField(TEXT("failedOperation")), 2);
	TestTrue(TEXT("Earlier staged operations never reach the live asset"), F.Profile->Variants.IsEmpty());
	const auto Good = DeepLevelPCGAutomation::Execute(F.Request(TEXT(R"JSON([{"op":"variant.add","name":"Original"}])JSON")));
	if (!TestTrue(TEXT("Valid baseline commits"), Good.bSuccess)) { return false; }
	const FGuid Existing = F.Profile->Variants[0].VariantGuid;
	FDeepLevelCityDecorationDocument Undo;
	Undo.Open(F.Set.Get());
	DeepLevelPCGAutomation::Execute(F.Request(Arrangement, TEXT(",\"dryRun\":true")));
	DeepLevelPCGAutomation::Execute(F.Request(TEXT(R"JSON([{"op":"variant.add","as":"v"},{"op":"entry.add","variant":"$wrong","asset":"/Engine/BasicShapes/Cube.Cube"}])JSON")));
	Undo.Undo();
	TestTrue(TEXT("Rejected and dry batches add no Undo records"), F.Profile->Variants.IsEmpty());
	Undo.Redo();
	TestEqual(TEXT("Baseline redo is intact"), F.Profile->Variants[0].VariantGuid, Existing);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelPCGDSLValidationTest, "DeepLevelDesignPCG.Editor.City.DecorationAutomation.StrictContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelPCGDSLValidationTest::RunTest(const FString&)
{
	FFixture F;
	for (const TCHAR* Operations : {
		TEXT(R"JSON([{"op":"variant.add","weigth":1}])JSON"),
		TEXT(R"JSON([{"op":"variant.add","as":"v"},{"op":"variant.remove","variant":"$v","weight":1}])JSON"),
		TEXT(R"JSON([{"op":"variant.add","as":"v"},{"op":"entry.add","variant":"$v","asset":"/Engine/BasicShapes/Cube.Cube","as":"v"}])JSON"),
		TEXT(R"JSON([{"op":"variant.add","as":"v"},{"op":"entry.update","variant":"$v","entry":"$v","name":"WrongKind"}])JSON"),
		TEXT(R"JSON([{"op":"variant.add","weight":0}])JSON")})
	{
		const auto Result = DeepLevelPCGAutomation::Execute(F.Request(Operations));
		TestFalse(TEXT("Typos, ignored fields, alias collisions/kinds and invalid totals are rejected"), Result.bSuccess);
		TestTrue(TEXT("Rejected request preserves the live profile"), F.Profile->Variants.IsEmpty());
	}
	F.Catalog->Buildings[0].bCalibrated = false;
	TestFalse(TEXT("Symmetry requires catalog calibration"), DeepLevelPCGAutomation::Execute(F.Request(Arrangement)).bSuccess);
	TestFalse(TEXT("Save conflicts with dry run"), DeepLevelPCGAutomation::Execute(F.Request(TEXT("[]"), TEXT(",\"save\":true,\"dryRun\":true"))).bSuccess);
	TestFalse(TEXT("Camera parameters cannot be silently ignored"), DeepLevelPCGAutomation::Execute(F.Request(TEXT("[]"), TEXT(",\"captureSize\":[256,256]"))).bSuccess);
	TestFalse(TEXT("Unknown version rejected"), DeepLevelPCGAutomation::Execute(TEXT("{\"version\":2,\"domain\":\"decoration\"}")).bSuccess);
	TestFalse(TEXT("Future domains are not pretended to be implemented"), DeepLevelPCGAutomation::Execute(TEXT("{\"version\":1,\"domain\":\"road\"}")).bSuccess);
	const auto Inspection = DeepLevelPCGAutomation::Execute(F.Request(TEXT("[]")));
	TestTrue(TEXT("Invalid profile can be inspected"), Inspection.bSuccess);
	TestFalse(TEXT("Inspection reports actual validation status"), Inspection.Report->GetBoolField(TEXT("validated")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelPCGDSLProfileCreationTest, "DeepLevelDesignPCG.Editor.City.DecorationAutomation.ProfileCreateAttachDryRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelPCGDSLProfileCreationTest::RunTest(const FString&)
{
	FFixture F(false);
	const FString Path = F.NewProfilePath();
	const FString CreateOptions = FString::Printf(TEXT(",\"profile\":\"%s\""), *Path);
	const FString DryOptions = CreateOptions + TEXT(",\"dryRun\":true");
	const FString Capture = TEXT(R"JSON(,"capture":true,"captureSize":[256,256],"camera":{"location":[400,400,300],"rotation":[-20,-135,0]},"variant":"$v")JSON");
	const auto Dry = DeepLevelPCGAutomation::Execute(F.Request(TEXT(R"JSON([{"op":"variant.add","name":"Preview","as":"v"}])JSON"), DryOptions + Capture));
	TestTrue(TEXT("Unassigned profile dry run stages and validates"), Dry.bSuccess && Dry.Report->GetBoolField(TEXT("validated")));
	TestFalse(TEXT("Dry run does not report application"), Dry.Report->GetBoolField(TEXT("applied")));
	TestTrue(TEXT("Dry run leaves the live set unassigned"), F.Set->BuildingProfiles.IsEmpty());
	const FString ObjectPath = Path + TEXT(".") + FPackageName::GetShortName(Path);
	TestNull(TEXT("Dry run creates no persistent profile object"), FindObject<UDeepLevelCityBuildingDecorationProfile>(nullptr, *ObjectPath));
	if (FApp::CanEverRender()) { TestTrue(TEXT("Unassigned profile dry run captures its staged variant"), Dry.Report->GetBoolField(TEXT("captured"))); }

	const auto Created = DeepLevelPCGAutomation::Execute(F.Request(TEXT(R"JSON([{"op":"variant.add","name":"Initial"}])JSON"), CreateOptions));
	if (!TestTrue(TEXT("AssetTools creates and document attaches the profile"), Created.bSuccess)) { AddError(DeepLevelPCGAutomation::ToJson(Created.Report)); return false; }
	TestEqual(TEXT("One profile assignment is made"), F.Set->BuildingProfiles.Num(), 1);
	auto* Profile = Cast<UDeepLevelCityBuildingDecorationProfile>(F.Set->BuildingProfiles[0]);
	TestNotNull(TEXT("Created asset is assigned"), Profile);
	if (Profile)
	{
		TestEqual(TEXT("Attach initializes the selected catalog class"), Profile->BuildingClass, F.Profile->BuildingClass);
		TestEqual(TEXT("Attach default and staged variant both reach the created profile"), Profile->Variants.Num(), 2);
	}
	TestTrue(TEXT("Creation result reports unsaved profile"), !Created.Report->GetBoolField(TEXT("saved")));

	FFixture Blank(false);
	const FString BlankPath = Blank.NewProfilePath();
	auto* BlankAsset = NewObject<UDeepLevelCityBuildingDecorationProfile>(CreatePackage(*BlankPath), *FPackageName::GetShortName(BlankPath));
	const auto Reused = DeepLevelPCGAutomation::Execute(Blank.Request(TEXT("[]"), FString::Printf(TEXT(",\"profile\":\"%s\""), *BlankPath)));
	TestTrue(TEXT("Existing blank profile asset is attached"), Reused.bSuccess);
	TestTrue(TEXT("Existing blank asset is reused by identity"), Blank.Set->BuildingProfiles.Num() == 1 && Blank.Set->BuildingProfiles[0] == BlankAsset);
	TestEqual(TEXT("Blank asset is initialized for the selected class"), BlankAsset->BuildingClass, Blank.Profile->BuildingClass);

	FFixture Occupied(false);
	const FString OccupiedPath = Occupied.NewProfilePath();
	const FString OccupiedObjectPath = OccupiedPath + TEXT(".") + FPackageName::GetShortName(OccupiedPath);
	auto* OccupiedAsset = NewObject<UDeepLevelCityBuildingDecorationProfile>(CreatePackage(*OccupiedPath), *FPackageName::GetShortName(OccupiedPath));
	OccupiedAsset->BuildingClass = APackedLevelActor::StaticClass();
	const auto Rejected = DeepLevelPCGAutomation::Execute(Occupied.Request(TEXT("[]"), FString::Printf(TEXT(",\"profile\":\"%s\""), *OccupiedPath)));
	TestFalse(TEXT("Occupied existing profile is rejected"), Rejected.bSuccess);
	TestEqual(TEXT("Rejected request leaves the set unassigned"), Occupied.Set->BuildingProfiles.Num(), 0);
	TestTrue(TEXT("Rejected request preserves occupied asset"), FindObject<UDeepLevelCityBuildingDecorationProfile>(nullptr, *OccupiedObjectPath) == OccupiedAsset);
	FFixture OutOfScopeFixture(false);
	const auto OutOfScope = DeepLevelPCGAutomation::Execute(OutOfScopeFixture.Request(TEXT("[]"), TEXT(R"JSON(,"profile":"/Game/Other/DA_Profile")JSON")));
	TestFalse(TEXT("Profile packages outside the set Buildings folder are rejected"), OutOfScope.bSuccess);
	const FString ProfileWithoutBuilding = FString::Printf(TEXT("{\"version\":1,\"domain\":\"decoration\",\"set\":\"%s\",\"profile\":\"%s\",\"operations\":[]}"), *OutOfScopeFixture.Set->GetPathName(), *OutOfScopeFixture.NewProfilePath());
	TestFalse(TEXT("Profile creation without a building is rejected"), DeepLevelPCGAutomation::Execute(ProfileWithoutBuilding).bSuccess);
	FFixture Duplicate(false);
	Duplicate.Set->BuildingProfiles.Add(Duplicate.Profile.Get());
	Duplicate.Set->BuildingProfiles.Add(Duplicate.Profile.Get());
	const auto DuplicateAssignment = DeepLevelPCGAutomation::Execute(Duplicate.Request(TEXT("[]"), FString::Printf(TEXT(",\"profile\":\"%s\""), *Duplicate.NewProfilePath())));
	TestFalse(TEXT("Duplicate building assignments cannot be replaced by creation"), DuplicateAssignment.bSuccess);
	TestEqual(TEXT("Rejected duplicate assignment remains intact"), Duplicate.Set->BuildingProfiles.Num(), 2);
	const FString WrongClassPath = FString::Printf(TEXT("{\"version\":1,\"domain\":\"decoration\",\"set\":\"%s\",\"building\":\"/Script/Engine.Actor\",\"profile\":\"%s\",\"operations\":[]}"), *F.Set->GetPathName(), *F.NewProfilePath());
	TestFalse(TEXT("Building classes outside the exact catalog class are rejected"), DeepLevelPCGAutomation::Execute(WrongClassPath).bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelPCGDSLCaptureTest, "DeepLevelDesignPCG.Editor.City.DecorationAutomation.CaptureContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelPCGDSLCaptureTest::RunTest(const FString&)
{
	FFixture F;
	const FString Operations = TEXT(R"JSON([{"op":"variant.add","as":"v"},{"op":"entry.add","variant":"$v","asset":"/Engine/BasicShapes/Cube.Cube","location":[0,0,100]}])JSON");
	const auto* Call = UDeepLevelPCGToolset::Execute(F.Request(Operations,
		TEXT(R"JSON(,"variant":"$v","dryRun":true,"capture":true,"captureSize":[256,256],"camera":{"location":[400,400,300],"rotation":[-20,-135,0]})JSON")));
	if (!TestTrue(TEXT("Decoration async result completes immediately"), Call->bIsComplete)) { return false; }
	const auto& Output = Call->Value;
	TSharedPtr<FJsonObject> Report;
	if (!TestTrue(TEXT("Capture report is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Output.Report), Report))) { return false; }
	TestTrue(TEXT("Capture request retains successful staging status"), Output.Success);
	TestTrue(TEXT("Capture does not commit a dry run"), F.Profile->Variants.IsEmpty());
	if (FApp::CanEverRender())
	{
		if (!TestTrue(TEXT("Updated isolated preview is captured"), Report->GetBoolField(TEXT("captured"))))
		{
			AddError(Output.Report);
			return false;
		}
		TestEqual(TEXT("Requested preview width is honored"), Report->GetObjectField(TEXT("camera"))->GetIntegerField(TEXT("width")), 256);
		TestEqual(TEXT("Requested preview height is honored"), Report->GetObjectField(TEXT("camera"))->GetIntegerField(TEXT("height")), 256);
		TestEqual(TEXT("Native tool image is PNG"), Output.Image.MimeType, FString(TEXT("image/png")));
		TestFalse(TEXT("Image has a payload"), Output.Image.Data.IsEmpty());
		TestTrue(TEXT("Capture identifies visible decoration pivots"), !Report->GetArrayField(TEXT("labels")).IsEmpty());
	}
	else
	{
		TestFalse(TEXT("Headless run reports capture unavailable"), Report->GetBoolField(TEXT("captured")));
		TestTrue(TEXT("Headless error is explicit"), Report->HasField(TEXT("captureError")));
	}
	return true;
}
#endif
