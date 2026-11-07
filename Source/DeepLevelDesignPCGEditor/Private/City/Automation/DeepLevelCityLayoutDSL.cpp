#include "City/Automation/DeepLevelCityLayoutDSL.h"
#include "Automation/DeepLevelPCGRequest.h"
#include "Automation/Data/DeepLevelPCGAutomationResult.h"
#include "City/DeepLevelCityLayout.h"
#include "City/DeepLevelCityDecoration.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/DecalComponent.h"
#include "Components/LightComponent.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/HLOD/HLODLayer.h"

FString DeepLevelCityLayoutDSL::Describe()
{
	return TEXT(R"JSON({"target":"loaded editor CityLayout actor path","operations":{"decoration.regenerate":[],"world.configure":["streaming: bool","hlodLayer: exact existing asset path"],"reference.exclude":["actors: 1..256 exact hidden actor paths"],"output.removeUntracked":["components: explicit component paths, 1..256"]},"scope":"Explicit city output, hidden authoring-reference exclusion and World Partition configuration. Explicit cleanup accepts untracked instance output components on the selected City actor; it validates every path before one Undo transaction.","inspection":"Reports saved output counts, HISM groups/instances, decals, child actors, lights and enabled actor/component ticks.","capture":"Not supported; use road/building world capture for street views.","dryRun":"Validates the request without changing output or saving."})JSON");
}

void DeepLevelCityLayoutDSL::Execute(const TSharedRef<FJsonObject>& Root, FDeepLevelPCGAutomationResult& Out)
{
	using namespace DeepLevelPCGRequest;
	FString Error, TargetPath;
	FOptions Options;
	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Fields(*Root, {TEXT("version"), TEXT("domain"), TEXT("target"), TEXT("operations"), TEXT("save"), TEXT("dryRun"), TEXT("includeState"), TEXT("capture")}, Error)
		|| !String(*Root, TEXT("target"), TargetPath, Error, true) || !Options.Read(*Root, Error))
	{
		Out.Report->SetStringField(TEXT("error"), Error); return;
	}
	if (Options.bCapture || !Root->TryGetArrayField(TEXT("operations"), Operations) || Operations->Num() > 1)
	{
		Out.Report->SetStringField(TEXT("error"), TEXT("City accepts zero operations for inspection or one city operation; capture is separate.")); return;
	}
	bool bGenerate = false;
	bool bConfigureWorld = false, bStreaming = false;
	FString HLODPath;
	TArray<FString> ReferencePaths;
	TArray<FString> RemovalPaths;
	if (!Operations->IsEmpty())
	{
		const auto Operation = Object(*(*Operations)[0]);
		FString Name;
		if (!Operation || !String(*Operation, TEXT("op"), Name, Error, true))
		{
			Out.Report->SetStringField(TEXT("error"), TEXT("Expected an explicit city operation.")); return;
		}
		if (Name == TEXT("decoration.regenerate") && Fields(*Operation, {TEXT("op")}, Error)) bGenerate = true;
		else if (Name == TEXT("world.configure") && Fields(*Operation, {TEXT("op"),TEXT("streaming"),TEXT("hlodLayer")}, Error))
		{
			if (!Operation->TryGetBoolField(TEXT("streaming"), bStreaming) || !String(*Operation, TEXT("hlodLayer"), HLODPath, Error, true))
			{ Out.Report->SetStringField(TEXT("error"), TEXT("World configuration requires explicit streaming and HLOD layer settings.")); return; }
			bConfigureWorld = true;
		}
		else if (Name == TEXT("reference.exclude") && Fields(*Operation, {TEXT("op"),TEXT("actors")}, Error))
		{
			const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
			if (!Operation->TryGetArrayField(TEXT("actors"), Paths) || Paths->IsEmpty() || Paths->Num() > 256)
			{ Out.Report->SetStringField(TEXT("error"), TEXT("actors requires 1..256 explicit hidden reference paths.")); return; }
			for (const auto& Path : *Paths)
			{
				FString Value;
				if (!Path->TryGetString(Value) || Value.IsEmpty() || ReferencePaths.Contains(Value))
				{ Out.Report->SetStringField(TEXT("error"), TEXT("Reference paths must be nonempty and unique.")); return; }
				ReferencePaths.Add(MoveTemp(Value));
			}
		}
		else if (Name == TEXT("output.removeUntracked") && Fields(*Operation, {TEXT("op"), TEXT("components")}, Error))
		{
			const TArray<TSharedPtr<FJsonValue>>* Paths;
			if (!Operation->TryGetArrayField(TEXT("components"), Paths) || Paths->IsEmpty() || Paths->Num() > 256)
			{ Out.Report->SetStringField(TEXT("error"), TEXT("components requires 1..256 explicit paths.")); return; }
			for (const auto& Path : *Paths)
			{
				FString Value;
				if (!Path->TryGetString(Value) || RemovalPaths.Contains(Value))
				{ Out.Report->SetStringField(TEXT("error"), TEXT("Component paths must be strings without duplicates.")); return; }
				RemovalPaths.Add(MoveTemp(Value));
			}
		}
		else { Out.Report->SetStringField(TEXT("error"), TEXT("Unsupported city operation or fields.")); return; }
	}
	auto* City = FindObject<ADeepLevelCityLayoutActor>(nullptr, *TargetPath);
	if (!City || City->GetWorld() != GEditor->GetEditorWorldContext().World() || !City->DecorationComponent)
	{
		Out.Report->SetStringField(TEXT("error"), TEXT("Target must be a loaded CityLayout in the current editor world.")); return;
	}
	TArray<UActorComponent*> Removal;
	TArray<AActor*> References;
	for (const FString& Path : ReferencePaths)
	{
		AActor* Actor = FindObject<AActor>(nullptr, *Path);
		if (!IsValid(Actor) || Actor == City || Actor->GetWorld() != City->GetWorld() || !Actor->IsHidden())
		{ Out.Report->SetStringField(TEXT("error"), TEXT("Reference exclusion accepts only explicit hidden actors in the current world: ") + Path); return; }
		References.Add(Actor);
	}
	UWorldPartition* Partition = City->GetWorld()->GetWorldPartition();
	UHLODLayer* Layer = bConfigureWorld ? LoadObject<UHLODLayer>(nullptr, *HLODPath) : nullptr;
	if (bConfigureWorld && (!Partition || !Layer))
	{ Out.Report->SetStringField(TEXT("error"), TEXT("World configuration requires World Partition and an existing HLOD layer asset.")); return; }
	if (!RemovalPaths.IsEmpty())
	{
		const TSet<UActorComponent*> Generated(City->DecorationComponent->GetGeneratedComponents());
		for (const FString& Path : RemovalPaths)
		{
			auto* Component = FindObject<UActorComponent>(nullptr, *Path);
			if (!IsValid(Component) || Component->GetOwner() != City || Component->CreationMethod != EComponentCreationMethod::Instance
				|| Generated.Contains(Component) || !(Component->IsA<UChildActorComponent>()
					|| Component->IsA<UHierarchicalInstancedStaticMeshComponent>() || Component->IsA<UDecalComponent>()))
			{ Out.Report->SetStringField(TEXT("error"), TEXT("Cleanup requires an untracked instance output on this City: ") + Path); return; }
			Removal.Add(Component);
		}
	}
	Out.bSuccess = true;
	Out.Report->SetStringField(TEXT("target"), TargetPath);
	if (!References.IsEmpty() && !Options.bDryRun)
	{
		FScopedTransaction Transaction(NSLOCTEXT("DeepLevelCityLayoutDSL", "ExcludeReferences", "Exclude authoring references from runtime"));
		for (AActor* Actor : References)
		{
			Actor->Modify();
			Actor->bIsEditorOnlyActor = true;
			Actor->SetActorEnableCollision(false);
			Actor->SetHLODLayer(nullptr);
			Actor->MarkPackageDirty();
		}
		Out.Report->SetNumberField(TEXT("excludedReferences"), References.Num());
		Out.Report->SetBoolField(TEXT("applied"), true);
	}
	if (bConfigureWorld && !Options.bDryRun)
	{
		FScopedTransaction Transaction(NSLOCTEXT("DeepLevelCityLayoutDSL", "ConfigureWorld", "Configure city streaming and HLOD"));
		Partition->Modify(); City->Modify();
		Partition->SetDefaultHLODLayer(Layer);
		Partition->SetEnableStreaming(bStreaming);
		City->DecorationHLODLayer = Layer;
		City->MarkPackageDirty();
		Out.Report->SetBoolField(TEXT("applied"), true);
	}
	if (Partition)
	{
		Out.Report->SetBoolField(TEXT("worldStreaming"), Partition->IsStreamingEnabled());
		Out.Report->SetStringField(TEXT("runtimeHash"), GetPathNameSafe(Partition->RuntimeHash));
		Out.Report->SetStringField(TEXT("defaultHLODLayer"), GetPathNameSafe(Partition->GetDefaultHLODLayer()));
	}
	if (!Removal.IsEmpty() && !Options.bDryRun)
	{
		FScopedTransaction Transaction(NSLOCTEXT("DeepLevelCityLayoutDSL", "RemoveUntracked", "Remove selected untracked city decoration"));
		City->Modify();
		for (UActorComponent* Component : Removal)
		{
			Component->Modify();
			City->RemoveInstanceComponent(Component);
			Component->DestroyComponent();
		}
		City->MarkPackageDirty();
		Out.Report->SetBoolField(TEXT("applied"), true);
		Out.Report->SetNumberField(TEXT("removedComponents"), Removal.Num());
	}
	if (bGenerate && !Options.bDryRun)
	{
		City->RegenerateDecoration();
		Out.bSuccess = City->DecorationComponent->LastGenerationError.IsEmpty();
		Out.Report->SetBoolField(TEXT("generated"), Out.bSuccess);
	}
	Out.Report->SetStringField(TEXT("lastGenerationError"), City->DecorationComponent->LastGenerationError.ToString());
	if (Options.bIncludeState)
	{
		int32 Groups = 0, Instances = 0, Decals = 0, Actors = 0, Lights = 0, ShadowedLights = 0, Ticks = 0;
		TArray<UActorComponent*> Components;
		City->GetComponents(Components);
		const TArray<UActorComponent*> Generated = City->DecorationComponent->GetGeneratedComponents();
		for (UActorComponent* Component : Generated) Components.AddUnique(Component);
		Out.Report->SetNumberField(TEXT("chunkActors"), City->DecorationComponent->GetGeneratedActors().Num());
		const TSet<UActorComponent*> GeneratedSet(Generated);
		TArray<TSharedPtr<FJsonValue>> UntrackedOutputs;
		for (UActorComponent* Component : Components)
		{
			Ticks += Component->PrimaryComponentTick.bCanEverTick && Component->IsComponentTickEnabled();
			if (const auto* Mesh = Cast<UHierarchicalInstancedStaticMeshComponent>(Component)) { ++Groups; Instances += Mesh->GetInstanceCount(); }
			if (Component->IsA<UDecalComponent>()) ++Decals;
			if (!GeneratedSet.Contains(Component) && Component->CreationMethod == EComponentCreationMethod::Instance
				&& (Component->IsA<UHierarchicalInstancedStaticMeshComponent>() || Component->IsA<UDecalComponent>()))
			{
				auto Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("component"), Component->GetPathName());
				Entry->SetStringField(TEXT("class"), Component->GetClass()->GetPathName());
				Entry->SetBoolField(TEXT("instance"), true);
				UntrackedOutputs.Add(MakeShared<FJsonValueObject>(Entry));
			}
			if (const auto* Child = Cast<UChildActorComponent>(Component))
			{
				if (!GeneratedSet.Contains(Component))
				{
					auto Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("component"), Component->GetPathName());
					Entry->SetStringField(TEXT("class"), Child->GetChildActorClass() ? Child->GetChildActorClass()->GetPathName() : FString());
					Entry->SetBoolField(TEXT("instance"), Component->CreationMethod == EComponentCreationMethod::Instance);
					UntrackedOutputs.Add(MakeShared<FJsonValueObject>(Entry));
				}
				if (const AActor* Actor = Child->GetChildActor())
				{
					++Actors; Ticks += Actor->PrimaryActorTick.bCanEverTick && Actor->IsActorTickEnabled();
					TArray<UActorComponent*> ChildComponents; Actor->GetComponents(ChildComponents);
					for (const UActorComponent* ChildComponent : ChildComponents)
					{
						Ticks += ChildComponent->PrimaryComponentTick.bCanEverTick && ChildComponent->IsComponentTickEnabled();
						if (const auto* Light = Cast<ULightComponent>(ChildComponent)) { ++Lights; ShadowedLights += Light->CastShadows; }
					}
				}
			}
		}
		Out.Report->SetNumberField(TEXT("placements"), City->DecorationComponent->LastPlacementCount);
		Out.Report->SetNumberField(TEXT("meshGroups"), Groups);
		Out.Report->SetNumberField(TEXT("meshInstances"), Instances);
		Out.Report->SetNumberField(TEXT("decals"), Decals);
		Out.Report->SetNumberField(TEXT("actors"), Actors);
		Out.Report->SetNumberField(TEXT("lights"), Lights);
		Out.Report->SetNumberField(TEXT("shadowedLights"), ShadowedLights);
		Out.Report->SetNumberField(TEXT("enabledTicks"), Ticks);
		Out.Report->SetArrayField(TEXT("untrackedOutputs"), UntrackedOutputs);
	}
	if (Out.bSuccess && Options.bSave && !Options.bDryRun)
	{
		const bool bSaved = UEditorLoadingAndSavingUtils::SaveMap(City->GetWorld(), City->GetWorld()->GetOutermost()->GetName());
		Out.Report->SetBoolField(TEXT("saved"), bSaved);
		if (!bSaved) { Out.bSuccess = false; Out.Report->SetStringField(TEXT("saveError"), Error); }
	}
}

