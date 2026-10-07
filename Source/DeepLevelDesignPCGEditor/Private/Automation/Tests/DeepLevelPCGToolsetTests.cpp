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
	TestTrue(TEXT("Road is advertised"), Contract->GetObjectField(TEXT("domains"))->HasField(TEXT("road")));
	TestTrue(TEXT("Building is advertised"), Contract->GetObjectField(TEXT("domains"))->HasField(TEXT("building")));
	TestEqual(TEXT("Exactly three implemented domains"), Contract->GetObjectField(TEXT("domains"))->Values.Num(), 3);
	const FString Schema = UToolsetRegistry::GetToolsetJsonSchema(UDeepLevelPCGToolset::StaticClass());
	TSharedPtr<FJsonObject> Native;
	TestTrue(TEXT("UE 5.8 accepts the reflected toolset schema"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Schema), Native));
	TestTrue(TEXT("Native schema exposes batch Execute"), Schema.Contains(TEXT("Execute")) && Schema.Contains(TEXT("Request")));
	TestTrue(TEXT("Native schema exposes Describe"), Schema.Contains(TEXT("Describe")));
	TestTrue(TEXT("Startup registers the native toolset"), UToolsetRegistry::IsToolsetClassRegistered(UDeepLevelPCGToolset::StaticClass()));
	int32 ToolCount = 0;
	for (TFieldIterator<UFunction> Function(UDeepLevelPCGToolset::StaticClass()); Function; ++Function)
	{ if (Function->HasMetaData(TEXT("AICallable"))) { ++ToolCount; } }
	TestEqual(TEXT("Domain expansion keeps exactly two tools"), ToolCount, 2);
	const auto* Return = FindFProperty<FObjectProperty>(UDeepLevelPCGToolset::StaticClass()->FindFunctionByName(TEXT("Execute")), TEXT("ReturnValue"));
	TestTrue(TEXT("Execute exposes a native UE asynchronous result"), Return && Return->PropertyClass->IsChildOf(UToolCallAsyncResult::StaticClass()));
	return true;
}
#endif
