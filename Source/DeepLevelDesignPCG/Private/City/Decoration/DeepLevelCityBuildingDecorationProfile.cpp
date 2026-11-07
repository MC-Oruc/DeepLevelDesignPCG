// Copyright <--\, Inc. All Rights Reserved.
#include "City/Decoration/DeepLevelCityBuildingDecorationProfile.h"
#include "Misc/Crc.h"
#include "City/Decoration/DeepLevelCityBuildingDecorationResolver.h"
#include "City/Decoration/DeepLevelCityDecorationValidation.h"
#include "Building/DeepLevelBuildingFacadeVisibility.h"
#include "PackedLevelActor/PackedLevelActor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DeepLevelCityBuildingDecorationProfile)
#define LOCTEXT_NAMESPACE "DeepLevelCityBuildingDecoration"


#if WITH_EDITOR
FTransform FDeepLevelCityDecorationSymmetryPair::GetMirroredTransform(const FTransform& Transform) const
{
	const auto Reflect = [this](const FVector& Vector) { return Vector - 2.0 * FVector::DotProduct(Vector, PlaneNormal) * PlaneNormal; };
	// Flip local Y to keep a proper rotation, preserving light/decal projection along X.
	const FVector X = Reflect(Transform.GetRotation().GetAxisX());
	const FVector Y = -Reflect(Transform.GetRotation().GetAxisY());
	const FVector Z = Reflect(Transform.GetRotation().GetAxisZ());
	FTransform Result = Transform;
	Result.SetLocation(PlaneOrigin + Reflect(Transform.GetLocation() - PlaneOrigin));
	Result.SetRotation(FQuat(FMatrix(X, Y, Z, FVector::ZeroVector)).GetNormalized());
	if (bMirrorGeometry)
	{
		FVector Scale = Transform.GetScale3D();
		Scale.Y *= -1.0;
		Result.SetScale3D(Scale);
	}
	return Result;
}

bool FDeepLevelCityDecorationSymmetryPair::HasValidSettings() const
{
	return !PlaneOrigin.ContainsNaN() && !PlaneNormal.ContainsNaN() && PlaneNormal.IsNormalized()
		&& !LocationOffset.ContainsNaN() && !RotationOffset.ContainsNaN() && !ScaleMultiplier.ContainsNaN()
		&& !FMath::IsNearlyZero(ScaleMultiplier.X) && !FMath::IsNearlyZero(ScaleMultiplier.Y) && !FMath::IsNearlyZero(ScaleMultiplier.Z)
		&& (RotationMode == EDeepLevelCityDecorationRotationLink::Mirror || RotationMode == EDeepLevelCityDecorationRotationLink::Copy);
}

FTransform FDeepLevelCityDecorationSymmetryPair::SynchronizeTransform(
	const FTransform& Changed, const FTransform& Other, bool bFromFirst) const
{
	FTransform Input = Changed;
	const FQuat Offset = RotationOffset.Quaternion();
	// Offsets always describe First -> Second; reverse edits invert before reflecting.
	if (!bFromFirst)
	{
		Input.SetLocation(Changed.GetLocation() - LocationOffset);
		Input.SetRotation((Changed.GetRotation() * Offset.Inverse()).GetNormalized());
		Input.SetScale3D(Changed.GetScale3D() / ScaleMultiplier);
	}
	FTransform Result = Other;
	const FTransform Mirrored = GetMirroredTransform(Input);
	if (bSyncLocation) { Result.SetLocation(Mirrored.GetLocation() + (bFromFirst ? LocationOffset : FVector::ZeroVector)); }
	if (bSyncRotation)
	{
		const FQuat Base = RotationMode == EDeepLevelCityDecorationRotationLink::Copy ? Input.GetRotation() : Mirrored.GetRotation();
		Result.SetRotation((bFromFirst ? Base * Offset : Base).GetNormalized());
	}
	if (bSyncScale) { Result.SetScale3D(Mirrored.GetScale3D() * (bFromFirst ? ScaleMultiplier : FVector::OneVector)); }
	return Result;
}

void FDeepLevelCityDecorationSymmetryPair::CaptureOffsets(const FTransform& Source, const FTransform& Target)
{
	const FTransform Mirrored = GetMirroredTransform(Source);
	LocationOffset = Target.GetLocation() - Mirrored.GetLocation();
	const FQuat Base = RotationMode == EDeepLevelCityDecorationRotationLink::Copy ? Source.GetRotation() : Mirrored.GetRotation();
	RotationOffset = (Base.Inverse() * Target.GetRotation()).GetNormalized().Rotator();
	ScaleMultiplier = Target.GetScale3D() / Mirrored.GetScale3D();
}

bool FDeepLevelCityBuildingDecorationVariant::HasValidSymmetry() const
{
	if (SymmetryPairs.IsEmpty()) { return true; }
	TMap<FGuid, const FDeepLevelCityBuildingDecorationEntry*> EntriesById;
	EntriesById.Reserve(Entries.Num());
	for (const auto& Entry : Entries) { EntriesById.Add(Entry.EntryGuid, &Entry); }
	TSet<FGuid> Linked;
	for (const auto& Pair : SymmetryPairs)
	{
		const auto* First = EntriesById.FindRef(Pair.First);
		const auto* Second = EntriesById.FindRef(Pair.Second);
		if (!First || !Second || Pair.First == Pair.Second || Linked.Contains(Pair.First) || Linked.Contains(Pair.Second)
			|| !Pair.HasValidSettings()) { return false; }
		if (!Pair.SynchronizeTransform(First->LocalTransform, Second->LocalTransform, true).Equals(Second->LocalTransform, 0.001)) { return false; }
		Linked.Add(Pair.First);
		Linked.Add(Pair.Second);
	}
	return true;
}

void FDeepLevelCityBuildingDecorationVariant::RegenerateIdentities()
{
	VariantGuid = FGuid::NewGuid();
	TMap<FGuid, FGuid> Ids;
	for (auto& Entry : Entries)
	{
		const FGuid Old = Entry.EntryGuid;
		Entry.EntryGuid = FGuid::NewGuid();
		Ids.Add(Old, Entry.EntryGuid);
	}
	for (auto& Pair : SymmetryPairs)
	{
		if (const auto* Id = Ids.Find(Pair.First)) { Pair.First = *Id; }
		if (const auto* Id = Ids.Find(Pair.Second)) { Pair.Second = *Id; }
	}
	for (auto& State : WindowStates)
	{
		const FGuid Old = State.EntryGuid;
		State.EntryGuid = FGuid::NewGuid();
		for (auto& Pair : SymmetryPairs)
		{
			if (Pair.First == Old) Pair.First = State.EntryGuid;
			if (Pair.Second == Old) Pair.Second = State.EntryGuid;
		}
	}
}
#endif

bool FDeepLevelCityBuildingDecorationEntry::Validate(FText& Error) const
{
    if (bWindowLight && (Output != EDeepLevelCityDecorationOutput::Mesh || (!FMath::IsFinite(WindowColor.R) || !FMath::IsFinite(WindowColor.G) || !FMath::IsFinite(WindowColor.B))
        || WindowColor.R < 0 || WindowColor.G < 0 || WindowColor.B < 0 || !FMath::IsFinite(WindowIntensity) || WindowIntensity < 0))
    { Error = LOCTEXT("WindowValues", "Window lighting requires a mesh, finite non-negative color and brightness."); return false; }
	if (!bAutoFacade && Facades.IsEmpty()) { Error = LOCTEXT("EmptyExplicitFacades", "Explicit facade assignment requires at least one face."); return false; }
	TSet<EDeepLevelBuildingVolumeFace> UniqueFaces;
	for (const EDeepLevelBuildingVolumeFace Face : Facades)
	{
		if (static_cast<uint8>(Face) > static_cast<uint8>(EDeepLevelBuildingVolumeFace::NegativeY) || UniqueFaces.Contains(Face))
		{ Error = LOCTEXT("InvalidExplicitFacades", "Facade assignment contains an invalid or duplicate face."); return false; }
		UniqueFaces.Add(Face);
	}
	return DeepLevelCityDecorationValidation::ValidateTransform(LocalTransform, Error)
		&& DeepLevelCityDecorationValidation::ValidateOutput(Output, Mesh, ActorClass, DecalMaterial, DecalSize, Error);
}

void UDeepLevelCityBuildingDecorationProfile::GetValidationIssues(TArray<FDeepLevelCityBuildingDecorationIssue>& Issues) const
{
	Issues.Reset();
	UClass* Class = BuildingClass.LoadSynchronous();
	if (!Class || !Class->IsChildOf(APackedLevelActor::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		Issues.Add({{}, {}, LOCTEXT("Building", "Profile requires a concrete Packed Level Actor class.")});
	}
	TSet<FGuid> VariantIds;
	TSet<FGuid> EntryIds;
	TMap<FName, double> GroupWeights;
	TSet<FGuid> WindowIds;
	for (const auto& Definition : WindowDefinitions)
	{
		FText Error;
		if (!Definition.WindowGuid.IsValid() || WindowIds.Contains(Definition.WindowGuid)
			|| !Definition.RoomGuid.IsValid() || !Definition.MakeEntry().Validate(Error))
		{
			Issues.Add({{},Definition.WindowGuid,LOCTEXT("WindowDefinition", "Window definitions require unique identities, a room and valid geometry.")});
		}
		WindowIds.Add(Definition.WindowGuid);
	}
	for (const auto& Variant : Variants)
	{
		if (!Variant.VariantGuid.IsValid() || VariantIds.Contains(Variant.VariantGuid))
		{
			Issues.Add({Variant.VariantGuid, {}, LOCTEXT("VariantId", "Variant has a missing or duplicate identity.")});
		}
		VariantIds.Add(Variant.VariantGuid);
#if WITH_EDITOR
		if (!BuildVariantView(Variant).HasValidSymmetry())
		{
			Issues.Add({Variant.VariantGuid, {}, LOCTEXT("Symmetry", "Symmetry requires distinct paired entries, a valid plane and matching baked transforms.")});
		}
#endif
		if (!FMath::IsFinite(Variant.SelectionWeight) || Variant.SelectionWeight < 0.0)
		{
			Issues.Add({Variant.VariantGuid, {}, LOCTEXT("Weight", "Selection weight must be finite and non-negative.")});
		}
		GroupWeights.FindOrAdd(Variant.SelectionGroup) += Variant.SelectionWeight;
		for (const auto& Entry : Variant.Entries)
		{
			if (Entry.bWindowLight)
			{
				Issues.Add({Variant.VariantGuid,Entry.EntryGuid,LOCTEXT("ExpandedWindowData", "Import window geometry into shared definitions before generating decoration.")});
			}
			if (!Entry.EntryGuid.IsValid() || EntryIds.Contains(Entry.EntryGuid))
			{
				Issues.Add({Variant.VariantGuid, Entry.EntryGuid, LOCTEXT("EntryId", "Decoration has a missing or duplicate identity.")});
			}
			EntryIds.Add(Entry.EntryGuid);
			FText Error;
			if (!Entry.Validate(Error)) { Issues.Add({Variant.VariantGuid, Entry.EntryGuid, Error}); }
		}
		TSet<FGuid> References;
		for (const auto& State : Variant.WindowStates)
		{
			if (!WindowIds.Contains(State.WindowGuid) || References.Contains(State.WindowGuid)
				|| !State.EntryGuid.IsValid() || EntryIds.Contains(State.EntryGuid)
				|| !FMath::IsFinite(State.Color.R) || !FMath::IsFinite(State.Color.G) || !FMath::IsFinite(State.Color.B)
				|| State.Color.R < 0 || State.Color.G < 0 || State.Color.B < 0
				|| !FMath::IsFinite(State.Brightness) || State.Brightness < 0)
			{
				Issues.Add({Variant.VariantGuid,State.EntryGuid,LOCTEXT("WindowState", "Window states require a unique existing window reference and valid lighting values.")});
			}
			References.Add(State.WindowGuid);
			EntryIds.Add(State.EntryGuid);
		}
	}
	if (GroupWeights.IsEmpty())
	{
		Issues.Add({{}, {}, LOCTEXT("NoGroups", "Profile requires a decoration arrangement.")});
	}
	for (const auto& Group : GroupWeights)
	if (!FMath::IsFinite(Group.Value) || Group.Value <= 0.0)
	{
		Issues.Add({{}, {}, LOCTEXT("NoWeight", "Each selection group requires at least one variant with positive weight.")});
	}
}

bool UDeepLevelCityBuildingDecorationProfile::Validate(FText& Error) const
{
	TArray<FDeepLevelCityBuildingDecorationIssue> Issues;
	GetValidationIssues(Issues);
	Error = Issues.IsEmpty() ? FText::GetEmpty() : Issues[0].Message;
	return Issues.IsEmpty();
}

void UDeepLevelCityBuildingDecorationProfile::PostDuplicate(EDuplicateMode::Type Mode)
{
	Super::PostDuplicate(Mode);
#if WITH_EDITOR
	if (Mode != EDuplicateMode::PIE)
	{
		for (auto& Variant : Variants)
		{
			Variant.RegenerateIdentities();
		}
	}
#endif
}

void DeepLevelCityBuildingDecoration::Resolve(
	const FDeepLevelCityLayoutSnapshot& Snapshot,
	const UDeepLevelCityDecorationSet& ValidatedSet, const int32 Seed,
	TArray<FDeepLevelCityResolvedDecoration>& OutPlacements)
{
	struct FBuildingOccluder
	{
		FGuid Id;
		FTransform Frame;
		FBox LocalBounds;
		FBox WorldBounds;
	};
	TArray<FBuildingOccluder> Occluders;
	for (const FDeepLevelCityBuilding& Building : Snapshot.GetBuildings())
	{
		const FTransform Frame = Building.FacadeGeometry.LocalFrame * Building.Transform;
		const FBox Bounds(-Building.FacadeGeometry.Extent, Building.FacadeGeometry.Extent);
		Occluders.Add({Building.StableId, Frame, Bounds, Bounds.TransformBy(Frame)});
	}
	struct FProfileSelection
	{
		TArray<const FDeepLevelCityBuildingDecorationVariant*> Variants;
		double TotalWeight = 0.0;
	};
	TMap<FSoftObjectPath, TMap<FName, FProfileSelection>> Profiles;
	TArray<FDeepLevelCityBuildingDecorationVariant> VariantViews;
	int32 VariantCount = 0;
	for (const UDeepLevelCityBuildingDecorationProfile* Profile : ValidatedSet.BuildingProfiles) VariantCount += Profile->Variants.Num();
	VariantViews.Reserve(VariantCount);
	for (const UDeepLevelCityBuildingDecorationProfile* Profile : ValidatedSet.BuildingProfiles)
	{
		auto& Groups = Profiles.Add(Profile->BuildingClass.ToSoftObjectPath());
		for (const FDeepLevelCityBuildingDecorationVariant& StoredVariant : Profile->Variants)
		{
			const auto& Variant = VariantViews.Add_GetRef(Profile->BuildVariantView(StoredVariant));
			if (Variant.SelectionWeight <= 0.0) { continue; }
			FProfileSelection& Selection = Groups.FindOrAdd(Variant.SelectionGroup);
			Selection.Variants.Add(&Variant);
			Selection.TotalWeight += Variant.SelectionWeight;
		}
		for (auto& Group : Groups)
		{
			Group.Value.Variants.Sort([](const auto& A, const auto& B) { return A.VariantGuid < B.VariantGuid; });
		}
	}
	for (const FDeepLevelCityBuilding& Building : Snapshot.GetBuildings())
	{
		const auto* Groups = Profiles.Find(Building.BuildingClass.ToSoftObjectPath());
		if (!Groups) { continue; }
		TArray<const FBuildingOccluder*, TInlineAllocator<16>> Neighbors;
		const FTransform Frame = Building.FacadeGeometry.LocalFrame * Building.Transform;
		const FBox Bounds = FBox(-Building.FacadeGeometry.Extent, Building.FacadeGeometry.Extent).TransformBy(Frame);
		for (const FBuildingOccluder& Other : Occluders)
		{
			if (Other.Id != Building.StableId && Bounds.Intersect(Other.WorldBounds)) { Neighbors.Add(&Other); }
		}
		TArray<FName> GroupNames;
		Groups->GetKeys(GroupNames);
		GroupNames.Sort(FNameLexicalLess());
		for (const FName GroupName : GroupNames)
		{
			const FProfileSelection* Selection = &Groups->FindChecked(GroupName);
			const uint32 BaseSeed = HashCombineFast(GetTypeHash(Seed), GetTypeHash(Building.StableId));
			const uint32 GroupSeed = GroupName.IsNone() ? BaseSeed : HashCombineFast(BaseSeed, FCrc::StrCrc32(*GroupName.ToString().ToLower()));
			FRandomStream Random(static_cast<int32>(GroupSeed));
			const double TargetWeight = Random.GetFraction() * Selection->TotalWeight;
			double AccumulatedWeight = 0.0;
			for (const FDeepLevelCityBuildingDecorationVariant* Variant : Selection->Variants)
			{
				AccumulatedWeight += Variant->SelectionWeight;
				if (TargetWeight >= AccumulatedWeight) { continue; }
				const FGuid ArrangementId = FDeepLevelCityStableId::MakePlacementId(Building.StableId, Variant->VariantGuid, 0);
				for (const FDeepLevelCityBuildingDecorationEntry& Entry : Variant->Entries)
				{
					if (!Entry.bEnabled) { continue; }
					const FVector ActorLocalPoint = Entry.LocalTransform.GetLocation();
					const FVector WorldPoint = Building.Transform.TransformPosition(ActorLocalPoint);
					// Facade spans project onto the outer OBB face; recessed entries can still sit inside a neighbor.
					if (Neighbors.ContainsByPredicate([&WorldPoint](const FBuildingOccluder* Other)
					{
						return Other->WorldBounds.IsInside(WorldPoint)
							&& Other->LocalBounds.IsInside(Other->Frame.InverseTransformPosition(WorldPoint));
					})) { continue; }
					if (Entry.bAutoFacade)
					{
						const EDeepLevelBuildingVolumeFace Face = DeepLevelBuildingFacades::FindNearestFace(Building.FacadeGeometry, ActorLocalPoint);
						if (!DeepLevelBuildingFacades::IsPointVisible(Building, Face, ActorLocalPoint)) { continue; }
					}
					else if (!Entry.Facades.ContainsByPredicate([&Building, &ActorLocalPoint](const EDeepLevelBuildingVolumeFace Face)
					{
						return DeepLevelBuildingFacades::IsPointVisible(Building, Face, ActorLocalPoint);
					})) { continue; }
					FDeepLevelCityResolvedDecoration& Placement = OutPlacements.Emplace_GetRef();
					Placement.StableId = FDeepLevelCityStableId::MakePlacementId(ArrangementId, Entry.EntryGuid, 0);
					Placement.BuildingId = Building.StableId;
					Placement.VariantId = Variant->VariantGuid;
					Placement.EntryGuid = Entry.EntryGuid;
					Placement.Output = Entry.Output;
					Placement.Mesh = Entry.Mesh;
                    if (Entry.bWindowLight)
                    {
                        Placement.InstanceData = {Entry.WindowColor.R, Entry.WindowColor.G, Entry.WindowColor.B, Entry.WindowIntensity};
                        Placement.MaterialOverride = Entry.WindowMaterialOverride;
                    }
					Placement.ActorClass = Entry.ActorClass;
					Placement.DecalMaterial = Entry.DecalMaterial;
					Placement.DecalSize = Entry.DecalSize;
					Placement.Rendering = Entry.Rendering;
					Placement.Transform = Entry.LocalTransform * Building.Transform;
					Placement.Chunk = Snapshot.GetGrid().CellToChunk(Snapshot.GetGrid().WorldToCell(Placement.Transform.GetLocation()));
				}
				break;
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
