// Copyright <--\, Inc. All Rights Reserved.
#include "City/Decoration/DeepLevelCityBuildingDecorationProfile.h"
#include "City/Decoration/DeepLevelCityBuildingDecorationResolver.h"
#include "City/Decoration/DeepLevelCityDecorationValidation.h"
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
	TSet<FGuid> Linked;
	for (const auto& Pair : SymmetryPairs)
	{
		const auto* First = Entries.FindByPredicate([&](const auto& Entry) { return Entry.EntryGuid == Pair.First; });
		const auto* Second = Entries.FindByPredicate([&](const auto& Entry) { return Entry.EntryGuid == Pair.Second; });
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
}
#endif

bool FDeepLevelCityBuildingDecorationEntry::Validate(FText& Error) const
{
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
	double TotalWeight = 0.0;
	for (const auto& Variant : Variants)
	{
		if (!Variant.VariantGuid.IsValid() || VariantIds.Contains(Variant.VariantGuid))
		{
			Issues.Add({Variant.VariantGuid, {}, LOCTEXT("VariantId", "Variant has a missing or duplicate identity.")});
		}
		VariantIds.Add(Variant.VariantGuid);
#if WITH_EDITOR
		if (!Variant.HasValidSymmetry())
		{
			Issues.Add({Variant.VariantGuid, {}, LOCTEXT("Symmetry", "Symmetry requires distinct paired entries, a valid plane and matching baked transforms.")});
		}
#endif
		if (!FMath::IsFinite(Variant.SelectionWeight) || Variant.SelectionWeight < 0.0)
		{
			Issues.Add({Variant.VariantGuid, {}, LOCTEXT("Weight", "Selection weight must be finite and non-negative.")});
		}
		TotalWeight += Variant.SelectionWeight;
		for (const auto& Entry : Variant.Entries)
		{
			if (!Entry.EntryGuid.IsValid() || EntryIds.Contains(Entry.EntryGuid))
			{
				Issues.Add({Variant.VariantGuid, Entry.EntryGuid, LOCTEXT("EntryId", "Decoration has a missing or duplicate identity.")});
			}
			EntryIds.Add(Entry.EntryGuid);
			FText Error;
			if (!Entry.Validate(Error)) { Issues.Add({Variant.VariantGuid, Entry.EntryGuid, Error}); }
		}
	}
	if (!FMath::IsFinite(TotalWeight) || TotalWeight <= 0.0)
	{
		Issues.Add({{}, {}, LOCTEXT("NoWeight", "Profile requires at least one variant with positive weight.")});
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
	struct FProfileSelection
	{
		TArray<const FDeepLevelCityBuildingDecorationVariant*> Variants;
		double TotalWeight = 0.0;
	};
	TMap<FSoftObjectPath, FProfileSelection> Profiles;
	for (const UDeepLevelCityBuildingDecorationProfile* Profile : ValidatedSet.BuildingProfiles)
	{
		FProfileSelection& Selection = Profiles.Add(Profile->BuildingClass.ToSoftObjectPath());
		for (const FDeepLevelCityBuildingDecorationVariant& Variant : Profile->Variants)
		{
			if (Variant.SelectionWeight <= 0.0) { continue; }
			Selection.Variants.Add(&Variant);
			Selection.TotalWeight += Variant.SelectionWeight;
		}
		Selection.Variants.Sort([](const auto& A, const auto& B) { return A.VariantGuid < B.VariantGuid; });
	}
	for (const FDeepLevelCityBuilding& Building : Snapshot.GetBuildings())
	{
		const FProfileSelection* Selection = Profiles.Find(Building.BuildingClass.ToSoftObjectPath());
		if (!Selection) { continue; }
		FRandomStream Random(static_cast<int32>(HashCombineFast(GetTypeHash(Seed), GetTypeHash(Building.StableId))));
		const double TargetWeight = Random.GetFraction() * Selection->TotalWeight;
		double AccumulatedWeight = 0.0;
		for (const FDeepLevelCityBuildingDecorationVariant* Variant : Selection->Variants)
		{
			AccumulatedWeight += Variant->SelectionWeight;
			if (TargetWeight >= AccumulatedWeight) { continue; }
			const FGuid ArrangementId = FDeepLevelCityStableId::MakePlacementId(Building.StableId, Variant->VariantGuid, 0);
			for (const FDeepLevelCityBuildingDecorationEntry& Entry : Variant->Entries)
			{
				FDeepLevelCityResolvedDecoration& Placement = OutPlacements.Emplace_GetRef();
				Placement.StableId = FDeepLevelCityStableId::MakePlacementId(ArrangementId, Entry.EntryGuid, 0);
				Placement.BuildingId = Building.StableId;
				Placement.VariantId = Variant->VariantGuid;
				Placement.EntryGuid = Entry.EntryGuid;
				Placement.Output = Entry.Output;
				Placement.Mesh = Entry.Mesh;
				Placement.ActorClass = Entry.ActorClass;
				Placement.DecalMaterial = Entry.DecalMaterial;
				Placement.DecalSize = Entry.DecalSize;
				Placement.Transform = Entry.LocalTransform * Building.Transform;
				Placement.Chunk = Snapshot.GetGrid().CellToChunk(Snapshot.GetGrid().WorldToCell(Placement.Transform.GetLocation()));
			}
			break;
		}
	}
}

#undef LOCTEXT_NAMESPACE
