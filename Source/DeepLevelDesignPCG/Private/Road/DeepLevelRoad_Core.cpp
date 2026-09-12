// Copyright <--\, Inc. All Rights Reserved.

#include "Road/DeepLevelRoadPCG.h"
#include "DeepLevelDesignPCGModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DeepLevelRoadPCG)

// ---- DeepLevelRoadNetworkActor ----


#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGManagedResource.h"
#include "Data/PCGSplineData.h"
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

void ADeepLevelRoadNetworkActor::PostLoad()
{
	Super::PostLoad();
	EnsureLayoutSourceGuid();
}

void ADeepLevelRoadNetworkActor::PostActorCreated()
{
	Super::PostActorCreated();
	EnsureLayoutSourceGuid();
}

void ADeepLevelRoadNetworkActor::PostDuplicate(const EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	LayoutSourceGuid = FGuid::NewGuid();
	LayoutRevision = 0;
}

void ADeepLevelRoadNetworkActor::EnsureLayoutSourceGuid()
{
	if (!LayoutSourceGuid.IsValid())
	{
		LayoutSourceGuid = FGuid::NewGuid();
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
	FDeepLevelCityGrid Grid;
	FText Error;
	return ResolveCityGrid(Grid, Error) ? Grid.Origin : FVector::ZeroVector;
}

double ADeepLevelRoadNetworkActor::GetGridSize() const
{
	FDeepLevelCityGrid Grid;
	FText Error;
	return ResolveCityGrid(Grid, Error) ? Grid.TileSize : 0.0;
}

bool ADeepLevelRoadNetworkActor::ResolveCityGrid(FDeepLevelCityGrid& OutGrid, FText& OutError) const
{
	if (!CityLayout)
	{
		OutError = NSLOCTEXT("DeepLevelRoadNetwork", "MissingCityLayout", "Road Network requires a City Layout actor.");
		return false;
	}
	return CityLayout->ResolveGrid(OutGrid, OutError);

}

bool ADeepLevelRoadNetworkActor::BuildCityLayoutFragment(
	const FDeepLevelCityGrid& Grid,
	FDeepLevelCityLayoutFragment& OutFragment,
	FText& OutError) const
{
	OutFragment = {};
	OutError = FText::GetEmpty();

	FDeepLevelCityGrid OwnedGrid;
	if (!ResolveCityGrid(OwnedGrid, OutError))
	{
		return false;
	}
	if (!OwnedGrid.Origin.Equals(Grid.Origin, 0.01)
		|| !FMath::IsNearlyEqual(OwnedGrid.TileSize, Grid.TileSize, 0.01)
		|| OwnedGrid.ChunkSizeInCells != Grid.ChunkSizeInCells
		|| OwnedGrid.ExtentInCells != Grid.ExtentInCells)
	{
		OutError = NSLOCTEXT("DeepLevelRoadNetwork", "ForeignCityGrid", "Road Network cannot build a fragment for a different City Layout grid.");
		return false;
	}

	UDeepLevelRoadTileCatalog* LoadedCatalog = Catalog.LoadSynchronous();
	if (!LoadedCatalog)
	{
		OutError = NSLOCTEXT("DeepLevelRoadNetwork", "MissingFragmentCatalog", "Road Network requires a Road Tile Catalog.");
		return false;
	}

	TArray<UDeepLevelRoadSplineComponent*> SplineComponents;
	GetRoadSplineComponents(SplineComponents);
	OutFragment.SourceGuid = LayoutSourceGuid;
	OutFragment.SourceRevision = LayoutRevision;
	if (SplineComponents.IsEmpty())
	{
		return true;
	}

	TArray<TObjectPtr<UPCGSplineData>> OwnedSplineData;
	TArray<const UPCGSplineData*> Splines;
	OwnedSplineData.Reserve(SplineComponents.Num());
	Splines.Reserve(SplineComponents.Num());
	for (UDeepLevelRoadSplineComponent* SplineComponent : SplineComponents)
	{
		UPCGSplineData* SplineData = NewObject<UPCGSplineData>();
		SplineData->Initialize(SplineComponent);
		OwnedSplineData.Add(SplineData);
		Splines.Add(SplineData);
	}

	FDeepLevelRoadNetworkPlan Plan;
	if (!FDeepLevelRoadNetworkPlanner::BuildPlan(
		*LoadedCatalog, Splines, Grid, 0, Plan, OutError, CellOverrides))
	{
		return false;
	}

	OutFragment.Cells.Reserve(Plan.Placements.Num());
	OutFragment.Anchors.Reserve(Plan.Placements.Num() * 2);
	TMap<FIntPoint, const FDeepLevelRoadTilePlacement*> RoadPlacements;
	TSet<FIntPoint> SidewalkCells;
	TSet<FIntPoint> JunctionCells;
	for (const FDeepLevelRoadTilePlacement& Placement : Plan.Placements)
	{
		if (Placement.Kind == EDeepLevelRoadTileKind::Road)
		{
			RoadPlacements.Add(Placement.GridCell, &Placement);
			if (Placement.bJunctionApproach || FMath::CountBits64(static_cast<uint64>(Placement.ConnectionMask)) > 2)
			{
				JunctionCells.Add(Placement.GridCell);
			}
		}
		else
		{
			SidewalkCells.Add(Placement.GridCell);
		}
	}
	for (const FDeepLevelRoadTilePlacement& Placement : Plan.Placements)
	{
		const EDeepLevelCityOccupancy Occupancy = Placement.Kind == EDeepLevelRoadTileKind::Road
			? EDeepLevelCityOccupancy::Road
			: EDeepLevelCityOccupancy::Sidewalk;
		FDeepLevelCityCellState& Cell = OutFragment.Cells.Emplace_GetRef();
		Cell.Cell = Placement.GridCell;
		Cell.OccupancyMask = static_cast<int32>(Occupancy);
		Cell.Tags.AddTag(Placement.Kind == EDeepLevelRoadTileKind::Road
			? DeepLevelCityTags::Cell_Road
			: DeepLevelCityTags::Cell_Sidewalk);

		FDeepLevelCityAnchor& Anchor = OutFragment.Anchors.Emplace_GetRef();
		const FString LocalKey = FString::Printf(TEXT("Cell:%d:%d"), Placement.GridCell.X, Placement.GridCell.Y);
		const FName Slot = Placement.Kind == EDeepLevelRoadTileKind::Road ? TEXT("RoadSurface") : TEXT("SidewalkSurface");
		Anchor.StableId = FDeepLevelCityStableId::MakeAnchorId(LayoutSourceGuid, LocalKey, Slot);
		Anchor.Geometry = EDeepLevelCityAnchorGeometry::Surface;
		Anchor.Tags.AddTag(Placement.Kind == EDeepLevelRoadTileKind::Road
			? DeepLevelCityTags::Anchor_Road_Surface
			: DeepLevelCityTags::Anchor_Sidewalk_Surface);
		Anchor.Transform = Placement.SurfaceTransform;
		Anchor.Extent = FVector(Grid.TileSize * 0.5, Grid.TileSize * 0.5, 0.0);
		Anchor.OccupiedCells.Add(Placement.GridCell);
		Anchor.SourceRevision = LayoutRevision;

		if (Placement.Kind != EDeepLevelRoadTileKind::Sidewalk)
		{
			continue;
		}
		static const FIntPoint NeighborDirections[] = {
			FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(-1, 0), FIntPoint(0, -1)};
		for (const FIntPoint Direction : NeighborDirections)
		{
			const FDeepLevelRoadTilePlacement* const* RoadPlacement = RoadPlacements.Find(Placement.GridCell + Direction);
			if (!RoadPlacement)
			{
				continue;
			}
			FDeepLevelCityAnchor& Edge = OutFragment.Anchors.Emplace_GetRef();
			const FString EdgeKey = FString::Printf(
				TEXT("Cell:%d:%d:Edge:%d:%d"), Placement.GridCell.X, Placement.GridCell.Y, Direction.X, Direction.Y);
			Edge.StableId = FDeepLevelCityStableId::MakeAnchorId(LayoutSourceGuid, EdgeKey, TEXT("SidewalkEdge"));
			Edge.Geometry = EDeepLevelCityAnchorGeometry::Segment;
			const FVector RoadDirection(Direction.X, Direction.Y, 0.0);
			const FVector Tangent(-RoadDirection.Y, RoadDirection.X, 0.0);
			FVector Location = Grid.CellToWorld(Placement.GridCell)
				+ RoadDirection * (Grid.TileSize * 0.5 - 75.0);
			Location.Z = Placement.SurfaceTransform.GetLocation().Z;
			Edge.Transform = FTransform(FRotationMatrix::MakeFromXZ(Tangent, FVector::UpVector).ToQuat(), Location);
			Edge.Extent = FVector(Grid.TileSize * 0.5, 0.0, 0.0);
			Edge.Tags.AddTag(DeepLevelCityTags::Anchor_Sidewalk_Edge);
			const int32 ConnectionCount = FMath::CountBits64(static_cast<uint64>((*RoadPlacement)->ConnectionMask));
			if (ConnectionCount == 1)
			{
				Edge.Tags.AddTag(DeepLevelCityTags::Anchor_Road_DeadEnd);
			}

			int32 RoadWidthInCells = 0;
			const int32 AlongEdgeMask = Direction.X != 0
				? static_cast<int32>(EDeepLevelRoadConnection::PositiveY) | static_cast<int32>(EDeepLevelRoadConnection::NegativeY)
				: static_cast<int32>(EDeepLevelRoadConnection::PositiveX) | static_cast<int32>(EDeepLevelRoadConnection::NegativeX);
			for (FIntPoint RoadCell = Placement.GridCell + Direction; ; RoadCell += Direction)
			{
				const FDeepLevelRoadTilePlacement* const* CrossSection = RoadPlacements.Find(RoadCell);
				// A bend or end cap points down the road, not across its width.
				if (!CrossSection || ((*CrossSection)->ConnectionMask & AlongEdgeMask) != AlongEdgeMask)
				{
					break;
				}
				++RoadWidthInCells;
			}
			Edge.Tags.AddTag(RoadWidthInCells > 1
				? DeepLevelCityTags::Anchor_Road_Arterial
				: DeepLevelCityTags::Anchor_Road_Local);

			bool bNearJunction = false;
			for (int32 X = -2; X <= 2 && !bNearJunction; ++X)
			{
				for (int32 Y = -2; Y <= 2; ++Y)
				{
					if (FMath::Abs(X) + FMath::Abs(Y) <= 2 && JunctionCells.Contains(Placement.GridCell + FIntPoint(X, Y)))
					{
						bNearJunction = true;
						break;
					}
				}
			}
			if (bNearJunction)
			{
				Edge.Tags.AddTag(DeepLevelCityTags::Anchor_Road_Junction);
			}

			int32 SidewalkDepthInCells = 0;
			for (FIntPoint SidewalkCell = Placement.GridCell; SidewalkCells.Contains(SidewalkCell); SidewalkCell -= Direction)
			{
				++SidewalkDepthInCells;
			}
			Edge.ClearanceDepth = SidewalkDepthInCells * Grid.TileSize;
			Edge.OccupiedCells.Add(Placement.GridCell);
			Edge.SourceRevision = LayoutRevision;
		}
	}
	return true;
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
		if (!ISMComponent)
		{
			continue;
		}

		ISMComponent->SetCollisionProfileName(RoadMeshCollisionProfile.Name);
		ISMComponent->SetCastShadow(false);

		if (ISMComponent->GetAttachParent() != GeneratedRoadMeshes)
		{
			ISMComponent->AttachToComponent(GeneratedRoadMeshes, FAttachmentTransformRules::KeepWorldTransform);
		}
	}

	NotifyRoadNetworkChanged(EDeepLevelRoadNetworkChange::GeneratedComponents);
}

void ADeepLevelRoadNetworkActor::NotifyRoadNetworkChanged(const EDeepLevelRoadNetworkChange Change)
{
	if (Change != EDeepLevelRoadNetworkChange::GeneratedComponents)
	{
		++LayoutRevision;
	}
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
	FDeepLevelCityGrid Grid;
	FText Error;
	if (!Network->ResolveCityGrid(Grid, Error))
	{
		ReportGenerationError(Error, Context);
		return true;
	}
	if (Catalog->GridProfile != Network->CityLayout->GridProfile)
	{
		ReportGenerationError(
			LOCTEXT("GridProfileMismatch", "Road Network and Road Tile Catalog must reference the same City Grid Profile."),
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
	if (!FDeepLevelRoadNetworkPlanner::BuildPlan(
		*Catalog,
		Splines,
		Grid,
		Settings->RandomSeed,
		Plan,
		Error,
		Network->CellOverrides))
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

double UDeepLevelRoadTileCatalog::GetTileSize() const
{
	return GridProfile ? GridProfile->TileSize : 0.0;
}

bool UDeepLevelRoadTileCatalog::ValidateForGeneration(FText& OutError) const
{
	OutError = FText::GetEmpty();
	if (!GridProfile)
	{
		OutError = LOCTEXT("MissingGridProfile", "Road Tile Catalog requires a City Grid Profile.");
		return false;
	}
	if (!GridProfile->Validate(OutError))
	{
		return false;
	}
	const double TileSize = GridProfile->TileSize;
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
		const double SizeTolerance = FMath::Max(TileSize * 0.01, 1.0);
		if (!FMath::IsNearlyEqual(TileWidth, TileSize, SizeTolerance)
			|| !FMath::IsNearlyEqual(TileDepth, TileSize, SizeTolerance))
		{
			OutError = FText::Format(
				LOCTEXT("TileSizeMismatch", "Road Tile Catalog entry {0} must be a 1x1 tile calibrated to one {1} x {1} grid cell."),
				FText::AsNumber(Index + 1),
				FText::AsNumber(TileSize));
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
