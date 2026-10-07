// Copyright <--\, Inc. All Rights Reserved.
#include "Automation/DeepLevelPCGAutomation.h"
#include "City/Decoration/Automation/DeepLevelCityDecorationDSL.h"
#include "Road/Automation/DeepLevelRoadDSL.h"
#include "Building/Automation/DeepLevelBuildingDSL.h"
#include "Automation/DeepLevelPCGGeneration.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Editor.h"

FString DeepLevelPCGAutomation::ToJson(const TSharedRef<FJsonObject>& Object)
{
	FString Text;
	FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
	return Text;
}

FString DeepLevelPCGAutomation::Describe()
{
 return TEXT(R"JSON({"version":1,"domains":{"decoration":)JSON") + DeepLevelCityDecorationDSL::Describe()
  + TEXT(R"JSON(,"road":)JSON") + DeepLevelRoadDSL::Describe()
  + TEXT(R"JSON(,"building":)JSON") + DeepLevelBuildingDSL::Describe()
  + TEXT(R"JSON(},"request":{"version":1,"domain":"decoration|road|building","operations":[],"capture":false,"save":false,"dryRun":false,"includeState":true,"camera":{"location":[0,0,0],"rotation":[0,0,0],"fov":90},"captureSize":[1024,768]},"limits":{"operations":256,"requestCharacters":262144,"imageSide":2048},"semantics":{"authoring":"validate staged edits before one Undo transaction; PCG generation and final alignment are separate phases, not atomically rolled back","generation":"Execute awaits requested PCG completion without blocking the editor; timeoutSeconds 1..300, default 120; cancellation and verification errors are explicit","statuses":"applied/generated/aligned/saved/captured are distinct; inspect lastGenerationError and validation","save":"false by default; selected target package only, including preexisting dirty changes in that package; no SaveAll","capture":"fresh isolated preview or editor-world viewport; no PIE or user camera changes","dryRun":"validate authoring without live edits or generation; catalog staged previews supported"}})JSON");
}

namespace
{
 bool ReadRequest(const FString& Request, TSharedPtr<FJsonObject>& Root, FString& Domain, FDeepLevelPCGAutomationResult& Out)
 {
  Out.Report->SetBoolField(TEXT("applied"), false); Out.Report->SetBoolField(TEXT("saved"), false);
  Out.Report->SetBoolField(TEXT("generated"), false); Out.Report->SetBoolField(TEXT("captured"), false);
  if (!IsInGameThread() || !GEditor || GEditor->PlayWorld)
  { Out.Report->SetStringField(TEXT("error"), TEXT("Requires the editor game thread with no PIE session.")); return false; }
  if (Request.Len() > 262144)
  { Out.Report->SetStringField(TEXT("error"), TEXT("Request exceeds 262144 characters.")); return false; }
  if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Request), Root) || !Root)
  { Out.Report->SetStringField(TEXT("error"), TEXT("Request must be a JSON object.")); return false; }
  double Version = 0;
  if (!Root->TryGetNumberField(TEXT("version"), Version) || Version != 1 || !Root->TryGetStringField(TEXT("domain"), Domain))
  { Out.Report->SetStringField(TEXT("error"), TEXT("version=1 and an explicit domain are required.")); return false; }
  return true;
 }
 bool PrepareDomain(const TSharedRef<FJsonObject>& Root, const FString& Domain, FDeepLevelPCGAutomationPlan& Plan, FDeepLevelPCGAutomationResult& Out)
 {
  if (Domain == TEXT("road")) { return DeepLevelRoadDSL::Prepare(Root, Plan, Out); }
  if (Domain == TEXT("building")) { return DeepLevelBuildingDSL::Prepare(Root, Plan, Out); }
  Out.Report->SetStringField(TEXT("error"), TEXT("Unsupported domain. Describe lists available domains.")); return false;
 }
}
FDeepLevelPCGAutomationResult DeepLevelPCGAutomation::Execute(const FString& Request)
{
 FDeepLevelPCGAutomationResult Out; TSharedPtr<FJsonObject> Root; FString Domain;
 if (!ReadRequest(Request, Root, Domain, Out)) { return Out; }
 if (Domain == TEXT("decoration")) { DeepLevelCityDecorationDSL::Execute(Root.ToSharedRef(), Out); return Out; }
 FDeepLevelPCGAutomationPlan Plan;
 if (!PrepareDomain(Root.ToSharedRef(), Domain, Plan, Out)) { return Out; }
 if (Plan.bRequiresAsync)
 { Out.Report->SetStringField(TEXT("error"), TEXT("Generation-capable actor edits require ExecuteAsync; no changes applied.")); return Out; }
 DeepLevelPCGGeneration::Run(MoveTemp(Plan), MoveTemp(Out), [&Out](FDeepLevelPCGAutomationResult R) { Out = MoveTemp(R); });
 return Out;
}
void DeepLevelPCGAutomation::ExecuteAsync(const FString& Request, TFunction<void(FDeepLevelPCGAutomationResult)>&& Completed)
{
 FDeepLevelPCGAutomationResult Out; TSharedPtr<FJsonObject> Root; FString Domain;
 if (!ReadRequest(Request, Root, Domain, Out)) { Completed(MoveTemp(Out)); return; }
 if (Domain == TEXT("decoration"))
 { DeepLevelCityDecorationDSL::Execute(Root.ToSharedRef(), Out); Completed(MoveTemp(Out)); return; }
 FDeepLevelPCGAutomationPlan Plan;
 if (!PrepareDomain(Root.ToSharedRef(), Domain, Plan, Out)) { Completed(MoveTemp(Out)); return; }
 DeepLevelPCGGeneration::Run(MoveTemp(Plan), MoveTemp(Out), MoveTemp(Completed));
}
