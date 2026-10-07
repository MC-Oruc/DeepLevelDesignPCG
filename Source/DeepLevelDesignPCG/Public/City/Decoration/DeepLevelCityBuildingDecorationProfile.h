// Copyright <--\, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "City/DeepLevelCityDecoration.h"
#include "Engine/DataAsset.h"
#include "DeepLevelCityBuildingDecorationProfile.generated.h"

USTRUCT(BlueprintType)
struct DEEPLEVELDESIGNPCG_API FDeepLevelCityBuildingDecorationEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FGuid EntryGuid;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decoration")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	EDeepLevelCityDecorationOutput Output = EDeepLevelCityDecorationOutput::Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output", meta = (EditCondition = "Output == EDeepLevelCityDecorationOutput::Mesh", EditConditionHides))
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output", meta = (EditCondition = "Output == EDeepLevelCityDecorationOutput::Actor", EditConditionHides))
	TSoftClassPtr<AActor> ActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output", meta = (EditCondition = "Output == EDeepLevelCityDecorationOutput::Decal", EditConditionHides))
	TSoftObjectPtr<UMaterialInterface> DecalMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output", meta = (ClampMin = "1.0", EditCondition = "Output == EDeepLevelCityDecorationOutput::Decal", EditConditionHides))
	FVector DecalSize = FVector(32.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	FTransform LocalTransform = FTransform::Identity;

	bool Validate(FText& Error) const;
};

/** Persisted authoring relation; cooked variants keep only their baked entries. */
UENUM()
enum class EDeepLevelCityDecorationRotationLink : uint8
{
	Mirror,
	Copy
};

USTRUCT()
struct DEEPLEVELDESIGNPCG_API FDeepLevelCityDecorationSymmetryPair
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid First;
	UPROPERTY()
	FGuid Second;
	UPROPERTY()
	FVector PlaneOrigin = FVector::ZeroVector;
	UPROPERTY()
	FVector PlaneNormal = FVector::RightVector;
	UPROPERTY(EditAnywhere, Category = "Synchronization", meta = (DisplayName = "Mirror Geometry", EditCondition = "bSyncScale", EditConditionHides))
	bool bMirrorGeometry = false;
	UPROPERTY(EditAnywhere, Category = "Synchronization", meta = (DisplayName = "Link Location"))
	bool bSyncLocation = true;
	UPROPERTY(EditAnywhere, Category = "Synchronization", meta = (DisplayName = "Link Rotation"))
	bool bSyncRotation = true;
	UPROPERTY(EditAnywhere, Category = "Synchronization", meta = (DisplayName = "Link Scale"))
	bool bSyncScale = true;
	UPROPERTY(EditAnywhere, Category = "Synchronization", meta = (EditCondition = "bSyncRotation", EditConditionHides))
	EDeepLevelCityDecorationRotationLink RotationMode = EDeepLevelCityDecorationRotationLink::Mirror;
	UPROPERTY(EditAnywhere, Category = "Offsets", meta = (EditCondition = "bSyncLocation", EditConditionHides, Units = "cm", ToolTip = "Target offset in building coordinates."))
	FVector LocationOffset = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category = "Offsets", meta = (EditCondition = "bSyncRotation", EditConditionHides, ToolTip = "Target-local rotation applied after mirrored or copied rotation."))
	FRotator RotationOffset = FRotator::ZeroRotator;
	UPROPERTY(EditAnywhere, Category = "Offsets", meta = (EditCondition = "bSyncScale", EditConditionHides, ToolTip = "Target scale relative to source. Components must be nonzero."))
	FVector ScaleMultiplier = FVector::OneVector;

#if WITH_EDITOR
	FTransform GetMirroredTransform(const FTransform& Transform) const;
	bool HasValidSettings() const;
	FTransform SynchronizeTransform(const FTransform& Changed, const FTransform& Other, bool bFromFirst) const;
	void CaptureOffsets(const FTransform& Source, const FTransform& Target);
#endif
};

/** One complete decoration arrangement. Empty entries define an undecorated variant. */
USTRUCT(BlueprintType)
struct DEEPLEVELDESIGNPCG_API FDeepLevelCityBuildingDecorationVariant
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FGuid VariantGuid;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variant")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variant", meta = (ClampMin = "0.0"))
	double SelectionWeight = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variant", meta = (TitleProperty = "Name"))
	TArray<FDeepLevelCityBuildingDecorationEntry> Entries;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FDeepLevelCityDecorationSymmetryPair> SymmetryPairs;
#endif
#if WITH_EDITOR
	bool HasValidSymmetry() const;
	void RegenerateIdentities();
#endif
};

struct DEEPLEVELDESIGNPCG_API FDeepLevelCityBuildingDecorationIssue
{
	FGuid VariantId;
	FGuid EntryId;
	FText Message;
};

/** City-owned decoration arrangements for one exact building class. */
UCLASS(BlueprintType)
class DEEPLEVELDESIGNPCG_API UDeepLevelCityBuildingDecorationProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Building")
	TSoftClassPtr<AActor> BuildingClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Decoration", meta = (TitleProperty = "Name"))
	TArray<FDeepLevelCityBuildingDecorationVariant> Variants;

	bool Validate(FText& OutError) const;
	void GetValidationIssues(TArray<FDeepLevelCityBuildingDecorationIssue>& Issues) const;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;

};
