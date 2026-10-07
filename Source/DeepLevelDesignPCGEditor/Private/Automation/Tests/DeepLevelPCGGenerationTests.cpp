// Copyright <--\, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Automation/DeepLevelPCGGeneration.h"
#include "PCGComponent.h"
#include "Road/DeepLevelRoadPCG.h"
#include "Misc/AutomationTest.h"

namespace
{
	struct FCallState { bool bDone = false; FDeepLevelPCGAutomationResult Result; };
	class FCheckCompletion final : public IAutomationLatentCommand
	{
	public:
		FCheckCompletion(FAutomationTestBase& InTest, TSharedRef<FCallState> InState, bool bExpected)
			: Test(InTest), State(InState), bExpectedSuccess(bExpected), Deadline(FPlatformTime::Seconds()+5) {}
		virtual bool Update() override
		{
			if (!State->bDone && FPlatformTime::Seconds() < Deadline) { return false; }
			Test.TestTrue(TEXT("Asynchronous result completed within bound"), State->bDone);
			Test.TestEqual(TEXT("Terminal event determines success"), State->Result.bSuccess, bExpectedSuccess);
			return true;
		}
	private:
		FAutomationTestBase& Test; TSharedRef<FCallState> State; bool bExpectedSuccess; double Deadline;
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelPCGGenerationTest, "DeepLevelDesignPCG.Editor.Automation.GenerationLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelPCGGenerationTest::RunTest(const FString&)
{
	// Synthetic delegate events exercise lifetime/order/cancellation without running PCG or PIE.
	for (int32 Case = 0; Case < 3; ++Case)
	{
		auto State = MakeShared<FCallState>(); FDeepLevelPCGAutomationPlan Plan;
		Plan.Target.Reset(NewObject<UDeepLevelRoadTileCatalog>());
		auto Component = TStrongObjectPtr<UPCGComponent>(NewObject<UPCGComponent>());
		Plan.Component = Component.Get(); Plan.bRequiresAsync = true; Plan.TimeoutSeconds = Case == 2 ? 0.001 : 5;
		Plan.Generate = [Component, Case]()
		{
			Component->OnPCGGraphStartGeneratingDelegate.Broadcast(Component.Get());
			if (Case == 0) { Component->OnPCGGraphGeneratedDelegate.Broadcast(Component.Get()); }
			if (Case == 1) { Component->OnPCGGraphCancelledDelegate.Broadcast(Component.Get()); }
		};
		Plan.Finish = [Component, this](FDeepLevelPCGAutomationResult&)
		{
			TestFalse(TEXT("Generation listeners detached before finalization"), Component->OnPCGGraphGeneratedDelegate.IsBound());
			TestFalse(TEXT("Cancellation listeners detached"), Component->OnPCGGraphCancelledDelegate.IsBound());
		};
		DeepLevelPCGGeneration::Run(MoveTemp(Plan), FDeepLevelPCGAutomationResult(), [State](FDeepLevelPCGAutomationResult R) { State->Result = MoveTemp(R); State->bDone = true; });
		TestFalse(TEXT("Completion is deferred beyond owner callbacks"), State->bDone);
		ADD_LATENT_AUTOMATION_COMMAND(FCheckCompletion(*this, State, Case == 0));
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelPCGGenerationShutdownTest, "DeepLevelDesignPCG.Editor.Automation.GenerationShutdown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelPCGGenerationShutdownTest::RunTest(const FString&)
{
	FDeepLevelPCGAutomationPlan Plan; Plan.Target.Reset(NewObject<UDeepLevelRoadTileCatalog>());
	auto Component = TStrongObjectPtr<UPCGComponent>(NewObject<UPCGComponent>());
	Plan.Component = Component.Get(); Plan.bRequiresAsync = true;
	Plan.Generate = [Component]() { Component->OnPCGGraphStartGeneratingDelegate.Broadcast(Component.Get()); };
	bool bDone = false, bSucceeded = true;
	DeepLevelPCGGeneration::Run(MoveTemp(Plan), FDeepLevelPCGAutomationResult(), [&](FDeepLevelPCGAutomationResult R) { bDone = true; bSucceeded = R.bSuccess; });
	TestFalse(TEXT("Call waits for a terminal event"), bDone);
	DeepLevelPCGGeneration::Shutdown();
	TestTrue(TEXT("Module shutdown resolves pending call"), bDone); TestFalse(TEXT("Shutdown does not pretend to generate output"), bSucceeded);
	TestFalse(TEXT("Shutdown detaches generated callback"), Component->OnPCGGraphGeneratedDelegate.IsBound());
	TestFalse(TEXT("Shutdown detaches cancelled callback"), Component->OnPCGGraphCancelledDelegate.IsBound());
	return true;
}
#endif
