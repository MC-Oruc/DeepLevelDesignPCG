// Copyright <--\, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Road/DeepLevelRoadEditor.h"

#include "Road/DeepLevelRoadPCG.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

#if WITH_EDITOR
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#endif

namespace DeepLevelRoadSplineAuthoringTests
{
	class FWorldScope
	{
	public:
		explicit FWorldScope(FAutomationTestBase& Test)
		{
			if (!GEngine)
			{
				Test.AddError(TEXT("GEngine is unavailable."));
				return;
			}
			GameInstance.Reset(NewObject<UGameInstance>(GEngine));
			if (GameInstance.IsValid())
			{
				GameInstance->InitializeStandalone(TEXT("RoadSplineAuthoringWorld"));
			}
		}

		~FWorldScope()
		{
			if (!GameInstance.IsValid())
			{
				return;
			}
			UWorld* World = GameInstance->GetWorld();
			GameInstance->Shutdown();
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}

		UWorld* GetWorld() const
		{
			return GameInstance.IsValid() ? GameInstance->GetWorld() : nullptr;
		}

	private:
		TStrongObjectPtr<UGameInstance> GameInstance;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelRoadSplineAuthoringTest,
	"DeepLevelDesignPCG.Editor.RoadNetwork.SplineAuthoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelRoadSplineAuthoringTest::RunTest(const FString& Parameters)
{
	DeepLevelRoadSplineAuthoringTests::FWorldScope WorldScope(*this);
	UWorld* World = WorldScope.GetWorld();
	if (!TestNotNull(TEXT("Automation world exists"), World))
	{
		return false;
	}

	ADeepLevelRoadNetworkActor* Network = World->SpawnActor<ADeepLevelRoadNetworkActor>();
	if (!TestNotNull(TEXT("Road Network actor spawned"), Network))
	{
		return false;
	}

	int32 StructureNotifications = 0;
	int32 GeometryNotifications = 0;
	const FDelegateHandle ChangeHandle = ADeepLevelRoadNetworkActor::OnRoadNetworkEditorChanged.AddLambda(
		[Network, &StructureNotifications, &GeometryNotifications](
			ADeepLevelRoadNetworkActor& ChangedNetwork,
			const EDeepLevelRoadNetworkChange Change)
		{
			if (&ChangedNetwork != Network)
			{
				return;
			}
			if (Change == EDeepLevelRoadNetworkChange::Structure)
			{
				++StructureNotifications;
			}
			else if (Change == EDeepLevelRoadNetworkChange::Geometry)
			{
				++GeometryNotifications;
			}
		});

	TSharedPtr<FComponentVisualizer> RegisteredVisualizer;
	TSharedPtr<FComponentVisualizer> RootVisualizer;
	if (GUnrealEd)
	{
		RegisteredVisualizer = GUnrealEd->FindComponentVisualizer(UDeepLevelRoadSplineComponent::StaticClass());
		RootVisualizer = GUnrealEd->FindComponentVisualizer(UDeepLevelRoadNetworkRootComponent::StaticClass());
	}
	TestTrue(TEXT("Custom visualizer is returned by the editor visualizer map"), RegisteredVisualizer.IsValid());
	TestTrue(TEXT("Road Network root keeps grid authoring visible before the first spline"), RootVisualizer.IsValid());
	TestTrue(TEXT("Root and spline authoring share one visualizer lifecycle"), RootVisualizer == RegisteredVisualizer);
	TestEqual(
		TEXT("Cursor positions snap to the Road Network grid"),
		FDeepLevelRoadSplineComponentVisualizer::SnapWorldToGrid(
			FVector(740.0, -260.0, 900.0),
			FVector(0.0, 0.0, 125.0),
			500.0),
		FVector(500.0, -500.0, 125.0));
	TestEqual(
		TEXT("Moving the Road Network actor offsets the authoring grid"),
		FDeepLevelRoadSplineComponentVisualizer::SnapWorldToGrid(
			FVector(740.0, -260.0, 900.0),
			FVector(125.0, 250.0, 875.0),
			500.0),
		FVector(625.0, -250.0, 875.0));
	TArray<FVector> SimplifiedPath;
	FDeepLevelRoadSplineComponentVisualizer::SimplifyGridPath(
		{
			FVector(0.0, 0.0, 125.0),
			FVector(500.0, 0.0, 125.0),
			FVector(1000.0, 0.0, 125.0),
			FVector(1000.0, 500.0, 125.0),
			FVector(1000.0, 1000.0, 125.0)
		},
		SimplifiedPath);
	TestEqual(TEXT("Grid tracing keeps only endpoints and direction changes"), SimplifiedPath.Num(), 3);
	if (SimplifiedPath.Num() == 3)
	{
		TestEqual(TEXT("Grid tracing preserves the start"), SimplifiedPath[0], FVector(0.0, 0.0, 125.0));
		TestEqual(TEXT("Grid tracing creates the corner"), SimplifiedPath[1], FVector(1000.0, 0.0, 125.0));
		TestEqual(TEXT("Grid tracing preserves the end"), SimplifiedPath[2], FVector(1000.0, 1000.0, 125.0));
	}

	TArray<FVector> ReshapedPath;
	FDeepLevelRoadSplineComponentVisualizer::ComposeReshapedGridPath(
		{
			FVector(0.0, 0.0, 125.0),
			FVector(500.0, 0.0, 125.0),
			FVector(1000.0, 0.0, 125.0),
			FVector(1500.0, 0.0, 125.0)
		},
		{
			FVector(1500.0, 0.0, 125.0),
			FVector(1000.0, 0.0, 125.0),
			FVector(500.0, 0.0, 125.0)
		},
		ReshapedPath);
	TestEqual(TEXT("Dragging an endpoint backward shortens its existing route"), ReshapedPath.Num(), 2);
	if (ReshapedPath.Num() == 2)
	{
		TestEqual(TEXT("Shortening preserves the fixed endpoint"), ReshapedPath[0], FVector(0.0, 0.0, 125.0));
		TestEqual(TEXT("Shortening replaces the dragged endpoint"), ReshapedPath[1], FVector(500.0, 0.0, 125.0));
	}

	FDeepLevelRoadSplineComponentVisualizer::ComposeReshapedGridPath(
		{
			FVector(0.0, 0.0, 125.0),
			FVector(500.0, 0.0, 125.0),
			FVector(500.0, 500.0, 125.0),
			FVector(1000.0, 500.0, 125.0)
		},
		{
			FVector(1000.0, 500.0, 125.0),
			FVector(500.0, 500.0, 125.0),
			FVector(500.0, 0.0, 125.0),
			FVector(500.0, -500.0, 125.0)
		},
		ReshapedPath);
	TestEqual(TEXT("Backtracking across corners and redirecting creates one clean route"), ReshapedPath.Num(), 3);
	if (ReshapedPath.Num() == 3)
	{
		TestEqual(TEXT("Redirect keeps the untouched route"), ReshapedPath[0], FVector(0.0, 0.0, 125.0));
		TestEqual(TEXT("Redirect keeps the last uncancelled cell"), ReshapedPath[1], FVector(500.0, 0.0, 125.0));
		TestEqual(TEXT("Redirect appends the new direction"), ReshapedPath[2], FVector(500.0, -500.0, 125.0));
	}

	FText Error;
	UDeepLevelRoadSplineComponent* NewSpline = FDeepLevelRoadSplineAuthoringService::AddBranch(*Network, Error);
	if (!TestNotNull(TEXT("Subobject authoring creates a Road Branch"), NewSpline))
	{
		AddError(Error.ToString());
		ADeepLevelRoadNetworkActor::OnRoadNetworkEditorChanged.Remove(ChangeHandle);
		return false;
	}

	TArray<UDeepLevelRoadSplineComponent*> Splines;
	Network->GetRoadSplineComponents(Splines);
	TestEqual(TEXT("Road Network owns one authored spline branch"), Splines.Num(), 1);
	TestEqual(TEXT("New branch uses the official instance-component lifecycle"), NewSpline->CreationMethod, EComponentCreationMethod::Instance);
	TestTrue(TEXT("New branch is registered"), NewSpline->IsRegistered());
	TestEqual(TEXT("New branch is attached to Road Lines"), NewSpline->GetAttachParent(), Network->RoadLines.Get());
	TestEqual(TEXT("New branch has the first stable sequential name"), NewSpline->GetFName(), FName(TEXT("RoadLine1")));
	TestEqual(TEXT("New branch uses the longer authoring default"), NewSpline->GetSplineLength(), 2000.0f);
	TestTrue(TEXT("New branch has a persistent component name"), !NewSpline->GetFName().IsNone());
	TestEqual(
		TEXT("The first grid cell is classified as the start endpoint"),
		FDeepLevelRoadSplineComponentVisualizer::FindSplineEndpointAtGridCell(
			*NewSpline,
			FVector::ZeroVector,
			FVector::ZeroVector,
			500.0),
		0);
	TestEqual(
		TEXT("An interior segment grid cell is not classified as an endpoint"),
		FDeepLevelRoadSplineComponentVisualizer::FindSplineEndpointAtGridCell(
			*NewSpline,
			FVector(500.0, 0.0, 0.0),
			FVector::ZeroVector,
			500.0),
		INDEX_NONE);
	TestEqual(
		TEXT("The final grid cell is classified as the end endpoint"),
		FDeepLevelRoadSplineComponentVisualizer::FindSplineEndpointAtGridCell(
			*NewSpline,
			FVector(2000.0, 0.0, 0.0),
			FVector::ZeroVector,
			500.0),
		1);
	Network->SetActorLocation(FVector(250.0, 250.0, 0.0));
	TestEqual(
		TEXT("A moved Road Network still classifies its spline endpoint"),
		FDeepLevelRoadSplineComponentVisualizer::FindSplineEndpointAtGridCell(
			*NewSpline,
			FVector(2250.0, 250.0, 0.0),
			Network->GetGridOrigin(),
			500.0),
		1);
	TestEqual(TEXT("Adding a branch publishes one structural editor refresh"), StructureNotifications, 1);
	Network->NotifyRoadNetworkChanged();
	TestEqual(TEXT("Geometry edits publish an editor refresh"), GeometryNotifications, 1);
	ADeepLevelRoadNetworkActor::OnRoadNetworkEditorChanged.Remove(ChangeHandle);
	return true;
}

#endif
