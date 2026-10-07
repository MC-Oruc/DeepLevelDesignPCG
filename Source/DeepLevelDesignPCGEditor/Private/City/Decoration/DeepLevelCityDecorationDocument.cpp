// Copyright <--\, Inc. All Rights Reserved.
#include "City/Decoration/DeepLevelCityDecorationDocument.h"
#include "City/Decoration/DeepLevelCityDecorationAuthoring.h"
#include "City/Decoration/DeepLevelCityDecorationValidation.h"
#include "Editor.h"
#include "PackedLevelActor/PackedLevelActor.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "DeepLevelCityDecorationDocument"
namespace Authoring = DeepLevelCityDecorationAuthoring;

FDeepLevelCityDecorationDocument::FDeepLevelCityDecorationDocument(EDeepLevelCityDecorationEditMode Mode) : EditMode(Mode)
{
	if (EditMode == EDeepLevelCityDecorationEditMode::Staging) { return; }
	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FDeepLevelCityDecorationDocument::ObjectChanged);
	if (GEditor) { GEditor->RegisterForUndo(this); }
}

FDeepLevelCityDecorationDocument::~FDeepLevelCityDecorationDocument() { Shutdown(); }

void FDeepLevelCityDecorationDocument::Shutdown()
{
	if (bClosed) { return; }
	bClosed = true;
	OnChanged.Clear();
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
	if (GEditor) { GEditor->UnregisterForUndo(this); }
	EndDrag(false);
}

void FDeepLevelCityDecorationDocument::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Set);
	Collector.AddReferencedObject(Catalog);
	Collector.AddReferencedObject(Profile);
}

void FDeepLevelCityDecorationDocument::Open(UDeepLevelCityDecorationSet* InSet, UDeepLevelCityBuildingDecorationProfile* RequestedProfile)
{
	if (bClosed) { return; }
	check(EditMode != EDeepLevelCityDecorationEditMode::Staging || !InSet || InSet->HasAnyFlags(RF_Transient));
	EndDrag();
	Set = InSet;
	Catalog = Set ? Set->BuildingCatalog.LoadSynchronous() : nullptr;
	Profile = RequestedProfile;
	if (Set) { Set->SetFlags(RF_Transactional); }
	if (RequestedProfile) { BuildingClass = RequestedProfile->BuildingClass; }
	if (Catalog)
	{
		if (!GetBuildingDefinition())
		{
			BuildingClass = Catalog->Buildings.IsEmpty() ? TSoftClassPtr<AActor>() : Catalog->Buildings[0].BuildingClass;
		}
		ResolveBuildingProfile();
	}
	else if (Set)
	{
		Profile = nullptr;
		BuildingClass.Reset();
	}
	VariantId.Invalidate();
	EntryId.Invalidate();
	ReconcileSelection();
	Notify(EDeepLevelCityDecorationChange::Context);
}

void FDeepLevelCityDecorationDocument::SetCatalog(UDeepLevelBuildingPlacementCatalog* InCatalog)
{
	if (EditMode == EDeepLevelCityDecorationEditMode::Staging || bClosed || !Set || Catalog == InCatalog) { return; }
	EndDrag();
	{
		TGuardValue<bool> Guard(bMutating, true);
		const FScopedTransaction Transaction(LOCTEXT("CatalogTx", "Choose Decoration Building Catalog"));
		Set->Modify();
		Set->BuildingCatalog = InCatalog;
		Set->PostEditChange();
	}
	Open(Set, Profile);
}

const FDeepLevelBuildingPlacementDefinition* FDeepLevelCityDecorationDocument::GetBuildingDefinition() const
{
	if (!Catalog || BuildingClass.IsNull()) { return nullptr; }
	const FDeepLevelBuildingPlacementDefinition* Found = nullptr;
	for (const auto& Definition : Catalog->Buildings)
	{
		if (Definition.BuildingClass != BuildingClass) { continue; }
		if (Found) { return nullptr; }
		Found = &Definition;
	}
	return Found;
}


const UDeepLevelCityBuildingDecorationProfile* FDeepLevelCityDecorationDocument::GetProfileForBuilding(const TSoftClassPtr<AActor>& Class) const
{
	if (bClosed || !Set || !Catalog || Class.IsNull()) { return nullptr; }
	int32 Definitions = 0;
	for (const auto& Item : Catalog->Buildings)
	{
		if (Item.BuildingClass == Class && ++Definitions > 1) { return nullptr; }
	}
	if (Definitions != 1) { return nullptr; }
	const UDeepLevelCityBuildingDecorationProfile* Found = nullptr;
	for (const UDeepLevelCityBuildingDecorationProfile* Candidate : Set->BuildingProfiles)
	{
		if (!Candidate || Candidate->BuildingClass != Class) { continue; }
		if (Found) { return nullptr; }
		Found = Candidate;
	}
	return Found;
}

void FDeepLevelCityDecorationDocument::ResolveBuildingProfile()
{
	Profile = const_cast<UDeepLevelCityBuildingDecorationProfile*>(GetProfileForBuilding(BuildingClass));
	if (Profile) { Profile->SetFlags(RF_Transactional); }
}

bool FDeepLevelCityDecorationDocument::SelectBuilding(const TSoftClassPtr<AActor>& Class)
{
	if (bClosed || !Catalog || !Catalog->Buildings.ContainsByPredicate([&](const auto& Item) { return Item.BuildingClass == Class; })) { return false; }
	if (BuildingClass == Class && !VariantId.IsValid() && !EntryId.IsValid()) { return true; }
	EndDrag();
	BuildingClass = Class;
	VariantId.Invalidate();
	EntryId.Invalidate();
	ResolveBuildingProfile();
	ReconcileSelection();
	Notify(EDeepLevelCityDecorationChange::Context);
	return true;
}

bool FDeepLevelCityDecorationDocument::CanCreateProfile() const
{
	if (bClosed || !Set || !GetBuildingDefinition()) { return false; }
	UClass* Class = BuildingClass.LoadSynchronous();
	return Class && Class->IsChildOf(APackedLevelActor::StaticClass())
		&& !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
		&& !Set->BuildingProfiles.ContainsByPredicate([&](const UDeepLevelCityBuildingDecorationProfile* Item) { return Item && Item->BuildingClass == BuildingClass; });
}

bool FDeepLevelCityDecorationDocument::AttachProfile(UDeepLevelCityBuildingDecorationProfile* InProfile, bool bInitializeNew)
{
	if (EditMode == EDeepLevelCityDecorationEditMode::Staging || !InProfile || !CanCreateProfile()) { return false; }
	if (bInitializeNew)
	{
		if (!InProfile->BuildingClass.IsNull() || !InProfile->Variants.IsEmpty()) { return false; }
	}
	else if (InProfile->BuildingClass != BuildingClass) { return false; }
	EndDrag();
	{
		TGuardValue<bool> Guard(bMutating, true);
		const FScopedTransaction Transaction(LOCTEXT("AttachTx", "Attach Building Decoration Profile"));
		Set->Modify();
		InProfile->SetFlags(RF_Transactional);
		InProfile->Modify();
		if (bInitializeNew)
		{
			InProfile->BuildingClass = BuildingClass;
			Authoring::AddVariant(*InProfile);
			InProfile->PostEditChange();
		}
		Set->BuildingProfiles.Add(InProfile);
		Set->PostEditChange();
	}
	ResolveBuildingProfile();
	VariantId.Invalidate();
	EntryId.Invalidate();
	ReconcileSelection();
	Notify(EDeepLevelCityDecorationChange::Context);
	return true;
}

bool FDeepLevelCityDecorationDocument::CanEdit() const
{
	if (bClosed || !Set || !Profile || !GetBuildingDefinition() || Profile->BuildingClass != BuildingClass) { return false; }
	TSet<FGuid> VariantIds;
	TSet<FGuid> EntryIds;
	for (const auto& Variant : Profile->Variants)
	{
		if (!Variant.VariantGuid.IsValid() || VariantIds.Contains(Variant.VariantGuid)) { return false; }
		VariantIds.Add(Variant.VariantGuid);
		if (!Variant.HasValidSymmetry()) { return false; }
		for (const auto& Entry : Variant.Entries)
		{
			if (!Entry.EntryGuid.IsValid() || EntryIds.Contains(Entry.EntryGuid)) { return false; }
			EntryIds.Add(Entry.EntryGuid);
		}
	}
	int32 Matches = 0;
	for (const UDeepLevelCityBuildingDecorationProfile* Item : Set->BuildingProfiles) { if (Item && Item->BuildingClass == BuildingClass) { ++Matches; } }
	UClass* Class = BuildingClass.LoadSynchronous();
	return Matches == 1 && Set->BuildingProfiles.Contains(Profile) && Class && Class->IsChildOf(APackedLevelActor::StaticClass())
		&& !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists);
}

const FDeepLevelCityBuildingDecorationVariant* FDeepLevelCityDecorationDocument::GetVariant() const
{
	return Profile && VariantId.IsValid() ? Authoring::FindVariant(*static_cast<const UDeepLevelCityBuildingDecorationProfile*>(Profile.Get()), VariantId) : nullptr;
}

const FDeepLevelCityBuildingDecorationEntry* FDeepLevelCityDecorationDocument::GetEntry() const
{
	return Profile && VariantId.IsValid() && EntryId.IsValid() ? Authoring::FindEntry(*static_cast<const UDeepLevelCityBuildingDecorationProfile*>(Profile.Get()), VariantId, EntryId) : nullptr;
}

FDeepLevelCityBuildingDecorationEntry* FDeepLevelCityDecorationDocument::MutableEntry()
{
	return Profile ? Authoring::FindEntry(*Profile, VariantId, EntryId) : nullptr;
}

void FDeepLevelCityDecorationDocument::ReconcileSelection()
{
	if (Set && Catalog && !BuildingClass.IsNull()
		&& !Catalog->Buildings.ContainsByPredicate([&](const auto& Definition) { return Definition.BuildingClass == BuildingClass; }))
	{
		BuildingClass.Reset();
		Profile = nullptr;
	}
	if (!GetVariant()) { VariantId.Invalidate(); }
	if (!GetEntry()) { EntryId.Invalidate(); }
}

void FDeepLevelCityDecorationDocument::SelectVariant(const FGuid& Id)
{
	if (bClosed || !Id.IsValid() || !Profile || !Authoring::FindVariant(*Profile, Id)) { return; }
	if (VariantId == Id && !EntryId.IsValid()) { return; }
	EndDrag();
	VariantId = Id;
	EntryId.Invalidate();
	Notify(EDeepLevelCityDecorationChange::Variant);
}

void FDeepLevelCityDecorationDocument::SelectEntry(const FGuid& Id)
{
	if (bClosed) { return; }
	if (Id.IsValid() && (!Profile || !Authoring::FindEntry(*Profile, VariantId, Id))) { return; }
	if (Id == EntryId) { return; }
	EndDrag();
	EntryId = Id;
	Notify(EDeepLevelCityDecorationChange::Selection);
}

void FDeepLevelCityDecorationDocument::Edit(const FText& Description, EDeepLevelCityDecorationChange Change, TFunctionRef<void()> Mutation)
{
	EndDrag();
	if (!CanEdit()) { return; }
	if (EditMode == EDeepLevelCityDecorationEditMode::Staging)
	{
		check(Profile->HasAnyFlags(RF_Transient));
		Mutation();
		Notify(Change);
		return;
	}
	{
		TGuardValue<bool> Guard(bMutating, true);
		const FScopedTransaction Transaction(Description);
		Profile->Modify();
		FProperty* Property = FindFProperty<FProperty>(Profile->GetClass(), GET_MEMBER_NAME_CHECKED(UDeepLevelCityBuildingDecorationProfile, Variants));
		Profile->PreEditChange(Property);
		Mutation();
		FPropertyChangedEvent Event(Property);
		Profile->PostEditChangeProperty(Event);
	}
	ReconcileSelection();
	Notify(Change);
}

bool FDeepLevelCityDecorationDocument::RemoveAssignment(const UDeepLevelCityDecorationSet* ExpectedSet,
	int32 Index, const UDeepLevelCityBuildingDecorationProfile* ExpectedProfile)
{
	if (EditMode == EDeepLevelCityDecorationEditMode::Staging || bClosed || !Set || Set != ExpectedSet || !Set->BuildingProfiles.IsValidIndex(Index) || Set->BuildingProfiles[Index] != ExpectedProfile) { return false; }
	EndDrag();
	{
		TGuardValue<bool> Guard(bMutating, true);
		const FScopedTransaction Transaction(LOCTEXT("UnlinkTx", "Unlink Building Decoration Profile"));
		Set->Modify();
		Set->BuildingProfiles.RemoveAt(Index);
		Set->PostEditChange();
	}
	ResolveBuildingProfile();
	ReconcileSelection();
	Notify(EDeepLevelCityDecorationChange::Context);
	return true;
}

void FDeepLevelCityDecorationDocument::SetCategories(const TArray<TObjectPtr<UDeepLevelCityDecorationCategory>>& Categories)
{
	if (EditMode == EDeepLevelCityDecorationEditMode::Staging || bClosed || !Set || Set->Categories == Categories) { return; }
	EndDrag();
	{
		TGuardValue<bool> Guard(bMutating, true);
		const FScopedTransaction Transaction(LOCTEXT("CategoriesTx", "Edit City Decoration Categories"));
		Set->Modify();
		Set->Categories = Categories;
		Set->PostEditChange();
	}
	Notify(EDeepLevelCityDecorationChange::Metadata);
}

void FDeepLevelCityDecorationDocument::AddVariant()
{
	Edit(LOCTEXT("AddVariant", "Add Decoration Variant"), EDeepLevelCityDecorationChange::Structure, [&]
	{
		VariantId = Authoring::AddVariant(*Profile);
		EntryId.Invalidate();
	});
}

void FDeepLevelCityDecorationDocument::DuplicateVariant()
{
	if (!GetVariant()) { return; }
	Edit(LOCTEXT("CopyVariant", "Duplicate Decoration Variant"), EDeepLevelCityDecorationChange::Structure, [&]
	{
		VariantId = Authoring::DuplicateVariant(*Profile, VariantId);
		EntryId.Invalidate();
	});
}

void FDeepLevelCityDecorationDocument::RemoveVariant()
{
	if (!GetVariant()) { return; }
	Edit(LOCTEXT("RemoveVariant", "Remove Decoration Variant"), EDeepLevelCityDecorationChange::Structure, [&]
	{
		Authoring::RemoveVariant(*Profile, VariantId);
		VariantId.Invalidate();
		EntryId.Invalidate();
	});
}

void FDeepLevelCityDecorationDocument::AddEntries(TConstArrayView<FDeepLevelCityBuildingDecorationEntry> Entries)
{
	if (!GetVariant() || Entries.IsEmpty()) { return; }
	Edit(LOCTEXT("AddEntries", "Add Building Decorations"), EDeepLevelCityDecorationChange::Structure, [&]
	{
		for (const auto& Entry : Entries) { EntryId = Authoring::AddEntry(*Profile, VariantId, Entry); }
	});
}


const FDeepLevelCityBuildingDecorationEntry* FDeepLevelCityDecorationDocument::GetEntry(const FGuid& Id) const
{
	return Profile ? Authoring::FindEntry(*Profile, VariantId, Id) : nullptr;
}

const FDeepLevelCityDecorationSymmetryPair* FDeepLevelCityDecorationDocument::GetSymmetryPair() const
{
	const auto* Variant = GetVariant();
	return Variant && EntryId.IsValid() ? Variant->SymmetryPairs.FindByPredicate([&](const auto& Pair)
	{
		return Pair.First == EntryId || Pair.Second == EntryId;
	}) : nullptr;
}

bool FDeepLevelCityDecorationDocument::CanCreateSymmetry(FGuid Target) const
{
	const auto* Definition = GetBuildingDefinition();
	if (!CanEdit() || !GetEntry() || GetSymmetryPair() || !Definition || !Definition->bCalibrated) { return false; }
	if (!Target.IsValid()) { return true; }
	return Target != EntryId && GetEntry(Target) && !GetVariant()->SymmetryPairs.ContainsByPredicate([&](const auto& Pair)
	{
		return Pair.First == Target || Pair.Second == Target;
	});
}

bool FDeepLevelCityDecorationDocument::CreateSymmetry(EAxis::Type Axis, FGuid Target)
{
	if (!CanCreateSymmetry(Target) || (Axis != EAxis::X && Axis != EAxis::Y && Axis != EAxis::Z)) { return false; }
	const auto Source = *GetEntry();
	const auto& Volume = GetBuildingDefinition()->PlacementVolume;
	FDeepLevelCityDecorationSymmetryPair Pair;
	Pair.First = Source.EntryGuid;
	Pair.PlaneOrigin = Volume.Center;
	const FVector Direction = Axis == EAxis::X ? FVector::ForwardVector : Axis == EAxis::Y ? FVector::RightVector : FVector::UpVector;
	Pair.PlaneNormal = Volume.Rotation.RotateVector(Direction).GetSafeNormal();
	auto Copy = Source;
	Copy.Name = FName(*(Source.Name.ToString() + TEXT("_Mirror")));
	Copy.LocalTransform = Pair.GetMirroredTransform(Source.LocalTransform);
	if (Target.IsValid())
	{
		Pair.Second = Target;
		Pair.CaptureOffsets(Source.LocalTransform, GetEntry(Target)->LocalTransform);
		if (!Pair.HasValidSettings()) { return false; }
	}
	Edit(LOCTEXT("CreateSymmetry", "Create Linked Symmetric Decoration"), EDeepLevelCityDecorationChange::Structure, [&]
	{
		if (!Target.IsValid()) { Pair.Second = Authoring::AddEntry(*Profile, VariantId, Copy); }
		Authoring::FindVariant(*Profile, VariantId)->SymmetryPairs.Add(Pair);
	});
	return true;
}

void FDeepLevelCityDecorationDocument::UnlinkSymmetry()
{
	if (!GetSymmetryPair()) { return; }
	Edit(LOCTEXT("UnlinkSymmetry", "Unlink Decoration Symmetry"), EDeepLevelCityDecorationChange::Metadata, [&]
	{
		Authoring::FindVariant(*Profile, VariantId)->SymmetryPairs.RemoveAll([&](const auto& Pair)
		{
			return Pair.First == EntryId || Pair.Second == EntryId;
		});
	});
}

bool FDeepLevelCityDecorationDocument::SetSymmetrySettings(const FDeepLevelCityDecorationSymmetryPair& Desired, FText& Error)
{
	const auto* Existing = GetSymmetryPair();
	if (!CanEdit() || !Existing) { return false; }
	if (Desired.First != Existing->First || Desired.Second != Existing->Second
		|| Desired.PlaneOrigin != Existing->PlaneOrigin || Desired.PlaneNormal != Existing->PlaneNormal || !Desired.HasValidSettings())
	{
		Error = LOCTEXT("InvalidLinkSettings", "Link settings require finite offsets, nonzero scale multipliers and unchanged endpoints and plane.");
		return false;
	}
	if (Desired.bSyncLocation == Existing->bSyncLocation && Desired.bSyncRotation == Existing->bSyncRotation
		&& Desired.bSyncScale == Existing->bSyncScale && Desired.RotationMode == Existing->RotationMode
		&& Desired.bMirrorGeometry == Existing->bMirrorGeometry && Desired.LocationOffset == Existing->LocationOffset
		&& Desired.RotationOffset == Existing->RotationOffset && Desired.ScaleMultiplier == Existing->ScaleMultiplier) { return true; }
	const FGuid FirstId = Existing->First;
	const FTransform Result = Desired.SynchronizeTransform(GetEntry(Desired.First)->LocalTransform, GetEntry(Desired.Second)->LocalTransform, true);
	if (!DeepLevelCityDecorationValidation::ValidateTransform(Result, Error)) { return false; }
	Edit(LOCTEXT("LinkSettings", "Edit Linked Transform Settings"), EDeepLevelCityDecorationChange::Transform, [&]
	{
		auto* Variant = Authoring::FindVariant(*Profile, VariantId);
		auto* Pair = Variant->SymmetryPairs.FindByPredicate([&](const auto& Item) { return Item.First == FirstId; });
		*Pair = Desired;
		Authoring::FindEntry(*Profile, VariantId, Pair->Second)->LocalTransform = Result;
	});
	return true;
}

void FDeepLevelCityDecorationDocument::SynchronizeSymmetryTransform()
{
	const auto* Pair = GetSymmetryPair();
	if (!Pair) { return; }
	const FGuid Other = Pair->First == EntryId ? Pair->Second : Pair->First;
	auto* Target = Authoring::FindEntry(*Profile, VariantId, Other);
	Target->LocalTransform = Pair->SynchronizeTransform(GetEntry()->LocalTransform, Target->LocalTransform, Pair->First == EntryId);
}

bool FDeepLevelCityDecorationDocument::ValidateLinkedTransform(const FTransform& Transform, FText& Error) const
{
	const auto* Pair = GetSymmetryPair();
	if (!Pair) { return true; }
	const auto* Other = GetEntry(Pair->First == EntryId ? Pair->Second : Pair->First);
	return DeepLevelCityDecorationValidation::ValidateTransform(
		Pair->SynchronizeTransform(Transform, Other->LocalTransform, Pair->First == EntryId), Error);
}

void FDeepLevelCityDecorationDocument::BroadcastTransformChange()
{
	const auto* Pair = GetSymmetryPair();
	const FGuid Other = Pair ? (Pair->First == EntryId ? Pair->Second : Pair->First) : FGuid();
	OnChanged.Broadcast(EDeepLevelCityDecorationChange::Transform, EntryId);
	if (Other.IsValid()) { OnChanged.Broadcast(EDeepLevelCityDecorationChange::Transform, Other); }
}

bool FDeepLevelCityDecorationDocument::CommitVariants(const TArray<FDeepLevelCityBuildingDecorationVariant>& Variants, FText& Error)
{
	if (EditMode != EDeepLevelCityDecorationEditMode::Undoable || !CanEdit()) { return false; }
	TStrongObjectPtr<UDeepLevelCityBuildingDecorationProfile> Candidate(NewObject<UDeepLevelCityBuildingDecorationProfile>());
	Candidate->BuildingClass = Profile->BuildingClass;
	Candidate->Variants = Variants;
	if (!Candidate->Validate(Error)) { return false; }
	Edit(LOCTEXT("BatchEdit", "Edit Building Decoration Arrangement"), EDeepLevelCityDecorationChange::Structure,
		[&] { Profile->Variants = Variants; });
	return true;
}

void FDeepLevelCityDecorationDocument::DuplicateEntry()
{
	if (!GetEntry()) { return; }
	Edit(LOCTEXT("DuplicateEntry", "Duplicate Building Decoration"), EDeepLevelCityDecorationChange::Structure,
		[&] { EntryId = Authoring::DuplicateEntry(*Profile, VariantId, EntryId); });
}

void FDeepLevelCityDecorationDocument::RemoveEntry()
{
	if (!GetEntry()) { return; }
	Edit(LOCTEXT("RemoveEntry", "Remove Building Decoration"), EDeepLevelCityDecorationChange::Structure, [&]
	{
		Authoring::RemoveEntry(*Profile, VariantId, EntryId);
		EntryId.Invalidate();
	});
}

void FDeepLevelCityDecorationDocument::CopyEntry() { if (const auto* Entry = GetEntry()) { Clipboard = *Entry; } }
void FDeepLevelCityDecorationDocument::PasteEntry() { if (Clipboard.IsSet()) { AddEntries(MakeArrayView(&Clipboard.GetValue(), 1)); } }

void FDeepLevelCityDecorationDocument::MoveEntry(int32 Direction)
{
	const auto* Variant = GetVariant();
	if (!Variant || !GetEntry()) { return; }
	const int32 Index = Variant->Entries.IndexOfByPredicate([&](const auto& Entry) { return Entry.EntryGuid == EntryId; });
	if (!Variant->Entries.IsValidIndex(Index + Direction)) { return; }
	Edit(LOCTEXT("Reorder", "Reorder Decoration"), EDeepLevelCityDecorationChange::Metadata,
		[&] { Authoring::FindVariant(*Profile, VariantId)->Entries.Swap(Index, Index + Direction); });
}

void FDeepLevelCityDecorationDocument::SetVariantFields(FName Name, double Weight)
{
	const auto* Variant = GetVariant();
	if (!Variant || (Variant->Name == Name && Variant->SelectionWeight == Weight) || !FMath::IsFinite(Weight) || Weight < 0.0) { return; }
	Edit(LOCTEXT("EditVariant", "Edit Decoration Variant"), EDeepLevelCityDecorationChange::Metadata, [&]
	{
		auto* Target = Authoring::FindVariant(*Profile, VariantId);
		Target->Name = Name;
		Target->SelectionWeight = Weight;
	});
}

bool FDeepLevelCityDecorationDocument::SetEntryFields(const FDeepLevelCityBuildingDecorationEntry& Desired,
	const FDeepLevelCityBuildingDecorationEntry& Baseline, FText& Error)
{
	const auto* Current = GetEntry();
	if (!CanEdit() || !Current || Desired.EntryGuid != EntryId || Baseline.EntryGuid != EntryId) { return false; }
	FDeepLevelCityBuildingDecorationEntry Patch = *Current;
	if (Desired.Name != Baseline.Name) { Patch.Name = Desired.Name; }
	if (Desired.Output != Baseline.Output) { Patch.Output = Desired.Output; }
	if (Desired.Mesh != Baseline.Mesh) { Patch.Mesh = Desired.Mesh; }
	if (Desired.ActorClass != Baseline.ActorClass) { Patch.ActorClass = Desired.ActorClass; }
	if (Desired.DecalMaterial != Baseline.DecalMaterial) { Patch.DecalMaterial = Desired.DecalMaterial; }
	if (Desired.DecalSize != Baseline.DecalSize) { Patch.DecalSize = Desired.DecalSize; }
	if (!Desired.LocalTransform.Equals(Baseline.LocalTransform)) { Patch.LocalTransform = Desired.LocalTransform; }
	if (!DeepLevelCityDecorationValidation::ValidateTransform(Patch.LocalTransform, Error)) { return false; }
	if (Patch.Output != Current->Output)
	{
		if (Patch.Output != EDeepLevelCityDecorationOutput::Mesh) { Patch.Mesh.Reset(); }
		if (Patch.Output != EDeepLevelCityDecorationOutput::Actor) { Patch.ActorClass.Reset(); }
		if (Patch.Output != EDeepLevelCityDecorationOutput::Decal) { Patch.DecalMaterial.Reset(); }
	}
	const bool bOutputChanged = Patch.Output != Current->Output || Patch.Mesh != Current->Mesh || Patch.ActorClass != Current->ActorClass
		|| Patch.DecalMaterial != Current->DecalMaterial || Patch.DecalSize != Current->DecalSize;
	const bool bTransformChanged = !Patch.LocalTransform.Equals(Current->LocalTransform);
	if (bTransformChanged && !ValidateLinkedTransform(Patch.LocalTransform, Error)) { return false; }
	if (!bOutputChanged && !bTransformChanged && Patch.Name == Current->Name) { return true; }
	const auto Change = bOutputChanged ? EDeepLevelCityDecorationChange::Output
		: bTransformChanged ? EDeepLevelCityDecorationChange::Transform : EDeepLevelCityDecorationChange::Metadata;
	Edit(LOCTEXT("EditEntry", "Edit Building Decoration"), Change, [&]
	{
		*MutableEntry() = Patch;
		if (bTransformChanged) { SynchronizeSymmetryTransform(); }
	});
	return true;
}

bool FDeepLevelCityDecorationDocument::SetTransform(const FTransform& Transform, FText& Error)
{
	const auto* Current = GetEntry();
	if (!Current) { return false; }
	const auto Baseline = *Current;
	auto Desired = Baseline;
	Desired.LocalTransform = Transform;
	return SetEntryFields(Desired, Baseline, Error);
}


void FDeepLevelCityDecorationDocument::ApplyDragTransform(const FTransform& Transform)
{
	if (EditMode == EDeepLevelCityDecorationEditMode::Staging || !CanEdit() || !GetEntry()) { return; }
	FText Error;
	if (!DeepLevelCityDecorationValidation::ValidateTransform(Transform, Error) || !ValidateLinkedTransform(Transform, Error)) { return; }
	if (GetEntry()->LocalTransform.Equals(Transform)) { return; }
	if (!bDragging)
	{
		bDragging = true;
		DragStart = GetEntry()->LocalTransform;
		bDirtyBeforeDrag = Profile->GetOutermost()->IsDirty();
	}
	if (!DragTransaction)
	{
		DragTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("Drag", "Transform Building Decoration"));
		Profile->Modify();
	}
	MutableEntry()->LocalTransform = Transform;
	SynchronizeSymmetryTransform();
	BroadcastTransformChange();
}

void FDeepLevelCityDecorationDocument::EndDrag(bool bNotify, bool bExternalEdit)
{
	if (!bDragging) { return; }
	bDragging = false;
	if (!DragTransaction) { return; }
	const bool bChanged = GetEntry() && !GetEntry()->LocalTransform.Equals(DragStart);
	if (!bChanged)
	{
		DragTransaction->Cancel();
		if (!bExternalEdit) { Profile->GetOutermost()->SetDirtyFlag(bDirtyBeforeDrag); }
	}
	else
	{
		TGuardValue<bool> Guard(bMutating, true);
		Profile->PostEditChange();
	}
	DragTransaction.Reset();
	if (bNotify && bChanged) { Notify(EDeepLevelCityDecorationChange::Transform); }
}

void FDeepLevelCityDecorationDocument::Undo() { if (bClosed || EditMode == EDeepLevelCityDecorationEditMode::Staging) { return; } EndDrag(); if (GEditor) { GEditor->UndoTransaction(); } }
void FDeepLevelCityDecorationDocument::Redo() { if (bClosed || EditMode == EDeepLevelCityDecorationEditMode::Staging) { return; } EndDrag(); if (GEditor) { GEditor->RedoTransaction(); } }

void FDeepLevelCityDecorationDocument::PostUndo(bool bSuccess)
{
	if (!bSuccess || bClosed) { return; }
	Catalog = Set ? Set->BuildingCatalog.LoadSynchronous() : nullptr;
	if (Set) { ResolveBuildingProfile(); }
	ReconcileSelection();
	Notify(EDeepLevelCityDecorationChange::Undo);
}

void FDeepLevelCityDecorationDocument::ObjectChanged(UObject* Object, FPropertyChangedEvent&)
{
	if (bClosed || bMutating) { return; }
	if (Object != Profile && Object != Set && Object != Catalog) { return; }
	EndDrag(false, true);
	if (Object == Set)
	{
		Catalog = Set->BuildingCatalog.LoadSynchronous();
		ResolveBuildingProfile();
	}
	else if (Object == Catalog || (Object == Profile && Profile->BuildingClass != BuildingClass)) { ResolveBuildingProfile(); }
	ReconcileSelection();
	Notify(Object == Profile ? EDeepLevelCityDecorationChange::Output : EDeepLevelCityDecorationChange::Context);
}

void FDeepLevelCityDecorationDocument::Notify(EDeepLevelCityDecorationChange Change)
{
	RefreshIssues();
	if (Change == EDeepLevelCityDecorationChange::Transform) { BroadcastTransformChange(); }
	else { OnChanged.Broadcast(Change, EntryId); }
}

void FDeepLevelCityDecorationDocument::RefreshIssues()
{
	Issues.Reset();
	if (!Set) { Issues.Add({{}, {}, {}, LOCTEXT("ChooseSet", "Choose a City Decoration Set to edit catalog buildings.")}); }
	else if (!Catalog) { Issues.Add({{}, {}, {}, LOCTEXT("ChooseCatalog", "Choose the Building Placement Catalog for this set.")}); }
	if (Catalog)
	{
		TSet<FSoftObjectPath> Classes;
		for (const auto& Definition : Catalog->Buildings)
		{
			const auto Path = Definition.BuildingClass.ToSoftObjectPath();
			if (Path.IsNull() || Classes.Contains(Path)) { Issues.Add({Path, {}, {}, LOCTEXT("CatalogClass", "Catalog has a missing or duplicate building class.")}); }
			Classes.Add(Path);
		}
	}
	TSet<FSoftObjectPath> ProfileClasses;
	if (Set)
	{
		for (const UDeepLevelCityDecorationCategory* Category : Set->Categories)
		{
			FText Error;
			if (!Category || !Category->Validate(Error))
			{
				Issues.Add({{}, {}, {}, Category ? Error : LOCTEXT("NullCategory", "Set contains a missing decoration category.")});
			}
		}
		for (int32 Index = 0; Index < Set->BuildingProfiles.Num(); ++Index)
		{
			const UDeepLevelCityBuildingDecorationProfile* Item = Set->BuildingProfiles[Index];
			const FSoftObjectPath Path = Item ? Item->BuildingClass.ToSoftObjectPath() : FSoftObjectPath();
			const auto AddAssignmentIssue = [&](const FText& Message)
			{
				FDeepLevelCityDecorationDocumentIssue Issue{Path, {}, {}, Message};
				Issue.AssignmentIndex = Index;
				Issue.AssignmentSet = Set.Get();
				Issue.AssignmentProfile = Item;
				Issues.Add(MoveTemp(Issue));
			};
			if (!Item) { AddAssignmentIssue(LOCTEXT("NullProfile", "Set contains a missing building profile.")); continue; }
			if (ProfileClasses.Contains(Path)) { AddAssignmentIssue(LOCTEXT("DuplicateProfile", "Set assigns multiple profiles to this building; unlink the conflicting assignment.")); }
			ProfileClasses.Add(Path);
			if (Catalog && !Catalog->Buildings.ContainsByPredicate([&](const auto& Definition) { return Definition.BuildingClass == Item->BuildingClass; }))
			{
				AddAssignmentIssue(LOCTEXT("OutsideCatalog", "Assigned profile is outside the selected building catalog."));
			}
		}
	}
	const auto AppendProfileIssues = [&](const UDeepLevelCityBuildingDecorationProfile* Item)
	{
		TArray<FDeepLevelCityBuildingDecorationIssue> Found;
		Item->GetValidationIssues(Found);
		for (const auto& Issue : Found) { Issues.Add({Item->BuildingClass.ToSoftObjectPath(), Issue.VariantId, Issue.EntryId, Issue.Message}); }
	};
	if (Set) { for (const UDeepLevelCityBuildingDecorationProfile* Item : Set->BuildingProfiles) { if (Item) { AppendProfileIssues(Item); } } }
	else if (Profile) { AppendProfileIssues(Profile); }
}
#undef LOCTEXT_NAMESPACE
