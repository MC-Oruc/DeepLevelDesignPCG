// Copyright <--\, Inc. All Rights Reserved.

#include "Building/DeepLevelBuildingPCG.h"

// ---- DeepLevelBuildingLineCatalogModel ----



#define LOCTEXT_NAMESPACE "DeepLevelBuildingLineCatalogModel"

namespace DeepLevelBuildingLinePacking
{
	double FCatalogModel::ExposureScore(const EDeepLevelStreetExposureRule Rule)
	{
		switch (Rule)
		{
		case EDeepLevelStreetExposureRule::Required: return 4.0;
		case EDeepLevelStreetExposureRule::Preferred: return 3.0;
		case EDeepLevelStreetExposureRule::Neutral: return 1.0;
		case EDeepLevelStreetExposureRule::Forbidden: return 0.0;
		default: return 0.0;
		}
	}

	uint8 FCatalogModel::MakeFaceMask(const EDeepLevelBuildingVolumeFace Face)
	{
		return 1U << static_cast<uint8>(Face);
	}

	EDeepLevelBuildingVolumeFace FCatalogModel::GetAdjacentFace(
		const EDeepLevelBuildingVolumeFace PrimaryFace,
		const bool bForwardEnd)
	{
		switch (PrimaryFace)
		{
		case EDeepLevelBuildingVolumeFace::PositiveX:
			return bForwardEnd ? EDeepLevelBuildingVolumeFace::PositiveY : EDeepLevelBuildingVolumeFace::NegativeY;
		case EDeepLevelBuildingVolumeFace::NegativeX:
			return bForwardEnd ? EDeepLevelBuildingVolumeFace::NegativeY : EDeepLevelBuildingVolumeFace::PositiveY;
		case EDeepLevelBuildingVolumeFace::PositiveY:
			return bForwardEnd ? EDeepLevelBuildingVolumeFace::NegativeX : EDeepLevelBuildingVolumeFace::PositiveX;
		case EDeepLevelBuildingVolumeFace::NegativeY:
			return bForwardEnd ? EDeepLevelBuildingVolumeFace::PositiveX : EDeepLevelBuildingVolumeFace::NegativeX;
		default:
			return EDeepLevelBuildingVolumeFace::PositiveX;
		}
	}

	bool FCatalogModel::ValidateExposure(
		const FDeepLevelBuildingPlacementDefinition& Definition,
		const uint8 ExposedFaceMask)
	{
		for (uint8 FaceIndex = 0; FaceIndex < 4; ++FaceIndex)
		{
			const EDeepLevelBuildingVolumeFace Face = static_cast<EDeepLevelBuildingVolumeFace>(FaceIndex);
			const EDeepLevelStreetExposureRule Rule = FDeepLevelBuildingPlacementGeometry::GetExposureRule(
				Definition.PlacementVolume.Exposure,
				Face);
			const bool bExposed = (ExposedFaceMask & MakeFaceMask(Face)) != 0;
			if ((Rule == EDeepLevelStreetExposureRule::Required && !bExposed)
				|| (Rule == EDeepLevelStreetExposureRule::Forbidden && bExposed))
			{
				return false;
			}
		}
		return true;
	}

	bool FCatalogModel::IsSpanFaceValid(
		const FDeepLevelBuildingPlacementDefinition& Definition,
		const EDeepLevelBuildingVolumeFace PrimaryFace)
	{
		return ValidateExposure(Definition, MakeFaceMask(PrimaryFace));
	}

	bool FCatalogModel::IsCornerFaceValid(
		const FDeepLevelBuildingPlacementDefinition& Definition,
		const EDeepLevelBuildingVolumeFace PrimaryFace,
		const bool bForwardEnd)
	{
		const EDeepLevelBuildingVolumeFace AdjacentFace = GetAdjacentFace(PrimaryFace, bForwardEnd);
		return ValidateExposure(Definition, MakeFaceMask(PrimaryFace) | MakeFaceMask(AdjacentFace));
	}

	double FCatalogModel::CornerExposureScore(
		const FDeepLevelBuildingPlacementDefinition& Definition,
		const EDeepLevelBuildingVolumeFace PrimaryFace,
		const bool bForwardEnd)
	{
		const EDeepLevelBuildingVolumeFace AdjacentFace = GetAdjacentFace(PrimaryFace, bForwardEnd);
		return ExposureScore(FDeepLevelBuildingPlacementGeometry::GetExposureRule(
			Definition.PlacementVolume.Exposure,
			AdjacentFace));
	}

	void FCatalogModel::GetEligibleFaces(
		const FDeepLevelBuildingPlacementDefinition& Definition,
		TArray<EDeepLevelBuildingVolumeFace>& OutFaces)
	{
		OutFaces.Reset();
		for (uint8 FaceIndex = 0; FaceIndex < 4; ++FaceIndex)
		{
			const EDeepLevelBuildingVolumeFace Face = static_cast<EDeepLevelBuildingVolumeFace>(FaceIndex);
			const EDeepLevelStreetExposureRule Rule = FDeepLevelBuildingPlacementGeometry::GetExposureRule(
				Definition.PlacementVolume.Exposure,
				Face);
			if (Rule != EDeepLevelStreetExposureRule::Forbidden)
			{
				OutFaces.Add(Face);
			}
		}
		OutFaces.Sort([&Definition](const EDeepLevelBuildingVolumeFace A, const EDeepLevelBuildingVolumeFace B)
		{
			return ExposureScore(FDeepLevelBuildingPlacementGeometry::GetExposureRule(Definition.PlacementVolume.Exposure, A))
				> ExposureScore(FDeepLevelBuildingPlacementGeometry::GetExposureRule(Definition.PlacementVolume.Exposure, B));
		});
	}

	FElementVariant FCatalogModel::MakeElementVariant(
		const FDeepLevelBuildingPlacementDefinition& Definition,
		const int32 BuildingIndex,
		const EDeepLevelBuildingVolumeFace Face)
	{
		const FDeepLevelResolvedBuildingGeometry Geometry = FDeepLevelBuildingPlacementGeometry::ResolveGeometry(Definition, Face);
		FElementVariant Result;
		Result.BuildingIndex = BuildingIndex;
		Result.Face = Face;
		Result.ExposedFaceMask = MakeFaceMask(Face);
		Result.HalfWidth = Geometry.HalfWidth;
		Result.HalfDepth = Geometry.HalfDepth;
		Result.HalfHeight = Definition.PlacementVolume.Extent.Z;
		Result.ExposureScore = ExposureScore(
			FDeepLevelBuildingPlacementGeometry::GetExposureRule(Definition.PlacementVolume.Exposure, Face));
		return Result;
	}

	bool FCatalogModel::AddModuleVariants(
		const TArray<int32>& BuildingIndices,
		const int32 ModuleIndex,
		const double Weight,
		const UDeepLevelBuildingPlacementCatalog& Catalog,
		TArray<FModuleVariant>& OutVariants)
	{
		TArray<TArray<EDeepLevelBuildingVolumeFace>> FacesByElement;
		FacesByElement.SetNum(BuildingIndices.Num());
		for (int32 ElementIndex = 0; ElementIndex < BuildingIndices.Num(); ++ElementIndex)
		{
			GetEligibleFaces(Catalog.Buildings[BuildingIndices[ElementIndex]], FacesByElement[ElementIndex]);
			FacesByElement[ElementIndex].RemoveAll([&Definition = Catalog.Buildings[BuildingIndices[ElementIndex]]](
				const EDeepLevelBuildingVolumeFace Face)
			{
				return !IsSpanFaceValid(Definition, Face);
			});
			if (FacesByElement[ElementIndex].IsEmpty())
			{
				return false;
			}
		}

		for (int32 VariantIndex = 0; VariantIndex < MaximumFaceVariantsPerModule; ++VariantIndex)
		{
			FModuleVariant Variant;
			Variant.ModuleIndex = ModuleIndex;
			Variant.Weight = Weight;
			for (int32 ElementIndex = 0; ElementIndex < BuildingIndices.Num(); ++ElementIndex)
			{
				const TArray<EDeepLevelBuildingVolumeFace>& Faces = FacesByElement[ElementIndex];
				const EDeepLevelBuildingVolumeFace Face = Faces[(VariantIndex + ElementIndex) % Faces.Num()];
				const FElementVariant Element = MakeElementVariant(
					Catalog.Buildings[BuildingIndices[ElementIndex]],
					BuildingIndices[ElementIndex],
					Face);
				Variant.Elements.Add(Element);
				Variant.Width += Element.HalfWidth * 2.0;
				Variant.Signature = HashCombineFast(Variant.Signature, GetTypeHash(static_cast<uint8>(Face)));
			}
			if (!OutVariants.ContainsByPredicate([&Variant](const FModuleVariant& Existing)
			{
				return Existing.ModuleIndex == Variant.ModuleIndex && Existing.Signature == Variant.Signature;
			}))
			{
				OutVariants.Add(MoveTemp(Variant));
			}
		}
		return true;
	}

	bool FCatalogModel::Build(
		const UDeepLevelBuildingPlacementCatalog& Catalog,
		TArray<FModuleVariant>& OutVariants,
		int32& OutModuleCount,
		FText& OutError)
	{
		TMap<FSoftObjectPath, int32> IndexByClass;
		for (int32 BuildingIndex = 0; BuildingIndex < Catalog.Buildings.Num(); ++BuildingIndex)
		{
			const FDeepLevelBuildingPlacementDefinition& Definition = Catalog.Buildings[BuildingIndex];
			const FSoftObjectPath ClassPath = Definition.BuildingClass.ToSoftObjectPath();
			if (ClassPath.IsNull())
			{
				OutError = FText::Format(LOCTEXT("MissingBuildingClass", "Building entry {0} has no class."), FText::AsNumber(BuildingIndex));
				return false;
			}
			if (IndexByClass.Contains(ClassPath))
			{
				OutError = FText::Format(LOCTEXT("DuplicateBuildingClass", "Building class '{0}' appears more than once."), FText::FromString(ClassPath.ToString()));
				return false;
			}
			FText GeometryError;
			if (!FDeepLevelBuildingPlacementGeometry::Validate(Definition, GeometryError))
			{
				OutError = FText::Format(LOCTEXT("InvalidGeometry", "Building '{0}' is invalid: {1}"), FText::FromString(ClassPath.ToString()), GeometryError);
				return false;
			}
			if (Definition.SelectionWeight < 0.0)
			{
				OutError = FText::Format(LOCTEXT("InvalidBuildingWeight", "Building '{0}' has a negative selection weight."), FText::FromString(ClassPath.ToString()));
				return false;
			}
			IndexByClass.Add(ClassPath, BuildingIndex);
			if (Definition.SelectionWeight > 0.0)
			{
				AddModuleVariants({BuildingIndex}, OutModuleCount++, Definition.SelectionWeight, Catalog, OutVariants);
			}
		}

		for (int32 PresetIndex = 0; PresetIndex < Catalog.Presets.Num(); ++PresetIndex)
		{
			const FDeepLevelBuildingSequencePreset& Preset = Catalog.Presets[PresetIndex];
			if (Preset.Name.IsNone() || Preset.Buildings.IsEmpty() || Preset.SelectionWeight < 0.0)
			{
				OutError = FText::Format(LOCTEXT("InvalidPreset", "Preset entry {0} has an invalid name, sequence, or weight."), FText::AsNumber(PresetIndex));
				return false;
			}
			TArray<int32> BuildingIndices;
			for (const TSoftClassPtr<AActor>& BuildingClass : Preset.Buildings)
			{
				const FSoftObjectPath ClassPath = BuildingClass.ToSoftObjectPath();
				const int32* BuildingIndex = IndexByClass.Find(ClassPath);
				if (!BuildingIndex)
				{
					OutError = FText::Format(
						LOCTEXT("PresetClassMissing", "Preset '{0}' references '{1}', which is not in the building catalog."),
						FText::FromName(Preset.Name),
						FText::FromString(ClassPath.ToString()));
					return false;
				}
				BuildingIndices.Add(*BuildingIndex);
			}
			if (Preset.SelectionWeight > 0.0)
			{
				AddModuleVariants(BuildingIndices, OutModuleCount++, Preset.SelectionWeight, Catalog, OutVariants);
			}
		}

		if (OutVariants.IsEmpty())
		{
			OutError = LOCTEXT(
				"NoSelectableModules",
				"Building catalog contains no module whose Required and Forbidden faces are valid on a regular spline span.");
			return false;
		}
		return true;
	}

	FSelectionHistory FCatalogModel::MakeHistory(const int32 BuildingCount, const int32 ModuleCount)
	{
		FSelectionHistory History;
		History.FirstBuildingDistance.Init(TNumericLimits<double>::Max(), BuildingCount);
		History.LastBuildingDistance.Init(-TNumericLimits<double>::Max(), BuildingCount);
		History.FirstModuleDistance.Init(TNumericLimits<double>::Max(), ModuleCount);
		History.LastModuleDistance.Init(-TNumericLimits<double>::Max(), ModuleCount);
		History.LastBuildingFace.Init(EDeepLevelBuildingVolumeFace::NegativeY, BuildingCount);
		History.HasBuildingFace.Init(false, BuildingCount);
		return History;
	}
}

#undef LOCTEXT_NAMESPACE

// ---- DeepLevelBuildingPlacementCatalog ----


#include "PackedLevelActor/PackedLevelActor.h"


#define LOCTEXT_NAMESPACE "DeepLevelBuildingPlacementCatalog"

bool UDeepLevelBuildingPlacementCatalog::ValidateForGeneration(FText& OutError) const
{
	OutError = FText::GetEmpty();
	if (Buildings.IsEmpty())
	{
		OutError = LOCTEXT("EmptyCatalog", "Building catalog contains no buildings.");
		return false;
	}

	TMap<FSoftObjectPath, int32> BuildingIndexByClass;
	bool bHasSelectableModule = false;
	for (int32 BuildingIndex = 0; BuildingIndex < Buildings.Num(); ++BuildingIndex)
	{
		const FDeepLevelBuildingPlacementDefinition& Definition = Buildings[BuildingIndex];
		const FSoftObjectPath ClassPath = Definition.BuildingClass.ToSoftObjectPath();
		if (ClassPath.IsNull())
		{
			OutError = FText::Format(
				LOCTEXT("MissingBuildingClass", "Building entry {0} has no class."),
				FText::AsNumber(BuildingIndex));
			return false;
		}
		if (BuildingIndexByClass.Contains(ClassPath))
		{
			OutError = FText::Format(
				LOCTEXT("DuplicateBuildingClass", "Building class '{0}' appears more than once."),
				FText::FromString(ClassPath.ToString()));
			return false;
		}

		FText GeometryError;
		if (!FDeepLevelBuildingPlacementGeometry::Validate(Definition, GeometryError))
		{
			OutError = FText::Format(
				LOCTEXT("InvalidBuildingGeometry", "Building '{0}' is invalid: {1}"),
				FText::FromString(ClassPath.ToString()),
				GeometryError);
			return false;
		}
		if (Definition.SelectionWeight < 0.0)
		{
			OutError = FText::Format(
				LOCTEXT("NegativeBuildingWeight", "Building '{0}' has a negative selection weight."),
				FText::FromString(ClassPath.ToString()));
			return false;
		}
		if (!Definition.bCalibrated)
		{
			OutError = FText::Format(
				LOCTEXT("UncalibratedBuilding", "Building entry {0} must be calibrated before generation."),
				FText::AsNumber(BuildingIndex));
			return false;
		}

		UClass* BuildingClass = Definition.BuildingClass.LoadSynchronous();
		if (!BuildingClass || !BuildingClass->IsChildOf(APackedLevelActor::StaticClass()))
		{
			OutError = FText::Format(
				LOCTEXT("NotPackedLevelActor", "'{0}' is not a Packed Level Actor class."),
				FText::FromString(ClassPath.ToString()));
			return false;
		}

		BuildingIndexByClass.Add(ClassPath, BuildingIndex);
		bHasSelectableModule |= Definition.SelectionWeight > 0.0;
	}

	TSet<FName> PresetNames;
	for (int32 PresetIndex = 0; PresetIndex < Presets.Num(); ++PresetIndex)
	{
		const FDeepLevelBuildingSequencePreset& Preset = Presets[PresetIndex];
		if (Preset.Name.IsNone())
		{
			OutError = FText::Format(
				LOCTEXT("UnnamedPreset", "Preset entry {0} has no name."),
				FText::AsNumber(PresetIndex));
			return false;
		}
		if (PresetNames.Contains(Preset.Name))
		{
			OutError = FText::Format(
				LOCTEXT("DuplicatePresetName", "Preset name '{0}' appears more than once."),
				FText::FromName(Preset.Name));
			return false;
		}
		if (Preset.Buildings.IsEmpty())
		{
			OutError = FText::Format(
				LOCTEXT("EmptyPreset", "Preset '{0}' contains no buildings."),
				FText::FromName(Preset.Name));
			return false;
		}
		if (Preset.SelectionWeight < 0.0)
		{
			OutError = FText::Format(
				LOCTEXT("NegativePresetWeight", "Preset '{0}' has a negative selection weight."),
				FText::FromName(Preset.Name));
			return false;
		}

		for (const TSoftClassPtr<AActor>& BuildingClass : Preset.Buildings)
		{
			const FSoftObjectPath ClassPath = BuildingClass.ToSoftObjectPath();
			if (!BuildingIndexByClass.Contains(ClassPath))
			{
				OutError = FText::Format(
					LOCTEXT("PresetClassMissingFromCatalog", "Preset '{0}' references '{1}', which is not in the building catalog."),
					FText::FromName(Preset.Name),
					FText::FromString(ClassPath.ToString()));
				return false;
			}
		}

		PresetNames.Add(Preset.Name);
		bHasSelectableModule |= Preset.SelectionWeight > 0.0;
	}

	if (!bHasSelectableModule)
	{
		OutError = LOCTEXT("NoSelectableModules", "Building catalog contains no selectable buildings or presets.");
		return false;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

// ---- DeepLevelBuildingPlacementGeometry ----


#define LOCTEXT_NAMESPACE "DeepLevelBuildingPlacementGeometry"

namespace
{
	FVector FaceNormal(const EDeepLevelBuildingVolumeFace Face)
	{
		switch (Face)
		{
		case EDeepLevelBuildingVolumeFace::PositiveX: return FVector::ForwardVector;
		case EDeepLevelBuildingVolumeFace::NegativeX: return -FVector::ForwardVector;
		case EDeepLevelBuildingVolumeFace::PositiveY: return FVector::RightVector;
		case EDeepLevelBuildingVolumeFace::NegativeY: return -FVector::RightVector;
		default: return -FVector::RightVector;
		}
	}

	FVector FaceTangent(const EDeepLevelBuildingVolumeFace Face)
	{
		return FVector::CrossProduct(FaceNormal(Face), FVector::UpVector);
	}
}

bool FDeepLevelBuildingPlacementGeometry::Validate(const FDeepLevelBuildingPlacementDefinition& Definition, FText& OutError)
{
	const FDeepLevelBuildingPlacementVolume& Volume = Definition.PlacementVolume;
	if (Definition.BuildingClass.IsNull())
	{
		OutError = LOCTEXT("MissingClass", "Building class is missing.");
		return false;
	}
	if (Volume.Extent.GetMin() <= UE_DOUBLE_SMALL_NUMBER || Volume.Extent.ContainsNaN()
		|| Volume.Center.ContainsNaN() || Volume.Rotation.ContainsNaN())
	{
		OutError = LOCTEXT("InvalidVolume", "Placement volume must have finite values and positive extents.");
		return false;
	}
	bool bHasAllowedFace = false;
	for (uint8 Index = 0; Index < 4; ++Index)
	{
		bHasAllowedFace |= GetExposureRule(Volume.Exposure, static_cast<EDeepLevelBuildingVolumeFace>(Index)) != EDeepLevelStreetExposureRule::Forbidden;
	}
	if (!bHasAllowedFace)
	{
		OutError = LOCTEXT("NoStreetFace", "Placement volume has no street-facing eligible surface.");
		return false;
	}
	OutError = FText::GetEmpty();
	return true;
}

EDeepLevelStreetExposureRule FDeepLevelBuildingPlacementGeometry::GetExposureRule(const FDeepLevelBuildingFaceExposureRules& Rules, const EDeepLevelBuildingVolumeFace Face)
{
	switch (Face)
	{
	case EDeepLevelBuildingVolumeFace::PositiveX: return Rules.PositiveX;
	case EDeepLevelBuildingVolumeFace::NegativeX: return Rules.NegativeX;
	case EDeepLevelBuildingVolumeFace::PositiveY: return Rules.PositiveY;
	case EDeepLevelBuildingVolumeFace::NegativeY: return Rules.NegativeY;
	default: return EDeepLevelStreetExposureRule::Forbidden;
	}
}

bool FDeepLevelBuildingPlacementGeometry::ResolveStreetFace(const FDeepLevelBuildingPlacementDefinition& Definition, const int32 Seed, EDeepLevelBuildingVolumeFace& OutFace)
{
	TArray<EDeepLevelBuildingVolumeFace> RequiredFaces;
	TArray<TPair<EDeepLevelBuildingVolumeFace, int32>> WeightedFaces;
	int32 TotalWeight = 0;
	for (uint8 Index = 0; Index < 4; ++Index)
	{
		const EDeepLevelBuildingVolumeFace Face = static_cast<EDeepLevelBuildingVolumeFace>(Index);
		const EDeepLevelStreetExposureRule Rule = GetExposureRule(Definition.PlacementVolume.Exposure, Face);
		if (Rule == EDeepLevelStreetExposureRule::Required) RequiredFaces.Add(Face);
		const int32 Weight = Rule == EDeepLevelStreetExposureRule::Preferred ? 3 : Rule == EDeepLevelStreetExposureRule::Neutral ? 1 : 0;
		if (Weight > 0) { WeightedFaces.Emplace(Face, Weight); TotalWeight += Weight; }
	}
	FRandomStream Random(Seed);
	if (!RequiredFaces.IsEmpty())
	{
		OutFace = RequiredFaces[Random.RandRange(0, RequiredFaces.Num() - 1)];
		return true;
	}
	if (TotalWeight <= 0) return false;
	int32 Choice = Random.RandRange(1, TotalWeight);
	for (const TPair<EDeepLevelBuildingVolumeFace, int32>& Candidate : WeightedFaces)
	{
		Choice -= Candidate.Value;
		if (Choice <= 0) { OutFace = Candidate.Key; return true; }
	}
	return false;
}

int32 FDeepLevelBuildingPlacementGeometry::MakeStreetFaceSeed(
	const int32 Seed,
	const int32 SelectionIndex,
	const int32 ModuleKey,
	const int32 ElementIndex)
{
	return HashCombineFast(HashCombineFast(Seed, SelectionIndex), HashCombineFast(ModuleKey, ElementIndex));
}

FDeepLevelResolvedBuildingGeometry FDeepLevelBuildingPlacementGeometry::ResolveGeometry(const FDeepLevelBuildingPlacementDefinition& Definition, const EDeepLevelBuildingVolumeFace Face)
{
	FDeepLevelResolvedBuildingGeometry Result;
	Result.StreetFace = Face;
	const bool bXFace = Face == EDeepLevelBuildingVolumeFace::PositiveX || Face == EDeepLevelBuildingVolumeFace::NegativeX;
	Result.HalfWidth = bXFace ? Definition.PlacementVolume.Extent.Y : Definition.PlacementVolume.Extent.X;
	Result.HalfDepth = bXFace ? Definition.PlacementVolume.Extent.X : Definition.PlacementVolume.Extent.Y;
	return Result;
}

bool FDeepLevelBuildingPlacementGeometry::ResizeVolumeFace(
	FDeepLevelBuildingPlacementVolume& Volume,
	const EDeepLevelBuildingVolumeFace Face,
	const double Distance)
{
	if (FMath::IsNearlyZero(Distance, UE_DOUBLE_KINDA_SMALL_NUMBER))
	{
		return false;
	}

	const FVector LocalNormal = FaceNormal(Face);
	double* Extent = nullptr;
	if (Face == EDeepLevelBuildingVolumeFace::PositiveX || Face == EDeepLevelBuildingVolumeFace::NegativeX)
	{
		Extent = &Volume.Extent.X;
	}
	else if (Face == EDeepLevelBuildingVolumeFace::PositiveY || Face == EDeepLevelBuildingVolumeFace::NegativeY)
	{
		Extent = &Volume.Extent.Y;
	}
	else
	{
		return false;
	}

	const double PreviousExtent = *Extent;
	const double NewExtent = FMath::Max(PreviousExtent + Distance * 0.5, 1.0);
	const double AppliedDistance = (NewExtent - PreviousExtent) * 2.0;
	if (FMath::IsNearlyZero(AppliedDistance, UE_DOUBLE_KINDA_SMALL_NUMBER))
	{
		return false;
	}

	*Extent = NewExtent;
	Volume.Center += Volume.Rotation.Quaternion().RotateVector(LocalNormal * AppliedDistance * 0.5);
	return true;
}

FTransform FDeepLevelBuildingPlacementGeometry::BuildActorTransform(
	const FDeepLevelBuildingPlacementDefinition& Definition,
	const EDeepLevelBuildingVolumeFace StreetFace,
	const FVector& SplineLocation,
	const FVector& SplineForward,
	const FVector& SplineRight)
{
	const FDeepLevelResolvedBuildingGeometry Geometry = ResolveGeometry(Definition, StreetFace);
	const FVector StreetDirection = -SplineRight;
	const FVector LocalNormal = FaceNormal(StreetFace);
	const FVector LocalTangent = FaceTangent(StreetFace);
	const FVector DesiredTangent = -SplineForward.GetSafeNormal();
	const FQuat DesiredVolumeRotation = FRotationMatrix::MakeFromXY(DesiredTangent, StreetDirection).ToQuat()
		* FRotationMatrix::MakeFromXY(LocalTangent, LocalNormal).ToQuat().Inverse();
	const FVector DesiredCenter = SplineLocation + SplineRight * Geometry.HalfDepth + FVector::UpVector * Definition.PlacementVolume.Extent.Z;
	const FTransform DesiredVolumeTransform(DesiredVolumeRotation, DesiredCenter);
	const FTransform LocalVolumeTransform(Definition.PlacementVolume.Rotation.Quaternion(), Definition.PlacementVolume.Center);
	return LocalVolumeTransform.Inverse() * DesiredVolumeTransform;
}

#undef LOCTEXT_NAMESPACE
