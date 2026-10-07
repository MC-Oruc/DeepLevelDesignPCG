// Copyright <--\, Inc. All Rights Reserved.
#include "Automation/DeepLevelPCGAutomation.h"
#include "City/Decoration/Automation/DeepLevelCityDecorationDSL.h"
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
		+ TEXT(R"JSON(},"request":{"version":1,"domain":"decoration","set":"asset object path","building":"catalog building class path","variant":"optional variant GUID or $alias","operations":[],"capture":false,"save":false,"dryRun":false,"includeState":true,"camera":{"location":[0,0,0],"rotation":[0,0,0],"fov":90},"captureSize":[1024,768]},"limits":{"operations":256,"requestCharacters":262144,"imageSide":2048},"semantics":{"ids":"GUIDs or $alias from an earlier operation's as field; names are not identities","transaction":"all operations stage through the document and validate before one live Undo transaction","capture":"isolated decoration preview, never PIE; application, save and capture status are separate","save":"only the selected target profile; false by default","dryRun":"validate and optionally capture staged state without live changes"}})JSON");
}

FDeepLevelPCGAutomationResult DeepLevelPCGAutomation::Execute(const FString& Request)
{
	FDeepLevelPCGAutomationResult Out;
	Out.Report->SetBoolField(TEXT("applied"), false);
	Out.Report->SetBoolField(TEXT("saved"), false);
	if (!IsInGameThread() || !GEditor || GEditor->PlayWorld)
	{
		Out.Report->SetStringField(TEXT("error"), TEXT("Requires the editor game thread with no PIE session."));
		return Out;
	}
	if (Request.Len() > 262144)
	{
		Out.Report->SetStringField(TEXT("error"), TEXT("Request exceeds 262144 characters."));
		return Out;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Request), Root) || !Root)
	{
		Out.Report->SetStringField(TEXT("error"), TEXT("Request must be a JSON object."));
		return Out;
	}
	double Version = 0;
	FString Domain;
	if (!Root->TryGetNumberField(TEXT("version"), Version) || Version != 1 || !Root->TryGetStringField(TEXT("domain"), Domain))
	{
		Out.Report->SetStringField(TEXT("error"), TEXT("version=1 and an explicit domain are required."));
		return Out;
	}
	// Composition dispatch owns no domain data. Future domains provide their own executors.
	if (Domain == TEXT("decoration")) { DeepLevelCityDecorationDSL::Execute(Root.ToSharedRef(), Out); }
	else { Out.Report->SetStringField(TEXT("error"), TEXT("Unsupported domain. Describe lists available domains.")); }
	return Out;
}
