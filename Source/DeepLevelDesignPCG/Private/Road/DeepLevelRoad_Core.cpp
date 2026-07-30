// Copyright <--\, Inc. All Rights Reserved.

#include "Road/DeepLevelRoadPCG.h"
#include "DeepLevelDesignPCGModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DeepLevelRoadPCG)

// ---- DeepLevelRoadNetworkActor ----


#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGManagedResource.h"
#include "Components/BillboardComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_EDITOR
FDeepLevelRoadNetworkEditorChanged ADeepLevelRoadNetworkActor::OnRoadNetworkEditorChanged;
#endif

ADeepLevelRoadNetworkActor::ADeepLevelRoadNetworkActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RoadNetworkRoot = CreateDefaultSubobject<UDeepLevelRoadNetworkRootComponent>(TEXT("RoadNetworkRoot"));
	RoadNetworkRoot->SetBoxExtent(FVector(50.0, 50.0, 1.0));
	RoadNetworkRoot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RoadNetworkRoot->SetHiddenInGame(true);
	RoadNetworkRoot->SetCanEverAffectNavigation(false);
	SetRootComponent(RoadNetworkRoot);

#if WITH_EDITORONLY_DATA
	EditorIcon = CreateDefaultSubobject<UBillboardComponent>(TEXT("EditorIcon"));
	EditorIcon->SetupAttachment(RoadNetworkRoot);
	EditorIcon->SetRelativeLocation(FVector(0.0, 0.0, 250.0));
	EditorIcon->SetIsVisualizationComponent(true);
	EditorIcon->SetHiddenInGame(true);
	EditorIcon->SetRelativeScale3D(FVector(4.0));
	EditorIcon->bIsScreenSizeScaled = true;
	EditorIcon->ScreenSize = 0.004f;
	EditorIcon->bUseInEditorScaling = true;
	EditorIcon->SpriteInfo.Category = TEXT("Procedural");
	EditorIcon->SpriteInfo.DisplayName = NSLOCTEXT("SpriteCategory", "Procedural", "Procedural");
	static ConstructorHelpers::FObjectFinderOptional<UTexture2D> IconTexture(
		TEXT("/DeepLevelDesignPCG/Editor/Icons/T_RoadNetwork.T_RoadNetwork"));
	EditorIcon->SetSprite(IconTexture.Get());
#endif

	RoadLines = CreateDefaultSubobject<USceneComponent>(TEXT("RoadLines"));
	RoadLines->SetupAttachment(RoadNetworkRoot);

	GeneratedRoadMeshes = CreateDefaultSubobject<USceneComponent>(TEXT("GeneratedRoadMeshes"));
	GeneratedRoadMeshes->SetupAttachment(RoadNetworkRoot);

	PCGComponent = CreateDefaultSubobject<UPCGComponent>(TEXT("PCGComponent"));
	PCGComponent->OnPCGGraphGeneratedDelegate.AddUObject(this, &ADeepLevelRoadNetworkActor::OrganizeGeneratedRoadMeshes);

	static ConstructorHelpers::FObjectFinderOptional<UPCGGraphInterface> DefaultGraph(
		TEXT("/DeepLevelDesignPCG/Road/PCG_DeepLevelRoadNetwork.PCG_DeepLevelRoadNetwork"));
	if (DefaultGraph.Succeeded())
	{
		if (UPCGGraphInstance* GraphInstance = PCGComponent->GetGraphInstance())
		{
			GraphInstance->SetGraph(DefaultGraph.Get());
		}
	}
}

void ADeepLevelRoadNetworkActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	TArray<UDeepLevelRoadSplineComponent*> Splines;
	GetRoadSplineComponents(Splines);
	for (UDeepLevelRoadSplineComponent* Spline : Splines)
	{
		Spline->NormalizePivotToFirstPoint();
	}
}

UDeepLevelRoadSplineComponent* ADeepLevelRoadNetworkActor::CreateRoadBranch()
{
	check(RoadLines);

	int32 BranchNumber = 1;
	FName BranchName;
	do
	{
		BranchName = *FString::Printf(TEXT("RoadLine%d"), BranchNumber++);
	}
	while (FindObjectFast<UObject>(this, BranchName));

	UDeepLevelRoadSplineComponent* NewSpline = NewObject<UDeepLevelRoadSplineComponent>(this, BranchName, RF_Transactional);
	AddInstanceComponent(NewSpline);
	NewSpline->OnComponentCreated();
	NewSpline->SetupAttachment(RoadLines);
	if (RoadNetworkRoot->IsRegistered())
	{
		NewSpline->RegisterComponent();
	}
	return NewSpline;
}

void ADeepLevelRoadNetworkActor::GetRoadSplineComponents(TArray<UDeepLevelRoadSplineComponent*>& OutSplines) const
{
	OutSplines.Reset();
	TInlineComponentArray<UDeepLevelRoadSplineComponent*> AllSplines(this);
	for (UDeepLevelRoadSplineComponent* Spline : AllSplines)
	{
		if (Spline && Spline->GetAttachParent() == RoadLines)
		{
			OutSplines.Add(Spline);
		}
	}
	OutSplines.Sort([](const UDeepLevelRoadSplineComponent& A, const UDeepLevelRoadSplineComponent& B)
	{
		return A.GetPathName() < B.GetPathName();
	});
}

FVector ADeepLevelRoadNetworkActor::GetGridOrigin() const
{
	return GetActorLocation();
}

void ADeepLevelRoadNetworkActor::OrganizeGeneratedRoadMeshes(UPCGComponent* GeneratedComponent)
{
	if (GeneratedComponent != PCGComponent || !GeneratedRoadMeshes)
	{
		return;
	}

	TArray<UInstancedStaticMeshComponent*> GeneratedISMs;
	PCGComponent->ForEachConstManagedResource([&GeneratedISMs](const UPCGManagedResource* Resource)
	{
		const UPCGManagedISMComponent* ManagedISM = Cast<UPCGManagedISMComponent>(Resource);
		if (UInstancedStaticMeshComponent* ISMComponent = ManagedISM ? ManagedISM->GetComponent() : nullptr)
		{
			GeneratedISMs.Add(ISMComponent);
		}
	});

	for (UInstancedStaticMeshComponent* ISMComponent : GeneratedISMs)
	{
		if (ISMComponent && ISMComponent->GetAttachParent() != GeneratedRoadMeshes)
		{
			ISMComponent->AttachToComponent(GeneratedRoadMeshes, FAttachmentTransformRules::KeepWorldTransform);
		}
	}

	NotifyRoadNetworkChanged(EDeepLevelRoadNetworkChange::GeneratedComponents);
}

void ADeepLevelRoadNetworkActor::NotifyRoadNetworkChanged(const EDeepLevelRoadNetworkChange Change)
{
	if (Change != EDeepLevelRoadNetworkChange::GeneratedComponents && PCGComponent)
	{
		PCGComponent->NotifyPropertiesChangedFromBlueprint();

#if WITH_EDITOR
		GenerateInitialRoadNetwork();
#endif
	}

#if WITH_EDITOR
	OnRoadNetworkEditorChanged.Broadcast(*this, Change);
#endif
}

#if WITH_EDITOR
void ADeepLevelRoadNetworkActor::GenerateInitialRoadNetwork()
{
	if (!PCGComponent || !PCGComponent->GetGraph() || PCGComponent->bGenerated || PCGComponent->IsGenerating())
	{
		return;
	}

	TArray<UDeepLevelRoadSplineComponent*> Splines;
	GetRoadSplineComponents(Splines);
	const bool bHasValidRoad = Splines.ContainsByPredicate([](const UDeepLevelRoadSplineComponent* Spline)
	{
		if (!Spline || Spline->GetNumberOfSplinePoints() < 2)
		{
			return false;
		}

		const FVector Start = Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
		for (int32 PointIndex = 1; PointIndex < Spline->GetNumberOfSplinePoints(); ++PointIndex)
		{
			if (!Spline->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World).Equals(Start))
			{
				return true;
			}
		}
		return false;
	});

	if (bHasValidRoad)
	{
		PCGComponent->GenerateLocal(true);
	}
}

void ADeepLevelRoadNetworkActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	NotifyRoadNetworkChanged();
}

void ADeepLevelRoadNetworkActor::PostEditMove(const bool bFinished)
{
	Super::PostEditMove(bFinished);
	if (bFinished)
	{
		NotifyRoadNetworkChanged();
	}
}

void ADeepLevelRoadNetworkActor::PostEditUndo()
{
	Super::PostEditUndo();
	NotifyRoadNetworkChanged(EDeepLevelRoadNetworkChange::Structure);
}
#endif

// ---- DeepLevelRoadSplineComponent ----




UDeepLevelRoadSplineComponent::UDeepLevelRoadSplineComponent()
{
	ClearSplinePoints(false);
	AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::Local, false);
	AddSplinePoint(FVector(2000.0, 0.0, 0.0), ESplineCoordinateSpace::Local, false);
	SetSplinePointType(0, ESplinePointType::Linear, false);
	SetSplinePointType(1, ESplinePointType::Linear, false);
	UpdateSpline();
}

void UDeepLevelRoadSplineComponent::SetRoadPathFromWorldPoints(const TArray<FVector>& WorldPoints)
{
	check(WorldPoints.Num() >= 2);
	SetWorldLocation(WorldPoints[0]);
	const FTransform WorldTransform = GetComponentTransform();

	ClearSplinePoints(false);
	for (const FVector& WorldPoint : WorldPoints)
	{
		AddSplinePoint(WorldTransform.InverseTransformPosition(WorldPoint), ESplineCoordinateSpace::Local, false);
	}
	for (int32 PointIndex = 0; PointIndex < GetNumberOfSplinePoints(); ++PointIndex)
	{
		SetSplinePointType(PointIndex, ESplinePointType::Linear, false);
	}
	UpdateSpline();
#if WITH_EDITOR
	bSplineHasBeenEdited = true;
#endif
}

void UDeepLevelRoadSplineComponent::NormalizePivotToFirstPoint()
{
	if (GetNumberOfSplinePoints() < 2
		|| GetLocationAtSplinePoint(0, ESplineCoordinateSpace::Local).IsNearlyZero(0.01))
	{
		return;
	}

	TArray<FVector> WorldPoints;
	WorldPoints.Reserve(GetNumberOfSplinePoints());
	for (int32 PointIndex = 0; PointIndex < GetNumberOfSplinePoints(); ++PointIndex)
	{
		WorldPoints.Add(GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World));
	}
	SetRoadPathFromWorldPoints(WorldPoints);
}

#if WITH_EDITOR
namespace
{
	void NotifyRoadSplineChanged(const UDeepLevelRoadSplineComponent& Component)
	{
		if (ADeepLevelRoadNetworkActor* Owner = Cast<ADeepLevelRoadNetworkActor>(Component.GetOwner()))
		{
			Owner->NotifyRoadNetworkChanged();
		}
	}
}

TArray<ESplinePointType::Type> UDeepLevelRoadSplineComponent::GetEnabledSplinePointTypes() const
{
	return {ESplinePointType::Linear};
}

void UDeepLevelRoadSplineComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	NotifyRoadSplineChanged(*this);
}

void UDeepLevelRoadSplineComponent::PostEditComponentMove(const bool bFinished)
{
	Super::PostEditComponentMove(bFinished);
	if (bFinished)
	{
		NotifyRoadSplineChanged(*this);
	}
}

void UDeepLevelRoadSplineComponent::PostEditUndo()
{
	Super::PostEditUndo();
	NotifyRoadSplineChanged(*this);
}

void UDeepLevelRoadSplineComponent::OnComponentDestroyed(const bool bDestroyingHierarchy)
{
	ADeepLevelRoadNetworkActor* Network = Cast<ADeepLevelRoadNetworkActor>(GetOwner());
	Super::OnComponentDestroyed(bDestroyingHierarchy);
	if (Network && !Network->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) && !Network->IsUnreachable())
	{
		Network->NotifyRoadNetworkChanged(EDeepLevelRoadNetworkChange::Structure);
	}
}
#endif

// ---- DeepLevelRoadNetworkPCGSettings ----


#include "PCGContext.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSplineData.h"
#include "Metadata/PCGMetadata.h"

#define LOCTEXT_NAMESPACE "DeepLevelRoadNetworkPCGSettings"

namespace DeepLevelRoadNetworkPCG
{
	class FElement final : public IPCGElement
	{
	protected:
		virtual bool ExecuteInternal(FPCGContext* Context) const override;
		virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
		virtual bool IsCacheable(const UPCGSettings* Settings) const override { return false; }
	};

	void ReportGenerationError(const FText& Message, const FPCGContext* Context)
	{
		PCGLog::LogErrorOnGraph(Message, Context);
#if WITH_EDITOR
		FDeepLevelDesignPCGEditorEvents::OnGenerationFailed().Broadcast(
			LOCTEXT("RoadNetworkSystemName", "Road Network"),
			Message);
#endif
	}
}

#if WITH_EDITOR
FName UDeepLevelRoadNetworkPCGSettings::GetDefaultNodeName() const
{
	return TEXT("DeepLevelRoadNetwork");
}

FText UDeepLevelRoadNetworkPCGSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "DeepLevel Road Network");
}

FText UDeepLevelRoadNetworkPCGSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Builds one deterministic, grid-snapped road and sidewalk network from its owning Road Network actor.");
}
#endif

TArray<FPCGPinProperties> UDeepLevelRoadNetworkPCGSettings::InputPinProperties() const
{
	return {};
}

TArray<FPCGPinProperties> UDeepLevelRoadNetworkPCGSettings::OutputPinProperties() const
{
	return DefaultPointOutputPinProperties();
}

FPCGElementPtr UDeepLevelRoadNetworkPCGSettings::CreateElement() const
{
	return MakeShared<DeepLevelRoadNetworkPCG::FElement>();
}

bool DeepLevelRoadNetworkPCG::FElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UDeepLevelRoadNetworkPCGSettings* Settings = Context->GetInputSettings<UDeepLevelRoadNetworkPCGSettings>();
	check(Settings);

	UPCGComponent* SourceComponent = Cast<UPCGComponent>(Context->ExecutionSource.Get());
	UPCGComponent* OriginalComponent = SourceComponent ? SourceComponent->GetOriginalComponent() : nullptr;
	const ADeepLevelRoadNetworkActor* Network = OriginalComponent
		? Cast<ADeepLevelRoadNetworkActor>(OriginalComponent->GetOwner())
		: nullptr;
	if (!Network)
	{
		ReportGenerationError(
			LOCTEXT("InvalidOwner", "DeepLevel Road Network node must run on a DeepLevel Road Network actor."),
			Context);
		return true;
	}

	UDeepLevelRoadTileCatalog* Catalog = Network->Catalog.LoadSynchronous();
	if (!Catalog)
	{
		ReportGenerationError(
			LOCTEXT("MissingCatalog", "Road Tile Catalog is missing! Please select your Road Network Actor in the level and assign a Catalog in its Details panel."),
			Context);
		return true;
	}
	if (!FMath::IsFinite(Network->GridSize) || Network->GridSize <= UE_DOUBLE_SMALL_NUMBER)
	{
		ReportGenerationError(LOCTEXT("InvalidAuthoringGrid", "Road Network Grid Size must be greater than zero."), Context);
		return true;
	}
	if (!FMath::IsNearlyEqual(Network->GridSize, Catalog->GridCellSize, 0.01))
	{
		ReportGenerationError(
			FText::Format(
				LOCTEXT("GridSizeMismatch", "Road Network Grid Size ({0}) must match its Road Tile Catalog grid size ({1})."),
				FText::AsNumber(Network->GridSize),
				FText::AsNumber(Catalog->GridCellSize)),
			Context);
		return true;
	}

	TArray<UDeepLevelRoadSplineComponent*> SplineComponents;
	Network->GetRoadSplineComponents(SplineComponents);
	if (SplineComponents.IsEmpty())
	{
		return true;
	}
	TArray<const UPCGSplineData*> Splines;
	Splines.Reserve(SplineComponents.Num());
	for (UDeepLevelRoadSplineComponent* SplineComponent : SplineComponents)
	{
		UPCGSplineData* SplineData = FPCGContext::NewObject_AnyThread<UPCGSplineData>(Context);
		SplineData->Initialize(SplineComponent);
		Splines.Add(SplineData);
	}

	FDeepLevelRoadNetworkPlan Plan;
	FText Error;
	if (!FDeepLevelRoadNetworkPlanner::BuildPlan(
		*Catalog,
		Splines,
		Network->GetGridOrigin(),
		Settings->RandomSeed,
		Plan,
		Error))
	{
		ReportGenerationError(Error, Context);
		return true;
	}

	UPCGPointData* OutputData = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
	OutputData->InitializeFromData(Splines[0]);
	UPCGMetadata* Metadata = OutputData->MutableMetadata();
	FPCGMetadataAttribute<FSoftObjectPath>* MeshAttribute = Metadata->CreateAttribute<FSoftObjectPath>(
		Settings->MeshAttribute, FSoftObjectPath(), false, false);
	FPCGMetadataAttribute<FSoftObjectPath>* MaterialOverrideAttribute = Metadata->CreateAttribute<FSoftObjectPath>(
		Settings->MaterialOverrideAttribute, FSoftObjectPath(), false, false);
	FPCGMetadataAttribute<int32>* KindAttribute = Metadata->CreateAttribute<int32>(
		Settings->TileKindAttribute, 0, false, false);
	FPCGMetadataAttribute<int32>* ConnectionsAttribute = Metadata->CreateAttribute<int32>(
		Settings->ConnectionMaskAttribute, 0, false, false);
	if (!MeshAttribute || !MaterialOverrideAttribute || !KindAttribute || !ConnectionsAttribute)
	{
		ReportGenerationError(LOCTEXT("MetadataFailure", "DeepLevel Road Network could not create its output attributes."), Context);
		return true;
	}

	TArray<FPCGPoint>& Points = OutputData->GetMutablePoints();
	Points.Reserve(Plan.Placements.Num());
	for (const FDeepLevelRoadTilePlacement& Placement : Plan.Placements)
	{
		FPCGPoint& Point = Points.Emplace_GetRef();
		Point.Transform = Placement.Transform;
		Point.Density = 1.0f;
		Point.Seed = HashCombineFast(Settings->RandomSeed, HashCombineFast(GetTypeHash(Placement.GridCell.X), GetTypeHash(Placement.GridCell.Y)));
		Point.MetadataEntry = Metadata->AddEntry();
		MeshAttribute->SetValue(Point.MetadataEntry, Placement.TileMesh.ToSoftObjectPath());
		MaterialOverrideAttribute->SetValue(Point.MetadataEntry, Placement.TileMaterialOverride.ToSoftObjectPath());
		KindAttribute->SetValue(Point.MetadataEntry, static_cast<int32>(Placement.Kind));
		ConnectionsAttribute->SetValue(Point.MetadataEntry, Placement.ConnectionMask);
	}

	FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
	Output.Pin = PCGPinConstants::DefaultOutputLabel;
	Output.Data = OutputData;
	return true;
}

#undef LOCTEXT_NAMESPACE

// ---- DeepLevelRoadTileCatalog ----


#include "Engine/StaticMesh.h"


#define LOCTEXT_NAMESPACE "DeepLevelRoadTileCatalog"

namespace
{
	constexpr int32 ValidConnectionMask = static_cast<int32>(EDeepLevelRoadConnection::PositiveX)
		| static_cast<int32>(EDeepLevelRoadConnection::PositiveY)
		| static_cast<int32>(EDeepLevelRoadConnection::NegativeX)
		| static_cast<int32>(EDeepLevelRoadConnection::NegativeY);

	bool HasExactlyOneBit(const int32 Mask)
	{
		return Mask > 0 && (Mask & (Mask - 1)) == 0;
	}
}

bool UDeepLevelRoadTileCatalog::ValidateForGeneration(FText& OutError) const
{
	OutError = FText::GetEmpty();
	if (!FMath::IsFinite(GridCellSize) || GridCellSize <= UE_DOUBLE_SMALL_NUMBER)
	{
		OutError = LOCTEXT("InvalidGridSize", "Road Tile Catalog grid cell size must be greater than zero.");
		return false;
	}
	if (SidewalkWidthInTiles < 1)
	{
		OutError = LOCTEXT("InvalidSidewalkWidth", "Road Tile Catalog sidewalk width must be at least one tile.");
		return false;
	}
	if (Tiles.IsEmpty())
	{
		OutError = LOCTEXT("EmptyCatalog", "Road Tile Catalog contains no tiles.");
		return false;
	}

	bool bHasSidewalk = false;
	bool bHasDeadEnd = false;
	bool bHasStraight = false;
	bool bHasCorner = false;
	bool bHasTJunction = false;
	bool bHasFourWay = false;
	bool bHasJunctionApproach = false;
	for (int32 Index = 0; Index < Tiles.Num(); ++Index)
	{
		const FDeepLevelRoadTileDefinition& Tile = Tiles[Index];
		if (Tile.TileMesh.IsNull())
		{
			OutError = FText::Format(LOCTEXT("MissingMesh", "Road Tile Catalog entry {0} has no static mesh."), FText::AsNumber(Index + 1));
			return false;
		}
		if (!Tile.TileMesh.LoadSynchronous())
		{
			OutError = FText::Format(
				LOCTEXT("InvalidStaticMesh", "Road Tile Catalog entry {0} has an invalid static mesh."),
				FText::AsNumber(Index + 1));
			return false;
		}
		if (!Tile.bCalibrated)
		{
			OutError = FText::Format(LOCTEXT("UncalibratedTile", "Road Tile Catalog entry {0} is not calibrated."), FText::AsNumber(Index + 1));
			return false;
		}
		if (!FMath::IsFinite(Tile.SelectionWeight) || Tile.SelectionWeight <= 0.0)
		{
			OutError = FText::Format(LOCTEXT("InvalidWeight", "Road Tile Catalog entry {0} must have a positive selection weight."), FText::AsNumber(Index + 1));
			return false;
		}
		const FVector Extent = Tile.PlacementVolume.Extent;
		if (!FMath::IsFinite(Extent.X)
			|| !FMath::IsFinite(Extent.Y)
			|| !FMath::IsFinite(Extent.Z)
			|| Extent.GetMin() <= UE_DOUBLE_SMALL_NUMBER)
		{
			OutError = FText::Format(LOCTEXT("InvalidVolume", "Road Tile Catalog entry {0} has an invalid placement volume."), FText::AsNumber(Index + 1));
			return false;
		}
		const double TileWidth = Extent.X * 2.0;
		const double TileDepth = Extent.Y * 2.0;
		const double SizeTolerance = FMath::Max(GridCellSize * 0.01, 1.0);
		if (!FMath::IsNearlyEqual(TileWidth, GridCellSize, SizeTolerance)
			|| !FMath::IsNearlyEqual(TileDepth, GridCellSize, SizeTolerance))
		{
			OutError = FText::Format(
				LOCTEXT("TileSizeMismatch", "Road Tile Catalog entry {0} must be a 1x1 tile calibrated to one {1} x {1} grid cell."),
				FText::AsNumber(Index + 1),
				FText::AsNumber(GridCellSize));
			return false;
		}

		if (Tile.GetKind() == EDeepLevelRoadTileKind::Sidewalk)
		{
			if (Tile.ApproachJunctionDirectionMask != 0)
			{
				OutError = FText::Format(
					LOCTEXT("SidewalkHasApproach", "Road Tile Catalog entry {0} is a sidewalk and cannot have a junction approach."),
					FText::AsNumber(Index + 1));
				return false;
			}
			bHasSidewalk = true;
			continue;
		}
		if (Tile.ConnectionMask <= 0 || (Tile.ConnectionMask & ~ValidConnectionMask) != 0)
		{
			OutError = FText::Format(LOCTEXT("InvalidConnections", "Road Tile Catalog entry {0} has invalid road connections."), FText::AsNumber(Index + 1));
			return false;
		}
		if (Tile.IsJunctionApproach()
			&& (!HasExactlyOneBit(Tile.ApproachJunctionDirectionMask)
				|| (Tile.ApproachJunctionDirectionMask & ~ValidConnectionMask) != 0
				|| (Tile.ApproachJunctionDirectionMask & Tile.ConnectionMask) == 0))
		{
			OutError = FText::Format(
				LOCTEXT("InvalidApproachDirection", "Road Tile Catalog entry {0} has an invalid JUNCTION port. Exactly one connected port may be JUNCTION; click a viewport port until only that port is orange."),
				FText::AsNumber(Index + 1));
			return false;
		}
		if (Tile.IsJunctionApproach() && Tile.GetTopology() != EDeepLevelRoadTileTopology::Straight)
		{
			OutError = FText::Format(
				LOCTEXT("ApproachMustBeStraight", "Road Tile Catalog entry {0} is a Junction Approach, but its ROAD ports are not opposite. Configure exactly two opposite ports: one ROAD and one JUNCTION."),
				FText::AsNumber(Index + 1));
			return false;
		}
		if (Tile.IsJunctionApproach())
		{
			bHasJunctionApproach = true;
			continue;
		}
		switch (Tile.GetTopology())
		{
		case EDeepLevelRoadTileTopology::DeadEnd: bHasDeadEnd = true; break;
		case EDeepLevelRoadTileTopology::Straight: bHasStraight = true; break;
		case EDeepLevelRoadTileTopology::Corner: bHasCorner = true; break;
		case EDeepLevelRoadTileTopology::TJunction: bHasTJunction = true; break;
		case EDeepLevelRoadTileTopology::FourWay: bHasFourWay = true; break;
		default: break;
		}
	}

	TArray<FText> MissingTypes;
	if (!bHasSidewalk) MissingTypes.Add(LOCTEXT("MissingSidewalkName", "Sidewalk (all ports CLOSED)"));
	if (!bHasDeadEnd) MissingTypes.Add(LOCTEXT("MissingDeadEndName", "Dead End (one ROAD port)"));
	if (!bHasStraight) MissingTypes.Add(LOCTEXT("MissingStraightName", "Straight (two opposite ROAD ports)"));
	if (!bHasCorner) MissingTypes.Add(LOCTEXT("MissingCornerName", "Corner (two adjacent ROAD ports)"));
	if (!bHasTJunction) MissingTypes.Add(LOCTEXT("MissingTJunctionName", "T-Junction (three ROAD ports)"));
	if (!bHasFourWay) MissingTypes.Add(LOCTEXT("MissingFourWayName", "Four-Way Junction (four ROAD ports)"));
	if (!bHasJunctionApproach) MissingTypes.Add(LOCTEXT("MissingApproachName", "Junction Approach (opposite ROAD/JUNCTION ports)"));
	if (!MissingTypes.IsEmpty())
	{
		OutError = FText::Format(
			LOCTEXT("IncompleteCatalog", "Road Tile Catalog is incomplete. Missing: {0}. Configure these canonical layouts once; the planner rotates them automatically."),
			FText::Join(LOCTEXT("MissingTypeSeparator", ", "), MissingTypes));
		return false;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
