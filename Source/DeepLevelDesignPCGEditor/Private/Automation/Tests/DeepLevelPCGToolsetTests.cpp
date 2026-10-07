// Copyright <--\, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Automation/MCP/DeepLevelPCGToolset.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonSerializer.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelPCGToolsetSchemaTest, "DeepLevelDesignPCG.Editor.Automation.NativeToolsetSchema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelPCGToolsetSchemaTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Contract;
	if (!TestTrue(TEXT("Describe supplies a valid DSL contract"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(UDeepLevelPCGToolset::Describe()), Contract))) { return false; }
	TestEqual(TEXT("Contract is versioned"), Contract->GetIntegerField(TEXT("version")), 1);
	TestTrue(TEXT("Decoration is advertised"), Contract->GetObjectField(TEXT("domains"))->HasField(TEXT("decoration")));
	TestEqual(TEXT("No future domains falsely advertised"), Contract->GetObjectField(TEXT("domains"))->Values.Num(), 1);
	const FString Schema = UToolsetRegistry::GetToolsetJsonSchema(UDeepLevelPCGToolset::StaticClass());
	TSharedPtr<FJsonObject> Native;
	TestTrue(TEXT("UE 5.8 accepts the reflected toolset schema"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Schema), Native));
	TestTrue(TEXT("Native schema exposes batch Execute"), Schema.Contains(TEXT("Execute")) && Schema.Contains(TEXT("Request")));
	TestTrue(TEXT("Native schema exposes Describe"), Schema.Contains(TEXT("Describe")));
	TestTrue(TEXT("Startup registers the native toolset"), UToolsetRegistry::IsToolsetClassRegistered(UDeepLevelPCGToolset::StaticClass()));
	return true;
}
#endif
