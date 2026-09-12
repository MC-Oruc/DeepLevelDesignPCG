// Copyright <--\, Inc. All Rights Reserved.

#include "Building/DeepLevelBuildingLayout.h"
#include "City/DeepLevelCityDecoration.h"
#include "DeepLevelDesignPCGModule.h"
#include "Engine/World.h"
#include "PackedLevelActor/PackedLevelActor.h"
#include "PCGComponent.h"
#include "PCGManagedResource.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "DeepLevelBuildingGeneration"
DEFINE_LOG_CATEGORY_STATIC(LogDeepLevelBuildingGeneration, Log, All);

uint32 ADeepLevelPCGBuildingLineActor::GetInputKey(const FDeepLevelCityLayoutSnapshot* Base) const
{
	// Identifies the complete placement input across editor reloads.
	FBufferArchive Bytes;
	FObjectAndNameAsStringProxyArchive Ar(Bytes, false);
	uint8 Mode = static_cast<uint8>(PathSource);
	uint32 RoadsidePlannerVersion = PathSource == EDeepLevelBuildingPathSource::RoadSidewalkEdges ? 4U : 0U;
	int32 Seed = RandomSeed, CornerMask = BuildingLine->CornerPlacementMask;
	double Variety = VarietyStrength, Preference = CornerPreference;
	FGuid Source = LayoutSourceGuid;
	Ar << Mode << RoadsidePlannerVersion << Seed << CornerMask << Variety << Preference << Source;
	FDeepLevelCityGrid Grid;
	FText Error;
	if (ResolveCityGrid(Grid, Error))
	{
		Ar << Grid.Origin << Grid.TileSize << Grid.ChunkSizeInCells << Grid.ExtentInCells;
	}
	FString CatalogPath = Catalog.ToSoftObjectPath().ToString();
	Ar << CatalogPath;
	if (UDeepLevelBuildingPlacementCatalog* Data = Catalog.Get())
	{
		int32 Count = Data->Buildings.Num(); Ar << Count;
		for (const auto& Item : Data->Buildings)
		{
			auto Copy = Item;
			FDeepLevelBuildingPlacementDefinition::StaticStruct()->SerializeItem(Ar, &Copy, nullptr);
		}
		Count = Data->Presets.Num(); Ar << Count;
		for (const auto& Item : Data->Presets)
		{
			auto Copy = Item;
			FDeepLevelBuildingSequencePreset::StaticStruct()->SerializeItem(Ar, &Copy, nullptr);
		}
	}
	if (PathSource == EDeepLevelBuildingPathSource::AuthoredSpline)
	{
		FTransform Transform = BuildingLine->GetComponentTransform();
		bool bClosed = BuildingLine->IsClosedLoop();
		Ar << Transform << bClosed;
		FSplineCurves Curves = BuildingLine->SplineCurves;
		Ar << Curves.Position << Curves.Rotation << Curves.Scale << Curves.ReparamTable;
	}
	if (Base)
	{
		TArray<FIntPoint> Cells;
		Base->GetCells().GetKeys(Cells);
		Cells.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
		int32 Count = Cells.Num(); Ar << Count;
		for (const FIntPoint& Cell : Cells)
		{
			auto Copy = *Base->FindCell(Cell);
			FDeepLevelCityCellState::StaticStruct()->SerializeItem(Ar, &Copy, nullptr);
		}
		Count = Base->GetAnchors().Num(); Ar << Count;
		for (const auto& Anchor : Base->GetAnchors())
		{
			auto Copy = Anchor;
			FDeepLevelCityAnchor::StaticStruct()->SerializeItem(Ar, &Copy, nullptr);
		}
		Count = RoadsideExclusionActors.Num(); Ar << Count;
		for (const TSoftObjectPtr<AActor>& ActorReference : RoadsideExclusionActors)
		{
			const AActor* Actor = ActorReference.Get();
			FString Path = ActorReference.ToSoftObjectPath().ToString();
			FVector Origin = FVector::ZeroVector, Extent = FVector::ZeroVector;
			if (Actor) { Actor->GetActorBounds(false, Origin, Extent); }
			Ar << Path << Origin << Extent;
		}
	}
	return FCrc::MemCrc32(Bytes.GetData(), Bytes.Num());
}

bool ADeepLevelPCGBuildingLineActor::PrepareLayout(FText& OutError) const
{
	OutError = FText::GetEmpty();
	if (PCGComponent && PCGComponent->IsPartitioned())
	{
		OutError = LOCTEXT("PartitionedBuilding", "Building Line uses one actor-owned placement plan. Disable Is Partitioned on its PCG component.");
		return false;
	}
	FDeepLevelCityGrid Grid;
	if (!ResolveCityGrid(Grid, OutError)) { return false; }
	UDeepLevelBuildingPlacementCatalog* Data = Catalog.LoadSynchronous();
	if (!Data || !Data->ValidateForGeneration(OutError))
	{
		if (!Data) { OutError = LOCTEXT("MissingCatalog", "Building Line requires a calibrated Building Placement Catalog."); }
		return false;
	}
	TSharedPtr<const FDeepLevelCityLayoutSnapshot> Base;
	if (PathSource == EDeepLevelBuildingPathSource::RoadSidewalkEdges)
	{
		if (CityLayout->DerivedLayoutProviders.Num() != 1 || CityLayout->DerivedLayoutProviders[0] != this
			|| CityLayout->LayoutProviders.Contains(this))
		{
			OutError = LOCTEXT("RoadsideRegistration", "Register this Roadside Building once in its City Layout's Derived Layout Providers, and remove it from Layout Providers.");
			return false;
		}
		if (!CityLayout->BuildBaseSnapshot(Base, OutError)) { return false; }
	}
	else if (CityLayout->DerivedLayoutProviders.Contains(this))
	{
		OutError = LOCTEXT("AuthoredRegistration", "Authored Spline Building belongs in Layout Providers, not Derived Layout Providers.");
		return false;
	}
	const uint32 Key = GetInputKey(Base.Get());
	if (PreparedLayout && PreparedLayout->InputKey == Key) { return true; }
	TSharedRef<FDeepLevelBuildingPreparedLayout> Layout = MakeShared<FDeepLevelBuildingPreparedLayout>();
	Layout->InputKey = Key;
	if (Base)
	{
		const int32 Mask = BuildingLine->CornerPlacementMask;
		if (Mask < 0 || (Mask & ~static_cast<int32>(EDeepLevelCornerPlacementFlags::All)))
		{
			OutError = LOCTEXT("CornerMask", "Building corner placement mask is invalid.");
			return false;
		}
		TArray<FBox2D> ExclusionBounds;
		for (const TSoftObjectPtr<AActor>& ActorReference : RoadsideExclusionActors)
		{
			const AActor* Actor = ActorReference.Get();
			if (!Actor || Actor->GetWorld() != GetWorld())
			{
				OutError = LOCTEXT("InvalidExclusionActor", "Roadside Exclusion Actors must be valid actors in the same level world.");
				return false;
			}
			FVector Origin, Extent;
			Actor->GetActorBounds(false, Origin, Extent);
			if (Extent.X <= 0.0 || Extent.Y <= 0.0)
			{
				OutError = LOCTEXT("EmptyExclusionActor", "A Roadside Exclusion Actor has empty XY bounds.");
				return false;
			}
			ExclusionBounds.Emplace(FVector2D(Origin - Extent), FVector2D(Origin + Extent));
		}
		if (!DeepLevelBuildingRoadside::BuildPlan(*Base, *Data, LayoutSourceGuid, RandomSeed, VarietyStrength,
			CornerPreference, static_cast<EDeepLevelCornerPlacementFlags>(Mask), ExclusionBounds,
			Layout->Plan, Layout->RejectedCount, OutError)) { return false; }
	}
	else if (!BuildPlan(Layout->Plan, OutError)) { return false; }
	for (const FDeepLevelBuildingLinePlacement& Placement : Layout->Plan.Placements)
	{
		const auto* Definition = Data->Buildings.FindByPredicate([&Placement](const auto& Entry) { return Entry.BuildingClass == Placement.BuildingClass; });
		UClass* Class = Placement.BuildingClass.LoadSynchronous();
		if (!Definition || !Class || !Class->IsChildOf(APackedLevelActor::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract))
		{
			OutError = LOCTEXT("InvalidPLA", "Building output requires a loadable, concrete Packed Level Actor class.");
			return false;
		}
		Layout->Transforms.Add(FDeepLevelBuildingPlacementGeometry::BuildActorTransform(*Definition,
			Placement.StreetFace, Placement.PathSample.Location, Placement.PathSample.Forward, Placement.PathSample.Right));
	}
	BuildFragment(Layout->Plan, Layout->Fragment);
	TSharedPtr<const FDeepLevelCityLayoutSnapshot> Validated;
	const TArray<FDeepLevelCityLayoutFragment> Fragments = {Layout->Fragment};
	if (!FDeepLevelCityLayoutBuilder::Build(Grid, Fragments, Validated, OutError)) { return false; }
	PreparedLayout = Layout;
	return true;
}

bool ADeepLevelPCGBuildingLineActor::BuildDerivedCityLayoutFragment(const ADeepLevelCityLayoutActor& LayoutOwner, const FDeepLevelCityGrid& Grid,
	const FDeepLevelCityLayoutSnapshot& Base, FDeepLevelCityLayoutFragment& OutFragment, FText& OutError) const
{
	OutFragment = {};
	FDeepLevelCityGrid OwnedGrid;
	if (PathSource != EDeepLevelBuildingPathSource::RoadSidewalkEdges || CityLayout != &LayoutOwner
		|| CityLayout->DerivedLayoutProviders.Num() != 1 || CityLayout->DerivedLayoutProviders[0] != this)
	{
		OutError = LOCTEXT("WrongDerivedMode", "Derived Layout Providers requires RoadSidewalkEdges mode and the same owning City Layout.");
		return false;
	}
	if (!ResolveCityGrid(OwnedGrid, OutError)) { return false; }
	if (!OwnedGrid.Origin.Equals(Grid.Origin) || OwnedGrid.TileSize != Grid.TileSize
		|| OwnedGrid.ExtentInCells != Grid.ExtentInCells || OwnedGrid.ChunkSizeInCells != Grid.ChunkSizeInCells)
	{
		OutError = LOCTEXT("ForeignGrid", "Roadside Building cannot publish to another City Layout grid.");
		return false;
	}
	Catalog.LoadSynchronous();
	if (!bOutputCurrent || GeneratedInputKey != GetInputKey(&Base) || (PCGComponent && PCGComponent->IsGenerating()))
	{
		OutError = LOCTEXT("StaleOutput", "Roadside Building output is missing or out of date. Use Generate Buildings before regenerating City Decoration.");
		return false;
	}
	OutFragment = GeneratedFragment;
	return true;
}

void ADeepLevelPCGBuildingLineActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	if (!PCGComponent) { return; }
	PCGComponent->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
#if WITH_EDITORONLY_DATA
	PCGComponent->bRegenerateInEditor = false;
#endif
	PCGComponent->OnPCGGraphStartGeneratingDelegate.RemoveAll(this);
	PCGComponent->OnPCGGraphGeneratedDelegate.RemoveAll(this);
	PCGComponent->OnPCGGraphCancelledDelegate.RemoveAll(this);
	PCGComponent->OnPCGGraphCleanedDelegate.RemoveAll(this);
	PCGComponent->OnPCGGraphStartGeneratingDelegate.AddUObject(this, &ThisClass::OnGenerationStarted);
	PCGComponent->OnPCGGraphGeneratedDelegate.AddUObject(this, &ThisClass::OnGenerationCompleted);
	PCGComponent->OnPCGGraphCancelledDelegate.AddUObject(this, &ThisClass::OnGenerationCancelled);
	PCGComponent->OnPCGGraphCleanedDelegate.AddUObject(this, &ThisClass::OnGenerationCleaned);
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &ThisClass::OnInputPropertyChanged);
#endif
}

void ADeepLevelPCGBuildingLineActor::PostUnregisterAllComponents()
{
	if (PCGComponent)
	{
		PCGComponent->OnPCGGraphStartGeneratingDelegate.RemoveAll(this);
		PCGComponent->OnPCGGraphGeneratedDelegate.RemoveAll(this);
		PCGComponent->OnPCGGraphCancelledDelegate.RemoveAll(this);
		PCGComponent->OnPCGGraphCleanedDelegate.RemoveAll(this);
	}
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
#endif
	Super::PostUnregisterAllComponents();
}

void ADeepLevelPCGBuildingLineActor::GenerateBuildings()
{
#if WITH_EDITOR
	if (!GetWorld() || GetWorld()->IsGameWorld() || !PCGComponent || PCGComponent->IsGenerating()) { return; }
	LastGenerationError = FText::GetEmpty();
	if (!PCGComponent->GetGraph()) { LastGenerationError = LOCTEXT("MissingGraph", "Assign the Building Line PCG graph before generation."); }
	else { PrepareLayout(LastGenerationError); }
	if (!LastGenerationError.IsEmpty())
	{
		FDeepLevelDesignPCGEditorEvents::OnGenerationFailed().Broadcast(LOCTEXT("BuildingSystem", "Building Line"), LastGenerationError);
		return;
	}
	Modify();
	if (PathSource == EDeepLevelBuildingPathSource::RoadSidewalkEdges && BuildingLine && BuildingLine->GetNumberOfSplinePoints() < 2)
	{
		BuildingLine->ClearSplinePoints(false);
		BuildingLine->AddSplinePoint(FVector(0.0, 0.0, 0.0), ESplineCoordinateSpace::Local, false);
		BuildingLine->AddSplinePoint(FVector(100.0, 0.0, 0.0), ESplineCoordinateSpace::Local, false);
		BuildingLine->UpdateSpline();
	}
	PCGComponent->GenerateLocal(true);
#endif
}

void ADeepLevelPCGBuildingLineActor::OnGenerationStarted(UPCGComponent* Component)
{
	GeneratingLayout.Reset();
	LastGenerationError = FText::GetEmpty();
	if (!GetWorld() || GetWorld()->IsGameWorld() || !PrepareLayout(LastGenerationError))
	{
		if (LastGenerationError.IsEmpty()) { LastGenerationError = LOCTEXT("EditorOnly", "Building generation is editor-only."); }
		Component->CancelGeneration();
		return;
	}
	GeneratingLayout = PreparedLayout;
	bOutputCurrent = false;
	RejectedPlacementCount = GeneratingLayout->RejectedCount;
}

void ADeepLevelPCGBuildingLineActor::OnGenerationCompleted(UPCGComponent* Component)
{
	if (!GeneratingLayout) { return; }
	TArray<AActor*> Actors;
	Component->ForEachConstManagedResource([&Actors](const UPCGManagedResource* Resource)
	{
		if (const UPCGManagedActors* Managed = Cast<UPCGManagedActors>(Resource))
		{
			for (const TSoftObjectPtr<AActor>& ActorPtr : Managed->GetConstGeneratedActors())
			{
				AActor* Actor = ActorPtr.Get();
				if (!Actor)
				{
					Actor = ActorPtr.LoadSynchronous();
				}
				if (IsValid(Actor)) { Actors.Add(Actor); }
			}
		}
	});
	bool bMatches = Actors.Num() == GeneratingLayout->Plan.Placements.Num();
	UE_LOG(LogDeepLevelBuildingGeneration, Log,
		TEXT("Building generation verification: %d planned, %d managed actors, generating key %u."),
		GeneratingLayout->Plan.Placements.Num(), Actors.Num(), GeneratingLayout->InputKey);
	TBitArray<> Matched(false, Actors.Num());
	for (int32 Index = 0; Index < GeneratingLayout->Plan.Placements.Num() && bMatches; ++Index)
	{
		bMatches = false;
		UClass* TargetClass = GeneratingLayout->Plan.Placements[Index].BuildingClass.Get();
		if (!TargetClass)
		{
			TargetClass = GeneratingLayout->Plan.Placements[Index].BuildingClass.LoadSynchronous();
		}
		for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
		{
			if (!Matched[ActorIndex] && TargetClass && Actors[ActorIndex]->GetClass() == TargetClass
				&& Actors[ActorIndex]->GetActorTransform().Equals(GeneratingLayout->Transforms[Index], 0.01))
			{
				Matched[ActorIndex] = true; bMatches = true; break;
			}
		}
	}
	FText Error;
	if (!bMatches || !PrepareLayout(Error) || PreparedLayout->InputKey != GeneratingLayout->InputKey)
	{
		const uint32 PreparedKey = PreparedLayout ? PreparedLayout->InputKey : 0U;
		UE_LOG(LogDeepLevelBuildingGeneration, Error,
			TEXT("Building generation verification failed: outputs match=%s, prepared=%s, prepared key=%u, generating key=%u."),
			bMatches ? TEXT("true") : TEXT("false"), PreparedLayout ? TEXT("true") : TEXT("false"),
			PreparedKey, GeneratingLayout->InputKey);
		LastGenerationError = Error.IsEmpty() ? LOCTEXT("OutputMismatch", "Building generation did not produce the accepted PLA plan, or its inputs changed during generation. Correct the graph/inputs and Generate Buildings again.") : Error;
		GeneratingLayout.Reset();
		UE_LOG(LogDeepLevelBuildingGeneration, Error, TEXT("%s"), *LastGenerationError.ToString());
		return;
	}
	GeneratedFragment = GeneratingLayout->Fragment;
	GeneratedInputKey = GeneratingLayout->InputKey;
	bOutputCurrent = true;
	GeneratingLayout.Reset();
	MarkPackageDirty();
	if (CityLayout && (CityLayout->LayoutProviders.Contains(this) || CityLayout->DerivedLayoutProviders.Contains(this)))
	{
		if (CityLayout->DecorationSet)
		{
			CityLayout->DecorationComponent->Regenerate(false, Error);
		}
		else
		{
			TSet<FIntPoint> Dirty;
			CityLayout->RebuildSnapshot(Dirty, Error);
		}
	}
	LastGenerationError = Error;
	UE_LOG(LogDeepLevelBuildingGeneration, Log, TEXT("Building generation: %d accepted, %d rejected by occupancy/footprint clearance."), GeneratedFragment.Anchors.Num() / 8, RejectedPlacementCount);
}

void ADeepLevelPCGBuildingLineActor::OnGenerationCancelled(UPCGComponent* Component)
{
	GeneratingLayout.Reset();
	bOutputCurrent = false;
	if (LastGenerationError.IsEmpty()) { LastGenerationError = LOCTEXT("Cancelled", "Building generation was cancelled; generate again before publishing the layout."); }
}

void ADeepLevelPCGBuildingLineActor::OnGenerationCleaned(UPCGComponent* Component)
{
	GeneratingLayout.Reset();
	bOutputCurrent = false;
	GeneratedFragment = {};
	MarkPackageDirty();
}

void ADeepLevelPCGBuildingLineActor::InvalidateDerivedLayout()
{
	PreparedLayout.Reset();
	bOutputCurrent = false;
	if (PCGComponent) { PCGComponent->NotifyPropertiesChangedFromBlueprint(); }
	MarkPackageDirty();
}

#if WITH_EDITOR
void ADeepLevelPCGBuildingLineActor::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	NotifyBuildingLayoutChanged();
	Super::PostEditChangeProperty(Event);
}

void ADeepLevelPCGBuildingLineActor::PostEditMove(const bool bFinished)
{
	Super::PostEditMove(bFinished);
	if (bFinished) { NotifyBuildingLayoutChanged(); }
}

void ADeepLevelPCGBuildingLineActor::PostEditUndo()
{
	Super::PostEditUndo();
	NotifyBuildingLayoutChanged();
}

void ADeepLevelPCGBuildingLineActor::OnInputPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	if (!Object || Object == this || !CityLayout) { return; }
	const UActorComponent* Component = Cast<UActorComponent>(Object);
	const AActor* Actor = Component ? Component->GetOwner() : Cast<AActor>(Object);
	const bool bBaseProvider = PathSource == EDeepLevelBuildingPathSource::RoadSidewalkEdges && Actor && CityLayout->LayoutProviders.Contains(Actor);
	if (Object == Catalog.Get() || Object == CityLayout || Object == CityLayout->GridProfile
		|| Object == CityLayout->SceneRoot || bBaseProvider)
	{
		NotifyBuildingLayoutChanged();
		MarkPackageDirty();
	}
}
#endif

#undef LOCTEXT_NAMESPACE
