// Copyright <--\, Inc. All Rights Reserved.

#include "Building/DeepLevelBuildingLayout.h"
#include "Building/DeepLevelBuildingPlacementClosure.h"
#include "Building/DeepLevelBuildingSidewalkInfill.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "DeepLevelDesignPCGModule.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "PackedLevelActor/PackedLevelActor.h"
#include "PCGComponent.h"
#include "PCGManagedResource.h"
#include "Data/PCGSplineData.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "DeepLevelRoadsideBuilding"
DEFINE_LOG_CATEGORY_STATIC(LogDeepLevelRoadsideBuilding, Log, All);

namespace
{
	bool IsPointInsidePolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon)
	{
		bool bInside = false;
		for (int32 A = 0, B = Polygon.Num() - 1; A < Polygon.Num(); B = A++)
		{
			const FVector2D& PA = Polygon[A];
			const FVector2D& PB = Polygon[B];
			if ((PA.Y > Point.Y) != (PB.Y > Point.Y)
				&& Point.X < (PB.X - PA.X) * (Point.Y - PA.Y) / (PB.Y - PA.Y) + PA.X)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	double DistanceToSegment(const FVector2D& Point, const FVector2D& Start, const FVector2D& End)
	{
		const FVector2D Segment = End - Start;
		const double LengthSquared = Segment.SquaredLength();
		if (LengthSquared <= UE_DOUBLE_SMALL_NUMBER) { return FVector2D::Distance(Point, Start); }
		const double T = FMath::Clamp(FVector2D::DotProduct(Point - Start, Segment) / LengthSquared, 0.0, 1.0);
		return FVector2D::Distance(Point, Start + Segment * T);
	}

	double Cross2D(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	bool RoadsideSegmentsIntersect(
		const FVector2D& A0, const FVector2D& A1, const FVector2D& B0, const FVector2D& B1)
	{
		const FVector2D A = A1 - A0;
		const FVector2D B = B1 - B0;
		const double Denominator = Cross2D(A, B);
		if (FMath::IsNearlyZero(Denominator)) { return false; }
		const FVector2D Offset = B0 - A0;
		const double T = Cross2D(Offset, B) / Denominator;
		const double U = Cross2D(Offset, A) / Denominator;
		return T >= 0.0 && T <= 1.0 && U >= 0.0 && U <= 1.0;
	}

	double DistanceBetweenSegments(
		const FVector2D& A0, const FVector2D& A1, const FVector2D& B0, const FVector2D& B1)
	{
		if (RoadsideSegmentsIntersect(A0, A1, B0, B1)) { return 0.0; }
		return FMath::Min(
			FMath::Min(DistanceToSegment(A0, B0, B1), DistanceToSegment(A1, B0, B1)),
			FMath::Min(DistanceToSegment(B0, A0, A1), DistanceToSegment(B1, A0, A1)));
	}

	bool IsFootprintInsideBlock(
		const DeepLevelBuildingLayoutGeometry::FFootprint& Footprint,
		const TArray<FVector2D>& Polygon,
		const double Margin)
	{
		for (const FVector2D& Corner : Footprint)
		{
			if (!IsPointInsidePolygon(Corner, Polygon)) { return false; }
		}
		for (int32 FootprintEdge = 0; FootprintEdge < Footprint.Num(); ++FootprintEdge)
		{
			const FVector2D& FootprintStart = Footprint[FootprintEdge];
			const FVector2D& FootprintEnd = Footprint[(FootprintEdge + 1) % Footprint.Num()];
			for (int32 BlockEdge = 0; BlockEdge < Polygon.Num(); ++BlockEdge)
			{
				if (DistanceBetweenSegments(
					FootprintStart, FootprintEnd,
					Polygon[BlockEdge], Polygon[(BlockEdge + 1) % Polygon.Num()])
					< FMath::Max(Margin, UE_DOUBLE_KINDA_SMALL_NUMBER))
				{
					return false;
				}
			}
		}
		return true;
	}

	void RedrawRoadsideEditorViewports()
	{
		if (GEngine) { GEngine->RedrawViewports(false); }
	}

	void ReportRoadsideFailure(ADeepLevelPCGRoadsideBuildingActor& Actor, const FText& Error)
	{
		Actor.LastGenerationError = Error;
		UE_LOG(LogDeepLevelRoadsideBuilding, Error, TEXT("%s"), *Error.ToString());
#if WITH_EDITOR
		FDeepLevelDesignPCGEditorEvents::OnGenerationFailed().Broadcast(
			LOCTEXT("RoadsideBuildingSystem", "Roadside Building"), Error);
#endif
		RedrawRoadsideEditorViewports();
	}
}

ADeepLevelPCGRoadsideBuildingActor::ADeepLevelPCGRoadsideBuildingActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	PCGComponent = CreateDefaultSubobject<UPCGComponent>(TEXT("PCGComponent"));
	PCGComponent->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
#if WITH_EDITORONLY_DATA
	PCGComponent->bRegenerateInEditor = false;
#endif
	static ConstructorHelpers::FObjectFinderOptional<UPCGGraphInterface> DefaultGraph(
		TEXT("/DeepLevelDesignPCG/Building/PCG_DeepLevelBuildingLine.PCG_DeepLevelBuildingLine"));
	if (DefaultGraph.Succeeded())
	{
		PCGComponent->GetGraphInstance()->SetGraph(DefaultGraph.Get());
	}
}

void ADeepLevelPCGRoadsideBuildingActor::PostLoad()
{
	Super::PostLoad();
	EnsureLayoutSourceGuid();
}

void ADeepLevelPCGRoadsideBuildingActor::PostActorCreated()
{
	Super::PostActorCreated();
	EnsureLayoutSourceGuid();
}

void ADeepLevelPCGRoadsideBuildingActor::PostDuplicate(const EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	LayoutSourceGuid = FGuid::NewGuid();
	LayoutRevision = 0;
}

void ADeepLevelPCGRoadsideBuildingActor::EnsureLayoutSourceGuid()
{
	if (!LayoutSourceGuid.IsValid()) { LayoutSourceGuid = FGuid::NewGuid(); }
}

void ADeepLevelPCGRoadsideBuildingActor::SynchronizeCityLayoutRegistration()
{
	if (RegisteredCityLayout == CityLayout) { return; }
	if (ADeepLevelCityLayoutActor* Previous = RegisteredCityLayout.Get())
	{
		Previous->UnregisterLayoutSource(*this);
	}
	RegisteredCityLayout = CityLayout;
	if (CityLayout) { CityLayout->RegisterLayoutSource(*this); }
}

bool ADeepLevelPCGRoadsideBuildingActor::BuildCityLayoutFragment(
	const FDeepLevelCityGrid& Grid,
	FDeepLevelCityLayoutFragment& OutFragment,
	FText& OutError) const
{
	(void)Grid;
	OutError = FText::GetEmpty();
	OutFragment = PreparedLayout ? PreparedLayout->Fragment : FDeepLevelCityLayoutFragment{};
	OutFragment.SourceGuid = LayoutSourceGuid;
	OutFragment.SourceRevision = LayoutRevision;
	return true;
}

void ADeepLevelPCGRoadsideBuildingActor::GenerateFrontageSplines()
{
#if WITH_EDITOR
	Modify();
	PreparedLayout.Reset();
	bOutputCurrent = false;
	LastGenerationError = FText::GetEmpty();
	if (!GetWorld() || GetWorld()->IsGameWorld() || !CityLayout)
	{
		ReportRoadsideFailure(*this, LOCTEXT("MissingCityLayout", "Roadside Building requires a City Layout actor in an editor world."));
		return;
	}
	++LayoutRevision;
	SynchronizeCityLayoutRegistration();
	CityLayout->InvalidateSnapshot();
	TSet<FIntPoint> DirtyChunks;
	FText SnapshotError;
	if (!CityLayout->RefreshSnapshot(DirtyChunks, SnapshotError))
	{
		ReportRoadsideFailure(*this, SnapshotError);
		return;
	}
	CityLayout->NotifySidewalkInfillChanged();
	const TSharedPtr<const FDeepLevelCityLayoutSnapshot> Base = CityLayout->GetSnapshot();
	if (!Base)
	{
		ReportRoadsideFailure(*this, LOCTEXT("MissingLayoutData", "Roadside Building could not resolve current City Layout data."));
		return;
	}
	TArray<DeepLevelBuildingRoadside::FFrontageSpline> GeneratedSplines;
	if (!DeepLevelBuildingRoadside::BuildFrontageSplines(
		*Base, GeneratedSplines, LastGenerationError, FrontageSetback))
	{
		ReportRoadsideFailure(*this, LastGenerationError);
		return;
	}
	FrontageSplines.Reset();
	FrontageSplineCount = 0;
	OpenFrontageSplineCount = 0;
	ClosedFrontageSplineCount = 0;
	TSet<FGuid> ReplacedIds;
	TMap<FGuid, UDeepLevelRoadsideFrontageSplineComponent*> AutomaticById;
	TSet<UDeepLevelRoadsideFrontageSplineComponent*> RetainedAutomatic;
	TInlineComponentArray<UDeepLevelRoadsideFrontageSplineComponent*> ExistingFrontages(this);
	for (UDeepLevelRoadsideFrontageSplineComponent* Frontage : ExistingFrontages)
	{
		if (!Frontage) { continue; }
		if (Frontage->Kind == EDeepLevelRoadsideFrontageKind::Automatic)
		{
			if (Frontage->FrontageId.IsValid()) { AutomaticById.Add(Frontage->FrontageId, Frontage); }
			continue;
		}
		FrontageSplines.Add(Frontage);
		if (Frontage->IsClosedLoop())
		{
			++ClosedFrontageSplineCount;
		}
		else
		{
			++OpenFrontageSplineCount;
		}
		if (Frontage->Kind == EDeepLevelRoadsideFrontageKind::Replace && Frontage->ReplacedFrontageId.IsValid())
		{
			ReplacedIds.Add(Frontage->ReplacedFrontageId);
		}
	}

	for (int32 Index = 0; Index < GeneratedSplines.Num(); ++Index)
	{
		const DeepLevelBuildingRoadside::FFrontageSpline& Source = GeneratedSplines[Index];
		if (ReplacedIds.Contains(Source.FrontageId)) { continue; }
		UDeepLevelRoadsideFrontageSplineComponent* Spline = AutomaticById.FindRef(Source.FrontageId);
		if (!Spline)
		{
			Spline = CreateFrontageComponent(
				MakeUniqueObjectName(this, UDeepLevelRoadsideFrontageSplineComponent::StaticClass(),
					*FString::Printf(TEXT("AutomaticFrontage_%d"), Index)),
				RF_Transactional, EDeepLevelRoadsideFrontageKind::Automatic);
		}
		Spline->Modify();
		RetainedAutomatic.Add(Spline);
		Spline->FrontageId = Source.FrontageId;
		Spline->bExcluded = ExcludedFrontageIds.Contains(Source.FrontageId);
		Spline->ClearSplinePoints(false);
		for (const FVector& Point : Source.Points)
		{
			Spline->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
			Spline->SetSplinePointType(Spline->GetNumberOfSplinePoints() - 1, ESplinePointType::Linear, false);
		}
		Spline->SetClosedLoop(Source.bClosed, false);
		if (!Spline->bExcluded && Source.bClosed)
		{
			++ClosedFrontageSplineCount;
		}
		else if (!Spline->bExcluded)
		{
			++OpenFrontageSplineCount;
		}
		Spline->SetDrawDebug(true);
		Spline->SetUnselectedSplineSegmentColor(Spline->bExcluded ? FLinearColor::Red : FLinearColor::Green);
		Spline->UpdateSpline();
		Spline->MarkRenderStateDirty();
		FrontageSplines.Add(Spline);
	}
	for (UDeepLevelRoadsideFrontageSplineComponent* Existing : ExistingFrontages)
	{
		if (Existing && Existing->Kind == EDeepLevelRoadsideFrontageKind::Automatic
			&& !RetainedAutomatic.Contains(Existing))
		{
			RemoveInstanceComponent(Existing);
			Existing->DestroyComponent();
		}
	}
	FrontageSplineCount = OpenFrontageSplineCount + ClosedFrontageSplineCount;
	MarkPackageDirty();
	UE_LOG(LogDeepLevelRoadsideBuilding, Log,
		TEXT("Generated %d active roadside frontages (%d open, %d closed, %d excluded)."),
		FrontageSplineCount, OpenFrontageSplineCount, ClosedFrontageSplineCount, ExcludedFrontageIds.Num());
	RedrawRoadsideEditorViewports();
#endif
}

void ADeepLevelPCGRoadsideBuildingActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	EnsureLayoutSourceGuid();
	SynchronizeCityLayoutRegistration();
#if WITH_EDITOR
	if (IsTemplate()) { return; }
	FrontageSplines.Reset();
	TInlineComponentArray<UDeepLevelRoadsideFrontageSplineComponent*> ExistingFrontages(this);
	for (UDeepLevelRoadsideFrontageSplineComponent* Frontage : ExistingFrontages)
	{
		if (!Frontage) { continue; }
		Frontage->SetDrawDebug(true);
		Frontage->SetUnselectedSplineSegmentColor(
			Frontage->bExcluded ? FLinearColor::Red
				: (Frontage->Kind == EDeepLevelRoadsideFrontageKind::Automatic
					? FLinearColor::Green : FLinearColor(0.0f, 0.8f, 1.0f)));
		Frontage->UpdateSpline();
		FrontageSplines.Add(Frontage);
	}
	if (PCGComponent)
	{
		PCGComponent->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
#if WITH_EDITORONLY_DATA
		PCGComponent->bRegenerateInEditor = false;
#endif
		PCGComponent->OnPCGGraphStartGeneratingDelegate.RemoveAll(this);
		PCGComponent->OnPCGGraphGeneratedDelegate.RemoveAll(this);
		PCGComponent->OnPCGGraphCancelledDelegate.RemoveAll(this);
		PCGComponent->OnPCGGraphCleanedDelegate.RemoveAll(this);
		PCGComponent->OnPCGGraphStartGeneratingDelegate.AddUObject(this, &ThisClass::OnBuildingGenerationStarted);
		PCGComponent->OnPCGGraphGeneratedDelegate.AddUObject(this, &ThisClass::OnBuildingGenerationCompleted);
		PCGComponent->OnPCGGraphCancelledDelegate.AddUObject(this, &ThisClass::OnBuildingGenerationCancelled);
		PCGComponent->OnPCGGraphCleanedDelegate.AddUObject(this, &ThisClass::OnBuildingGenerationCleaned);
	}
#endif
}

void ADeepLevelPCGRoadsideBuildingActor::PostUnregisterAllComponents()
{
	if (ADeepLevelCityLayoutActor* Registered = RegisteredCityLayout.Get())
	{
		Registered->UnregisterLayoutSource(*this);
	}
	RegisteredCityLayout.Reset();
	if (PCGComponent)
	{
		PCGComponent->OnPCGGraphStartGeneratingDelegate.RemoveAll(this);
		PCGComponent->OnPCGGraphGeneratedDelegate.RemoveAll(this);
		PCGComponent->OnPCGGraphCancelledDelegate.RemoveAll(this);
		PCGComponent->OnPCGGraphCleanedDelegate.RemoveAll(this);
	}
	Super::PostUnregisterAllComponents();
}

bool ADeepLevelPCGRoadsideBuildingActor::PrepareBuildingLayout(FText& OutError)
{
	OutError = FText::GetEmpty();
	FinalClosureMovedPlacementCount = 0;
	FinalClosurePhaseCount = 0;
	if (!PCGComponent || PCGComponent->IsPartitioned())
	{
		OutError = LOCTEXT("PartitionedRoadsideBuilding", "Roadside Building requires one non-partitioned PCG component.");
		return false;
	}
	UDeepLevelBuildingPlacementCatalog* LoadedCatalog = Catalog.LoadSynchronous();
	if (!LoadedCatalog || !LoadedCatalog->ValidateForGeneration(OutError))
	{
		if (!LoadedCatalog) { OutError = LOCTEXT("MissingRoadsideCatalog", "Roadside Building requires a calibrated Building Placement Catalog."); }
		return false;
	}
	const int32 ValidCornerMask = static_cast<int32>(EDeepLevelCornerPlacementFlags::All);
	if ((CornerPlacementMask & ~ValidCornerMask) != 0)
	{
		OutError = LOCTEXT("InvalidRoadsideCornerMask", "Roadside Building has an invalid Corner Placement value.");
		return false;
	}

	TSharedRef<FDeepLevelBuildingPreparedLayout> Layout = MakeShared<FDeepLevelBuildingPreparedLayout>();
	uint32 InputKey = HashCombineFast(GetTypeHash(RandomSeed), GetTypeHash(Catalog.ToSoftObjectPath()));
	for (UDeepLevelRoadsideFrontageSplineComponent* Frontage : FrontageSplines)
	{
		if (!Frontage || Frontage->bExcluded || Frontage->GetNumberOfSplinePoints() < 2) { continue; }
		UPCGSplineData* SplineData = NewObject<UPCGSplineData>();
		SplineData->Initialize(Frontage);
		FDeepLevelBuildingLinePath Path;
		if (!FDeepLevelBuildingLinePath::Build(*SplineData, Path, OutError)) { return false; }
		TArray<FVector2D> BlockPolygon;
		FDeepLevelBuildingPlacementCandidateResolver CandidateResolver;
		if (Frontage->IsClosedLoop())
		{
			BlockPolygon.Reserve(Frontage->GetNumberOfSplinePoints());
			for (int32 PointIndex = 0; PointIndex < Frontage->GetNumberOfSplinePoints(); ++PointIndex)
			{
				BlockPolygon.Add(FVector2D(Frontage->GetLocationAtSplinePoint(
					PointIndex, ESplineCoordinateSpace::World)));
			}
			CandidateResolver = [this, &BlockPolygon](
				const FDeepLevelBuildingPlacementDefinition& Definition,
				const EDeepLevelBuildingVolumeFace StreetFace,
				FDeepLevelBuildingLinePathSample& InOutSample)
			{
				const double SearchStep = FMath::Max(50.0, FrontageDepthTolerance / 10.0);
				for (double Distance = 0.0; Distance <= FrontageDepthTolerance + UE_DOUBLE_KINDA_SMALL_NUMBER;
					Distance += SearchStep)
				{
					for (const double Sign : {1.0, -1.0})
					{
						if (Distance <= UE_DOUBLE_KINDA_SMALL_NUMBER && Sign < 0.0) { continue; }
						FDeepLevelBuildingLinePathSample CandidateSample = InOutSample;
						CandidateSample.Location += CandidateSample.Right * Distance * Sign;
						const FTransform CandidateTransform = FDeepLevelBuildingPlacementGeometry::BuildActorTransform(
							Definition, StreetFace, CandidateSample.Location,
							CandidateSample.Forward, CandidateSample.Right);
						DeepLevelBuildingLayoutGeometry::FFootprint CandidateFootprint;
						DeepLevelBuildingLayoutGeometry::MakeFootprintCorners(
							Definition.PlacementVolume, CandidateTransform, CandidateFootprint);
						if (!IsFootprintInsideBlock(CandidateFootprint, BlockPolygon, BlockBoundaryMargin)) { continue; }
						InOutSample = CandidateSample;
						return true;
					}
				}
				return false;
			};
		}
		FDeepLevelBuildingLinePlan FrontagePlan;
		const int32 FrontageSeed = HashCombineFast(RandomSeed, GetTypeHash(Frontage->FrontageId));
		if (!FDeepLevelBuildingLinePlanner::BuildPlan(
			*LoadedCatalog, Path, FrontageSeed, VarietyStrength, CornerPreference,
			static_cast<EDeepLevelCornerPlacementFlags>(CornerPlacementMask), FrontagePlan, OutError,
			true, true, Frontage->IsClosedLoop()
				? EDeepLevelBuildingClearancePolicy::DecorativeBlock
				: EDeepLevelBuildingClearancePolicy::Strict,
			Frontage->IsClosedLoop() ? &CandidateResolver : nullptr))
		{
			return false;
		}
		if (!FrontagePlan.SkippedCornerIndices.IsEmpty())
		{
			TArray<FString> CornerLabels;
			CornerLabels.Reserve(FrontagePlan.SkippedCornerIndices.Num());
			for (const int32 CornerIndex : FrontagePlan.SkippedCornerIndices)
			{
				CornerLabels.Add(FString::FromInt(CornerIndex));
			}
			const FText Warning = FText::Format(
				LOCTEXT("SkippedRoadsideCorners", "Frontage {0} skipped corners {1}: no catalog building satisfies exposure and footprint constraints."),
				FText::FromString(Frontage->FrontageId.ToString(EGuidFormats::Short)),
				FText::FromString(FString::Join(CornerLabels, TEXT(", "))));
			UE_LOG(LogDeepLevelRoadsideBuilding, Warning, TEXT("%s"), *Warning.ToString());
#if WITH_EDITOR
			FDeepLevelDesignPCGEditorEvents::OnGenerationWarning().Broadcast(
				LOCTEXT("RoadsideBuildingWarningSystem", "Roadside Building"), Warning);
#endif
			Layout->RejectedCount += FrontagePlan.SkippedCornerIndices.Num();
			Layout->Plan.SkippedCornerIndices.Append(FrontagePlan.SkippedCornerIndices);
		}
		InputKey = HashCombineFast(InputKey, GetTypeHash(Frontage->FrontageId));
		InputKey = HashCombineFast(InputKey, GetTypeHash(Frontage->SplineCurves.Position.Points.Num()));
		for (FDeepLevelBuildingLinePlacement& Placement : FrontagePlan.Placements)
		{
			Placement.FrontageId = Frontage->FrontageId;
			const FDeepLevelBuildingPlacementDefinition* Definition = LoadedCatalog->Buildings.FindByPredicate(
				[&Placement](const FDeepLevelBuildingPlacementDefinition& Entry)
				{
					return Entry.BuildingClass == Placement.BuildingClass;
				});
			UClass* BuildingClass = Placement.BuildingClass.LoadSynchronous();
			if (!Definition || !BuildingClass || !BuildingClass->IsChildOf(APackedLevelActor::StaticClass())
				|| BuildingClass->HasAnyClassFlags(CLASS_Abstract))
			{
				OutError = LOCTEXT("InvalidRoadsidePLA", "Roadside Building output requires loadable Packed Level Actor classes.");
				return false;
			}
			const FTransform ActorTransform = FDeepLevelBuildingPlacementGeometry::BuildActorTransform(
				*Definition, Placement.StreetFace, Placement.PathSample.Location,
				Placement.PathSample.Forward, Placement.PathSample.Right);
			Layout->Transforms.Add(ActorTransform);
			Layout->Plan.Placements.Add(MoveTemp(Placement));
		}
	}
	if (Layout->Plan.Placements.IsEmpty())
	{
		OutError = LOCTEXT("NoRoadsidePlacements", "No active roadside frontage is long enough for a catalog building.");
		return false;
	}
	Layout->InputKey = InputKey;
	PreparedLayout = Layout;
	return true;
}

void ADeepLevelPCGRoadsideBuildingActor::GenerateBuildings()
{
#if WITH_EDITOR
	if (!GetWorld() || GetWorld()->IsGameWorld() || !PCGComponent || PCGComponent->IsGenerating()) { return; }
	GenerateFrontageSplines();
	if (!LastGenerationError.IsEmpty()) { return; }
	if (!PCGComponent->GetGraph())
	{
		ReportRoadsideFailure(*this, LOCTEXT("MissingRoadsideGraph", "Assign the Building Line PCG graph before generation."));
		return;
	}
	if (!PrepareBuildingLayout(LastGenerationError))
	{
		ReportRoadsideFailure(*this, LastGenerationError);
		return;
	}
	Modify();
	PCGComponent->GenerateLocal(true);
#endif
}

void ADeepLevelPCGRoadsideBuildingActor::AlignBuildingsVolFinal()
{
#if WITH_EDITOR
	if (!GetWorld() || GetWorld()->IsGameWorld() || !PCGComponent || PCGComponent->IsGenerating()) { return; }
	if (!bOutputCurrent || !PreparedLayout)
	{
		ReportRoadsideFailure(*this,
			LOCTEXT("FinalClosureRequiresGeneratedLayout", "Run Generate Buildings successfully before Align Buildings Vol Final."));
		return;
	}
	UDeepLevelBuildingPlacementCatalog* LoadedCatalog = Catalog.LoadSynchronous();
	if (!LoadedCatalog || !LoadedCatalog->ValidateForGeneration(LastGenerationError))
	{
		if (!LoadedCatalog)
		{
			LastGenerationError = LOCTEXT("FinalClosureMissingCatalog", "Align Buildings Vol Final requires a calibrated Building Placement Catalog.");
		}
		ReportRoadsideFailure(*this, LastGenerationError);
		return;
	}

	const FDeepLevelBuildingLinePlan BeforeAlignment = PreparedLayout->Plan;
	TSharedRef<FDeepLevelBuildingPreparedLayout> Layout = MakeShared<FDeepLevelBuildingPreparedLayout>(*PreparedLayout);
	FinalClosureMovedPlacementCount = 0;
	FinalClosurePhaseCount = 0;
	for (const UDeepLevelRoadsideFrontageSplineComponent* Frontage : FrontageSplines)
	{
		if (!Frontage || Frontage->bExcluded || !Frontage->IsClosedLoop()) { continue; }
		FDeepLevelBuildingLinePlan FrontagePlan;
		TArray<int32> LayoutIndices;
		for (int32 Index = 0; Index < Layout->Plan.Placements.Num(); ++Index)
		{
			if (Layout->Plan.Placements[Index].FrontageId == Frontage->FrontageId)
			{
				LayoutIndices.Add(Index);
				FrontagePlan.Placements.Add(Layout->Plan.Placements[Index]);
			}
		}
		if (FrontagePlan.Placements.Num() < 2) { continue; }
		TArray<FVector2D> BlockPolygon;
		BlockPolygon.Reserve(Frontage->GetNumberOfSplinePoints());
		for (int32 PointIndex = 0; PointIndex < Frontage->GetNumberOfSplinePoints(); ++PointIndex)
		{
			BlockPolygon.Add(FVector2D(Frontage->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World)));
		}
		FDeepLevelBuildingFinalClosureStats ClosureStats;
		if (!FDeepLevelBuildingPlacementClosure::ApplyFinal(
			*LoadedCatalog, BlockPolygon, BlockBoundaryMargin, FrontagePlan, ClosureStats, LastGenerationError))
		{
			ReportRoadsideFailure(*this, LastGenerationError);
			return;
		}
		for (int32 Index = 0; Index < LayoutIndices.Num(); ++Index)
		{
			Layout->Plan.Placements[LayoutIndices[Index]] = MoveTemp(FrontagePlan.Placements[Index]);
		}
		FinalClosureMovedPlacementCount += ClosureStats.MovedPlacementCount;
		FinalClosurePhaseCount = FMath::Max(FinalClosurePhaseCount, ClosureStats.PhaseCount);
	}

	if (FinalClosureMovedPlacementCount == 0)
	{
		LastGenerationError = LOCTEXT("FinalClosureNoMovement", "Align Buildings Vol Final found no safe placement movement.");
		FDeepLevelDesignPCGEditorEvents::OnGenerationWarning().Broadcast(
			LOCTEXT("RoadsideBuildingWarningSystem", "Roadside Building"), LastGenerationError);
		RedrawRoadsideEditorViewports();
		return;
	}

	Layout->Transforms.Reset(Layout->Plan.Placements.Num());
	for (const FDeepLevelBuildingLinePlacement& Placement : Layout->Plan.Placements)
	{
		const FDeepLevelBuildingPlacementDefinition* Definition = LoadedCatalog->Buildings.FindByPredicate(
			[&Placement](const FDeepLevelBuildingPlacementDefinition& Entry)
			{
				return Entry.BuildingClass == Placement.BuildingClass;
			});
		if (!Definition)
		{
			ReportRoadsideFailure(*this, LOCTEXT("FinalClosureMissingDefinition", "Vol.Final lost a building placement definition."));
			return;
		}
		Layout->Transforms.Add(FDeepLevelBuildingPlacementGeometry::BuildActorTransform(
			*Definition, Placement.StreetFace, Placement.PathSample.Location,
			Placement.PathSample.Forward, Placement.PathSample.Right));
	}
	Layout->InputKey = HashCombineFast(Layout->InputKey, GetTypeHash(FName(TEXT("Vol.Final"))));
	FDeepLevelCityGrid Grid;
	if (!CityLayout || !CityLayout->ResolveGrid(Grid, LastGenerationError))
	{
		ReportRoadsideFailure(*this, LastGenerationError);
		return;
	}
	++LayoutRevision;
	Layout->Fragment = {};
	Layout->Fragment.SourceGuid = LayoutSourceGuid;
	Layout->Fragment.SourceRevision = LayoutRevision;
	if (!DeepLevelBuildingSidewalkInfill::BuildAnchors(
		*LoadedCatalog, Grid, LayoutSourceGuid, LayoutRevision,
		BeforeAlignment, Layout->Plan, Layout->Fragment.Anchors, LastGenerationError))
	{
		ReportRoadsideFailure(*this, LastGenerationError);
		return;
	}
	PreparedLayout = Layout;
	LastGenerationError = FText::GetEmpty();
	SynchronizeCityLayoutRegistration();
	TSet<FIntPoint> DirtyChunks;
	FText SnapshotError;
	CityLayout->InvalidateSnapshot();
	if (!CityLayout->RefreshSnapshot(DirtyChunks, SnapshotError))
	{
		ReportRoadsideFailure(*this, SnapshotError);
		return;
	}
	CityLayout->NotifySidewalkInfillChanged();
	Modify();
	PCGComponent->GenerateLocal(true);
#endif
}

void ADeepLevelPCGRoadsideBuildingActor::OnBuildingGenerationStarted(UPCGComponent* Component)
{
	GeneratingLayout = PreparedLayout;
	bOutputCurrent = false;
	BuildingPlacementCount = 0;
	RejectedPlacementCount = GeneratingLayout ? GeneratingLayout->RejectedCount : 0;
	if (!GeneratingLayout)
	{
		if (LastGenerationError.IsEmpty())
		{
			LastGenerationError = LOCTEXT("UnpreparedRoadsideGeneration", "Roadside Building generation started without a prepared placement plan.");
			ReportRoadsideFailure(*this, LastGenerationError);
		}
		Component->CancelGeneration();
	}
}

void ADeepLevelPCGRoadsideBuildingActor::OnBuildingGenerationCompleted(UPCGComponent* Component)
{
	if (!GeneratingLayout) { return; }
	TArray<AActor*> Actors;
	Component->ForEachConstManagedResource([&Actors](const UPCGManagedResource* Resource)
	{
		if (const UPCGManagedActors* Managed = Cast<UPCGManagedActors>(Resource))
		{
			for (const TSoftObjectPtr<AActor>& ActorPtr : Managed->GetConstGeneratedActors())
			{
				if (AActor* Actor = ActorPtr.LoadSynchronous(); IsValid(Actor)) { Actors.Add(Actor); }
			}
		}
	});
	bool bMatches = Actors.Num() == GeneratingLayout->Plan.Placements.Num();
	TBitArray<> Matched(false, Actors.Num());
	for (int32 PlacementIndex = 0;
		PlacementIndex < GeneratingLayout->Plan.Placements.Num() && bMatches; ++PlacementIndex)
	{
		bMatches = false;
		UClass* ExpectedClass = GeneratingLayout->Plan.Placements[PlacementIndex].BuildingClass.LoadSynchronous();
		for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
		{
			if (!Matched[ActorIndex] && ExpectedClass && Actors[ActorIndex]->GetClass() == ExpectedClass
				&& Actors[ActorIndex]->GetActorTransform().Equals(GeneratingLayout->Transforms[PlacementIndex], 0.01))
			{
				Matched[ActorIndex] = true;
				bMatches = true;
				break;
			}
		}
	}
	if (!bMatches)
	{
		LastGenerationError = FText::Format(
			LOCTEXT("RoadsideOutputMismatch", "Roadside Building planned {0} buildings but PCG produced {1}."),
			FText::AsNumber(GeneratingLayout->Plan.Placements.Num()), FText::AsNumber(Actors.Num()));
		GeneratingLayout.Reset();
		ReportRoadsideFailure(*this, LastGenerationError);
		return;
	}
	BuildingPlacementCount = Actors.Num();
	bOutputCurrent = true;
	LastGenerationError = FText::GetEmpty();
	GeneratingLayout.Reset();
	MarkPackageDirty();
	UE_LOG(LogDeepLevelRoadsideBuilding, Log, TEXT("Generated %d roadside buildings."), BuildingPlacementCount);
}

void ADeepLevelPCGRoadsideBuildingActor::OnBuildingGenerationCancelled(UPCGComponent*)
{
	GeneratingLayout.Reset();
	bOutputCurrent = false;
	if (LastGenerationError.IsEmpty())
	{
		LastGenerationError = LOCTEXT("RoadsideGenerationCancelled", "Roadside Building generation was cancelled.");
		ReportRoadsideFailure(*this, LastGenerationError);
	}
}

void ADeepLevelPCGRoadsideBuildingActor::OnBuildingGenerationCleaned(UPCGComponent*)
{
	GeneratingLayout.Reset();
	bOutputCurrent = false;
	BuildingPlacementCount = 0;
	MarkPackageDirty();
}

#if WITH_EDITOR
void ADeepLevelPCGRoadsideBuildingActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	PreparedLayout.Reset();
	bOutputCurrent = false;
	SynchronizeCityLayoutRegistration();
	if (CityLayout) { CityLayout->InvalidateSnapshot(); }
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

UDeepLevelRoadsideFrontageSplineComponent* ADeepLevelPCGRoadsideBuildingActor::CreateFrontageComponent(
	const FName Name, const EObjectFlags Flags, const EDeepLevelRoadsideFrontageKind Kind)
{
	UDeepLevelRoadsideFrontageSplineComponent* Spline =
		NewObject<UDeepLevelRoadsideFrontageSplineComponent>(this, Name, Flags);
	Spline->Kind = Kind;
	Spline->ComponentTags.Add(TEXT("DeepLevelRoadsideFrontage"));
	AddInstanceComponent(Spline);
	Spline->SetupAttachment(SceneRoot);
	Spline->RegisterComponent();
	return Spline;
}

#if WITH_EDITOR
void ADeepLevelPCGRoadsideBuildingActor::ToggleFrontageExclusion(
	const UDeepLevelRoadsideFrontageSplineComponent& Frontage)
{
	if (Frontage.Kind != EDeepLevelRoadsideFrontageKind::Automatic || !Frontage.FrontageId.IsValid()) { return; }
	Modify();
	if (ExcludedFrontageIds.Remove(Frontage.FrontageId) == 0) { ExcludedFrontageIds.Add(Frontage.FrontageId); }
	MarkPackageDirty();
	GenerateFrontageSplines();
}

void ADeepLevelPCGRoadsideBuildingActor::CreateFrontageOverride(
	const UDeepLevelRoadsideFrontageSplineComponent& Source, const bool bReplace)
{
	Modify();
	UDeepLevelRoadsideFrontageSplineComponent* Override = CreateFrontageComponent(
		MakeUniqueObjectName(this, UDeepLevelRoadsideFrontageSplineComponent::StaticClass(),
			bReplace ? TEXT("ReplacementFrontage") : TEXT("AddedFrontage")),
		RF_Transactional,
		bReplace ? EDeepLevelRoadsideFrontageKind::Replace : EDeepLevelRoadsideFrontageKind::Add);
	Override->Modify();
	Override->FrontageId = FGuid::NewGuid();
	Override->ReplacedFrontageId = bReplace ? Source.FrontageId : FGuid();
	Override->SplineCurves = Source.SplineCurves;
	Override->SetClosedLoop(Source.IsClosedLoop(), false);
	Override->SetUnselectedSplineSegmentColor(FLinearColor(0.0f, 0.8f, 1.0f));
	Override->UpdateSpline();
	MarkPackageDirty();
	GenerateFrontageSplines();
}

void ADeepLevelPCGRoadsideBuildingActor::RemoveFrontageOverride(
	UDeepLevelRoadsideFrontageSplineComponent& Frontage)
{
	if (Frontage.Kind == EDeepLevelRoadsideFrontageKind::Automatic) { return; }
	Modify();
	RemoveInstanceComponent(&Frontage);
	Frontage.DestroyComponent();
	MarkPackageDirty();
	GenerateFrontageSplines();
}
#endif

#undef LOCTEXT_NAMESPACE
