// Copyright <--\, Inc. All Rights Reserved.

#include "City/Decoration/DeepLevelCityDecorationAuthoring.h"
#include "City/Decoration/DeepLevelCityDecorationValidation.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"

#define LOCTEXT_NAMESPACE "DeepLevelCityDecorationAuthoring"

FDeepLevelCityBuildingDecorationVariant* DeepLevelCityDecorationAuthoring::FindVariant(
	UDeepLevelCityBuildingDecorationProfile& Profile, const FGuid& Id)
{
	return Profile.Variants.FindByPredicate([&](const auto& Variant) { return Variant.VariantGuid == Id; });
}

FDeepLevelCityBuildingDecorationEntry* DeepLevelCityDecorationAuthoring::FindEntry(
	UDeepLevelCityBuildingDecorationProfile& Profile, const FGuid& VariantId, const FGuid& EntryId)
{
	auto* Variant = FindVariant(Profile, VariantId);
	return Variant ? Variant->Entries.FindByPredicate([&](const auto& Entry) { return Entry.EntryGuid == EntryId; }) : nullptr;
}

const FDeepLevelCityBuildingDecorationVariant* DeepLevelCityDecorationAuthoring::FindVariant(
	const UDeepLevelCityBuildingDecorationProfile& Profile, const FGuid& Id)
{
	return Profile.Variants.FindByPredicate([&](const auto& Variant) { return Variant.VariantGuid == Id; });
}

const FDeepLevelCityBuildingDecorationEntry* DeepLevelCityDecorationAuthoring::FindEntry(
	const UDeepLevelCityBuildingDecorationProfile& Profile, const FGuid& VariantId, const FGuid& EntryId)
{
	const auto* Variant = FindVariant(Profile, VariantId);
	return Variant ? Variant->Entries.FindByPredicate([&](const auto& Entry) { return Entry.EntryGuid == EntryId; }) : nullptr;
}

FGuid DeepLevelCityDecorationAuthoring::AddVariant(UDeepLevelCityBuildingDecorationProfile& Profile)
{
	auto& Variant = Profile.Variants.Emplace_GetRef();
	Variant.VariantGuid = FGuid::NewGuid();
	Variant.Name = FName(*FString::Printf(TEXT("Variant_%d"), Profile.Variants.Num()));
	return Variant.VariantGuid;
}

FGuid DeepLevelCityDecorationAuthoring::DuplicateVariant(UDeepLevelCityBuildingDecorationProfile& Profile, const FGuid& Id)
{
	const auto* Source = FindVariant(Profile, Id);
	if (!Source) { return FGuid(); }
	FDeepLevelCityBuildingDecorationVariant Copy = *Source;
	Copy.RegenerateIdentities();
	Copy.Name = FName(*(Source->Name.ToString() + TEXT("_Copy")));
	const FGuid CopyId = Copy.VariantGuid;
	Profile.Variants.Add(MoveTemp(Copy));
	return CopyId;
}

FGuid DeepLevelCityDecorationAuthoring::AddEntry(UDeepLevelCityBuildingDecorationProfile& Profile,
	const FGuid& VariantId, const FDeepLevelCityBuildingDecorationEntry& Definition)
{
	auto* Variant = FindVariant(Profile, VariantId);
	if (!Variant) { return FGuid(); }
	FDeepLevelCityBuildingDecorationEntry Copy = Definition;
	Copy.EntryGuid = FGuid::NewGuid();
	const FGuid Id = Copy.EntryGuid;
	Variant->Entries.Add(MoveTemp(Copy));
	return Id;
}

FGuid DeepLevelCityDecorationAuthoring::DuplicateEntry(UDeepLevelCityBuildingDecorationProfile& Profile,
	const FGuid& VariantId, const FGuid& EntryId)
{
	const auto* Source = FindEntry(Profile, VariantId, EntryId);
	if (!Source) { return FGuid(); }
	FDeepLevelCityBuildingDecorationEntry Copy = *Source;
	Copy.Name = FName(*(Source->Name.ToString() + TEXT("_Copy")));
	return AddEntry(Profile, VariantId, Copy);
}

bool DeepLevelCityDecorationAuthoring::RemoveVariant(UDeepLevelCityBuildingDecorationProfile& Profile, const FGuid& Id)
{
	return Profile.Variants.RemoveAll([&](const auto& Variant) { return Variant.VariantGuid == Id; }) > 0;
}

bool DeepLevelCityDecorationAuthoring::RemoveEntry(UDeepLevelCityBuildingDecorationProfile& Profile,
	const FGuid& VariantId, const FGuid& EntryId)
{
	auto* Variant = FindVariant(Profile, VariantId);
	if (!Variant) { return false; }
	const bool bRemoved = Variant->Entries.RemoveAll([&](const auto& Entry) { return Entry.EntryGuid == EntryId; }) > 0;
	if (bRemoved) { Variant->SymmetryPairs.RemoveAll([&](const auto& Pair) { return Pair.First == EntryId || Pair.Second == EntryId; }); }
	return bRemoved;
}

bool DeepLevelCityDecorationAuthoring::MakeEntryFromAsset(UObject* Asset, FDeepLevelCityBuildingDecorationEntry& OutEntry, FText& OutError)
{
	OutEntry = FDeepLevelCityBuildingDecorationEntry();
	OutError = FText::GetEmpty();
	if (auto* Mesh = Cast<UStaticMesh>(Asset))
	{
		OutEntry.Output = EDeepLevelCityDecorationOutput::Mesh;
		OutEntry.Mesh = Mesh;
	}
	else if (auto* Blueprint = Cast<UBlueprint>(Asset))
	{
		UClass* Class = Blueprint->GeneratedClass;
		OutEntry.Output = EDeepLevelCityDecorationOutput::Actor;
		OutEntry.ActorClass = Class;
	}
	else if (auto* Material = Cast<UMaterialInterface>(Asset))
	{
		OutEntry.Output = EDeepLevelCityDecorationOutput::Decal;
		OutEntry.DecalMaterial = Material;
	}
	else
	{
		OutError = LOCTEXT("UnsupportedAsset", "Choose a Static Mesh, actor Blueprint, or Deferred Decal material.");
		return false;
	}
	OutEntry.Name = Asset->GetFName();
	return DeepLevelCityDecorationValidation::ValidateOutput(OutEntry.Output, OutEntry.Mesh, OutEntry.ActorClass, OutEntry.DecalMaterial, OutEntry.DecalSize, OutError);
}

FTransform DeepLevelCityDecorationAuthoring::ApplyTransformDelta(const FTransform& Transform,
	const FVector& Translation, const FRotator& Rotation, const FVector& Scale)
{
	FTransform Result = Transform;
	Result.AddToTranslation(Translation);
	Result.SetRotation((Rotation.Quaternion() * Transform.GetRotation()).GetNormalized());
	FVector NewScale = Transform.GetScale3D() + Scale;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (FMath::Abs(NewScale[Axis]) < DeepLevelCityDecorationValidation::MinimumScale)
		{
			NewScale[Axis] = Transform.GetScale3D()[Axis] < 0.0 ? -DeepLevelCityDecorationValidation::MinimumScale : DeepLevelCityDecorationValidation::MinimumScale;
		}
	}
	Result.SetScale3D(NewScale);
	return Result;
}

#undef LOCTEXT_NAMESPACE
