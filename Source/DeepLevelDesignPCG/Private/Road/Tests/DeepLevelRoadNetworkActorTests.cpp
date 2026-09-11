// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Road/DeepLevelRoadPCG.h"

#include "PCGComponent.h"
#include "PCGManagedResource.h"
#include "Components/BillboardComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace DeepLevelRoadNetworkActorTests
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
				GameInstance->InitializeStandalone(TEXT("RoadNetworkActorWorld"));
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
	FDeepLevelRoadNetworkActorTest,
	"DeepLevelDesignPCG.Editor.RoadNetwork.ActorAuthoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelRoadNetworkActorTest::RunTest(const FString& Parameters)
{
	DeepLevelRoadNetworkActorTests::FWorldScope WorldScope(*this);
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
	ADeepLevelCityLayoutActor* CityLayout = World->SpawnActor<ADeepLevelCityLayoutActor>();
	CityLayout->GridProfile = NewObject<UDeepLevelCityGridProfile>(CityLayout);
	CityLayout->SetActorLocation(FVector(125.0, 250.0, 875.0));
	Network->CityLayout = CityLayout;

	TArray<UDeepLevelRoadSplineComponent*> Splines;
	Network->GetRoadSplineComponents(Splines);
	TestEqual(TEXT("Road Network resolves the shared 500-unit authoring grid"), Network->GetGridSize(), 500.0);
	TestEqual(TEXT("Road Network resolves the City Layout origin"), Network->GetGridOrigin(), CityLayout->GetActorLocation());
	TestTrue(TEXT("Road Network has a dedicated visualization root"), Network->RoadNetworkRoot->IsA<UDeepLevelRoadNetworkRootComponent>());
	TestEqual(TEXT("Road Network starts without an authored spline branch"), Splines.Num(), 0);
	TestTrue(TEXT("Road Network has a native scene root"), Network->GetRootComponent() == Network->RoadNetworkRoot);
	UBillboardComponent* EditorIcon = Network->FindComponentByClass<UBillboardComponent>();
	TestNotNull(TEXT("Road Network has a visible editor icon"), EditorIcon);
	if (EditorIcon)
	{
		TestTrue(TEXT("Road Network icon is editor-only visualization"), EditorIcon->IsVisualizationComponent());
		TestTrue(TEXT("Road Network icon is easy to identify in the viewport"), EditorIcon->GetRelativeScale3D().X >= 4.0);
	}
	TestTrue(TEXT("Road Lines is attached to the scene root"), Network->RoadLines->GetAttachParent() == Network->RoadNetworkRoot);
	TestTrue(
		TEXT("Generated Road Meshes is attached to the scene root"),
		Network->GeneratedRoadMeshes->GetAttachParent() == Network->RoadNetworkRoot);
	TestEqual(
		TEXT("Road meshes block collision by default"),
		Network->RoadMeshCollisionProfile.Name,
		UCollisionProfile::BlockAll_ProfileName);

	UDeepLevelRoadSplineComponent* FirstSpline = Network->CreateRoadBranch();
	if (!TestNotNull(TEXT("The first authored branch is created"), FirstSpline))
	{
		return false;
	}
	Network->GetRoadSplineComponents(Splines);
	TestEqual(TEXT("The first authored branch is the only spline"), Splines.Num(), 1);
	TestEqual(TEXT("The first authored branch uses instance ownership"), FirstSpline->CreationMethod, EComponentCreationMethod::Instance);
	TestEqual(TEXT("The first authored branch has the first sequential name"), FirstSpline->GetFName(), FName(TEXT("RoadLine1")));

	for (UDeepLevelRoadSplineComponent* Spline : Splines)
	{
		TestTrue(TEXT("Spline branch is registered"), Spline->IsRegistered());
		TestTrue(TEXT("Spline branch belongs to the Road Network actor"), Spline->GetOwner() == Network);
		TestTrue(TEXT("Spline branch is attached to Road Lines"), Spline->GetAttachParent() == Network->RoadLines);
		const TArray<ESplinePointType::Type> EnabledPointTypes = Spline->GetEnabledSplinePointTypes();
		TestEqual(TEXT("Road authoring exposes only one point type"), EnabledPointTypes.Num(), 1);
		if (EnabledPointTypes.Num() == 1)
		{
			TestEqual(TEXT("Road authoring exposes only Linear points"), EnabledPointTypes[0], ESplinePointType::Linear);
		}
		for (int32 PointIndex = 0; PointIndex < Spline->GetNumberOfSplinePoints(); ++PointIndex)
		{
			TestEqual(
				TEXT("Road spline points default to Linear"),
				Spline->GetSplinePointType(PointIndex),
				ESplinePointType::Linear);
		}
		TestEqual(
			TEXT("A new road branch uses the authoring default"),
			Spline->GetSplineLength(),
			2000.0f,
			static_cast<float>(UE_KINDA_SMALL_NUMBER));
	}

	const TArray<FVector> AuthoredWorldPath = {
		FVector(1000.0, 1500.0, 125.0),
		FVector(2000.0, 1500.0, 125.0),
		FVector(2000.0, 2500.0, 125.0)
	};
	FirstSpline->SetRoadPathFromWorldPoints(AuthoredWorldPath);
	TestEqual(TEXT("Road spline pivot follows its first world point"), FirstSpline->GetComponentLocation(), AuthoredWorldPath[0]);
	TestEqual(
		TEXT("Road spline stores its first point at local origin"),
		FirstSpline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::Local),
		FVector::ZeroVector);
	for (int32 PointIndex = 0; PointIndex < AuthoredWorldPath.Num(); ++PointIndex)
	{
		TestEqual(
			TEXT("Pivot normalization preserves authored world geometry"),
			FirstSpline->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World),
			AuthoredWorldPath[PointIndex]);
	}

	const FVector ActorMove(500.0, -500.0, 250.0);
	Network->SetActorLocation(ActorMove);
	TestEqual(
		TEXT("Moving the Road Network moves the normalized branch"),
		FirstSpline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World),
		AuthoredWorldPath[0] + ActorMove);

	UInstancedStaticMeshComponent* GeneratedISM = NewObject<UInstancedStaticMeshComponent>(
		Network,
		TEXT("ISM_AutomationRoadTile"));
	GeneratedISM->RegisterComponent();
	Network->AddInstanceComponent(GeneratedISM);
	GeneratedISM->AttachToComponent(Network->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	UPCGManagedISMComponent* ManagedISM = NewObject<UPCGManagedISMComponent>(Network->PCGComponent);
	ManagedISM->SetComponent(GeneratedISM);
	Network->PCGComponent->AddToManagedResources(ManagedISM);
	Network->PCGComponent->OnPCGGraphGeneratedDelegate.Broadcast(Network->PCGComponent);
	TestEqual(
		TEXT("PCG-managed ISM is organized under Generated Road Meshes"),
		GeneratedISM->GetAttachParent(),
		Network->GeneratedRoadMeshes.Get());
	TestEqual(
		TEXT("PCG-managed ISM uses the Road Network collision profile"),
		GeneratedISM->GetCollisionProfileName(),
		Network->RoadMeshCollisionProfile.Name);

	Network->SetActorLocation(FVector(125.0, 250.0, 875.0));
	TestEqual(TEXT("Road Network movement does not move the shared grid"), Network->GetGridOrigin(), CityLayout->GetActorLocation());

	return true;
}

#endif
