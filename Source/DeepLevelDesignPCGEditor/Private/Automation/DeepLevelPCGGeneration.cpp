// Copyright <--\, Inc. All Rights Reserved.
#include "Automation/DeepLevelPCGGeneration.h"
#include "PCGComponent.h"
#include "Containers/Ticker.h"
#include "Editor.h"

namespace
{
	class FGenerationCall;
	TArray<TSharedPtr<FGenerationCall>> PendingCalls;
	class FGenerationCall final : public TSharedFromThis<FGenerationCall>
	{
	public:
		FDeepLevelPCGAutomationPlan Plan;
		FDeepLevelPCGAutomationResult Result;
		TFunction<void(FDeepLevelPCGAutomationResult)> Completed;

		void Begin()
		{
			EditorWorld = GEditor->GetEditorWorldContext().World();
			if (auto* Component = Plan.Component.Get(); Component && Plan.bRequiresAsync)
			{
				StartedHandle = Component->OnPCGGraphStartGeneratingDelegate.AddSP(AsShared(), &FGenerationCall::Started);
				GeneratedHandle = Component->OnPCGGraphGeneratedDelegate.AddSP(AsShared(), &FGenerationCall::Generated);
				CancelledHandle = Component->OnPCGGraphCancelledDelegate.AddSP(AsShared(), &FGenerationCall::Cancelled);
			}
			Result.bSuccess = true;
			if (Plan.Apply) { Plan.Apply(Result); }
			if (Result.bSuccess && Plan.Generate) { Plan.Generate(); }
			if (!Plan.bRequiresAsync || (!bStarted && !Plan.Component->IsGenerating()))
			{
				if (Plan.Generate && !bStarted) { Fail(TEXT("PCG generation did not start; inspect generationError/validation.")); }
				Finish(); return;
			}
			Deadline = FPlatformTime::Seconds() + Plan.TimeoutSeconds;
			PendingCalls.Add(AsShared());
			TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self = AsShared()](float)
			{
				// Completion is deferred until all owner delegates have published/verified their output.
				if (!IsValid(Self->Plan.Target.Get()) || !GEditor || GEditor->PlayWorld
					|| Self->EditorWorld.Get() != GEditor->GetEditorWorldContext().World())
				{
					Self->Abort(TEXT("Editor target/world changed or PIE started during generation.")); return false;
				}
				if (Self->bTerminal) { Self->Finish(); return false; }
				if (FPlatformTime::Seconds() < Self->Deadline) { return true; }
				Self->Fail(TEXT("PCG generation timed out; no capture or save performed."));
				if (Self->Plan.Component.IsValid() && Self->Plan.Component->IsGenerating()) { Self->Plan.Component->CancelGeneration(); }
				Self->Finish(); return false;
			}));
		}
		void Abort(const FString& Reason)
		{
			Fail(Reason); Detach();
			FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
			if (Plan.Component.IsValid() && Plan.Component->IsGenerating()) { Plan.Component->CancelGeneration(); }
			PendingCalls.Remove(AsShared());
			Completed(MoveTemp(Result));
		}
	private:
		void Started(UPCGComponent*) { bStarted = true; }
		void Generated(UPCGComponent*) { bTerminal = true; Result.Report->SetBoolField(TEXT("generated"), true); }
		void Cancelled(UPCGComponent*) { bTerminal = true; Fail(TEXT("PCG generation was cancelled.")); }
		void Fail(const FString& Error) { Result.bSuccess = false; Result.Report->SetStringField(TEXT("generationError"), Error); Result.Report->SetBoolField(TEXT("generated"), false); }
		void Detach()
		{
			if (auto* Component = Plan.Component.Get())
			{
				Component->OnPCGGraphStartGeneratingDelegate.Remove(StartedHandle);
				Component->OnPCGGraphGeneratedDelegate.Remove(GeneratedHandle);
				Component->OnPCGGraphCancelledDelegate.Remove(CancelledHandle);
			}
		}
		void Finish()
		{
			const auto Lifetime = AsShared();
			Detach();
			FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
			PendingCalls.Remove(Lifetime);
			if (Plan.Finish) { Plan.Finish(Result); }
			Completed(MoveTemp(Result));
		}
		FDelegateHandle StartedHandle, GeneratedHandle, CancelledHandle;
		FTSTicker::FDelegateHandle TickerHandle;
		TWeakObjectPtr<UWorld> EditorWorld;
		double Deadline = 0;
		bool bStarted = false, bTerminal = false;
	};
}
void DeepLevelPCGGeneration::Run(FDeepLevelPCGAutomationPlan&& Plan, FDeepLevelPCGAutomationResult&& Result,
	TFunction<void(FDeepLevelPCGAutomationResult)>&& Completed)
{
	auto Call = MakeShared<FGenerationCall>();
	Call->Plan = MoveTemp(Plan); Call->Result = MoveTemp(Result); Call->Completed = MoveTemp(Completed); Call->Begin();
}
void DeepLevelPCGGeneration::Shutdown()
{
	const auto Calls = PendingCalls;
	for (const auto& Call : Calls) { Call->Abort(TEXT("PCG automation module is shutting down; no save or capture performed.")); }
}
