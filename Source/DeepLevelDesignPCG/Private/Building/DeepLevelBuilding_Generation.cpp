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

namespace
{
	void BroadcastBuildingFailure(const FText& Error)
	{
#if WITH_EDITOR
		FDeepLevelDesignPCGEditorEvents::OnGenerationFailed().Broadcast(
			LOCTEXT("BuildingLineSystem", "Building Line"), Error);
#endif
	}
}

uint32 ADeepLevelPCGBuildingLineActor::GetInputKey() const
{
	// Identifies the complete placement input across editor reloads.
	FBufferArchive Bytes;
	FObjectAndNameAsStringProxyArchive Ar(Bytes, false);
	int32 Seed = RandomSeed, CornerMask = BuildingLine->CornerPlacementMask;
	double Variety = VarietyStrength, Preference = CornerPreference;
	FGuid Source = LayoutSourceGuid;
	Ar << Seed << CornerMask << Variety << Preference << Source;
	FDeepLevelCityGrid Grid;
	FText Error;
	if (ResolveCityGrid(Grid, Error))
	{
		Ar << Grid.Origin << Grid.TileSize << Grid.ChunkSizeInCells;
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
	FTransform Transform = BuildingLine->GetComponentTransform();
	bool bClosed = BuildingLine->IsClosedLoop();
	Ar << Transform << bClosed;
	FSplineCurves Curves = BuildingLine->SplineCurves;
	Ar << Curves.Position << Curves.Rotation << Curves.Scale << Curves.ReparamTable;
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
	const uint32 Key = GetInputKey();
	if (PreparedLayout && PreparedLayout->InputKey == Key) { return true; }
	TSharedRef<FDeepLevelBuildingPreparedLayout> Layout = MakeShared<FDeepLevelBuildingPreparedLayout>();
	Layout->InputKey = Key;
	if (!BuildPlan(Layout->Plan, OutError)) { return false; }
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

void ADeepLevelPCGBuildingLineActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	SynchronizeCityLayoutRegistration();
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
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
#endif
	Super::PostUnregisterAllComponents();
}

void ADeepLevelPCGBuildingLineActor::SynchronizeCityLayoutRegistration()
{
	if (RegisteredCityLayout == CityLayout) { return; }
	if (ADeepLevelCityLayoutActor* Previous = RegisteredCityLayout.Get())
	{
		Previous->UnregisterLayoutSource(*this);
	}
	RegisteredCityLayout = CityLayout;
	if (CityLayout)
	{
		CityLayout->RegisterLayoutSource(*this);
	}
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
		UE_LOG(LogDeepLevelBuildingGeneration, Error, TEXT("%s"), *LastGenerationError.ToString());
		BroadcastBuildingFailure(LastGenerationError);
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
		BroadcastBuildingFailure(LastGenerationError);
		return;
	}
	GeneratedFragment = GeneratingLayout->Fragment;
	GeneratedInputKey = GeneratingLayout->InputKey;
	bOutputCurrent = true;
	GeneratingLayout.Reset();
	MarkPackageDirty();
	if (CityLayout)
	{
		SynchronizeCityLayoutRegistration();
		TSet<FIntPoint> Dirty;
		CityLayout->RefreshSnapshot(Dirty, Error);
	}
	LastGenerationError = Error;
	if (!LastGenerationError.IsEmpty())
	{
		UE_LOG(LogDeepLevelBuildingGeneration, Error, TEXT("%s"), *LastGenerationError.ToString());
		BroadcastBuildingFailure(LastGenerationError);
	}
	UE_LOG(LogDeepLevelBuildingGeneration, Log, TEXT("Building generation: %d accepted, %d rejected by occupancy/footprint clearance."), GeneratedFragment.Anchors.Num() / 8, RejectedPlacementCount);
}

void ADeepLevelPCGBuildingLineActor::OnGenerationCancelled(UPCGComponent* Component)
{
	GeneratingLayout.Reset();
	bOutputCurrent = false;
	if (LastGenerationError.IsEmpty()) { LastGenerationError = LOCTEXT("Cancelled", "Building generation was cancelled; generate again before publishing the layout."); }
	UE_LOG(LogDeepLevelBuildingGeneration, Error, TEXT("%s"), *LastGenerationError.ToString());
	BroadcastBuildingFailure(LastGenerationError);
}

void ADeepLevelPCGBuildingLineActor::OnGenerationCleaned(UPCGComponent* Component)
{
	GeneratingLayout.Reset();
	bOutputCurrent = false;
	GeneratedFragment = {};
	if (CityLayout) { CityLayout->InvalidateSnapshot(); }
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
	if (Object == Catalog.Get() || Object == CityLayout || Object == CityLayout->GridProfile
		|| Object == CityLayout->SceneRoot)
	{
		NotifyBuildingLayoutChanged();
		MarkPackageDirty();
	}
}
#endif

#undef LOCTEXT_NAMESPACE
