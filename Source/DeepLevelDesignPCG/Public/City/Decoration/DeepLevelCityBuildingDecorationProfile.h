// Copyright <--\, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "City/DeepLevelCityDecoration.h"
#include "Building/DeepLevelBuildingFacadeVisibility.h"
#include "Engine/DataAsset.h"
#include "DeepLevelCityBuildingDecorationProfile.generated.h"

USTRUCT()
struct DEEPLEVELDESIGNPCG_API FDeepLevelCityWindowAppearance
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Appearance")
    FName Name;
    UPROPERTY(EditAnywhere, Category="Appearance")
    TSoftObjectPtr<UMaterialInterface> Material;
    UPROPERTY(EditAnywhere, Category="Appearance", meta=(HideAlphaChannel))
    FLinearColor Color = FLinearColor::White;
    UPROPERTY(EditAnywhere, Category="Appearance", meta=(ClampMin="0"))
    float Brightness = 1.6f;
    UPROPERTY(EditAnywhere, Category="Appearance", meta=(ClampMin="0"))
    float Weight = 1.f;
};

USTRUCT(BlueprintType)
struct DEEPLEVELDESIGNPCG_API FDeepLevelCityBuildingDecorationEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	FDeepLevelCityDecorationRendering Rendering;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FGuid EntryGuid;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decoration")
	FName Name;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decoration", meta = (DisplayName = "Enabled"))
    bool bEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window Lighting", meta = (EditCondition = "Output == EDeepLevelCityDecorationOutput::Mesh", EditConditionHides, DisplayName = "Window Lighting"))
    bool bWindowLight = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window Lighting", meta = (EditCondition = "bWindowLight", EditConditionHides, HideAlphaChannel))
    FLinearColor WindowColor = FLinearColor(1.f, .56f, .26f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window Lighting", meta = (EditCondition = "bWindowLight", EditConditionHides, ClampMin = "0", UIMax = "10", DisplayName = "Brightness"))
    float WindowIntensity = 1.6f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Window Lighting", meta=(EditCondition="bWindowLight", EditConditionHides))
    TSoftObjectPtr<UMaterialInterface> WindowMaterialOverride;

#if WITH_EDITORONLY_DATA
    UPROPERTY()
    FGuid WindowDefinitionGuid;
    UPROPERTY()
    FGuid WindowRoomGuid;
    UPROPERTY()
    FName WindowSource;
    UPROPERTY()
    bool bWindowAutoCandidate = false;
#endif


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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facade")
	bool bAutoFacade = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facade", meta = (EditCondition = "!bAutoFacade", EditConditionHides))
	TArray<EDeepLevelBuildingVolumeFace> Facades;

	bool Validate(FText& Error) const;
};

USTRUCT(BlueprintType)
struct DEEPLEVELDESIGNPCG_API FDeepLevelCityWindowDefinition
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, Category="Identity") FGuid WindowGuid;
    UPROPERTY(EditAnywhere, Category="Window") FName Name;
    UPROPERTY(EditAnywhere, Category="Window") FGuid RoomGuid;
    UPROPERTY(EditAnywhere, Category="Geometry") TSoftObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere, Category="Geometry") FTransform LocalTransform = FTransform::Identity;
    UPROPERTY(EditAnywhere, Category="Rendering") FDeepLevelCityDecorationRendering Rendering;
    UPROPERTY(EditAnywhere, Category="Facade") bool bAutoFacade = true;
    UPROPERTY(EditAnywhere, Category="Facade", meta=(EditCondition="!bAutoFacade",EditConditionHides)) TArray<EDeepLevelBuildingVolumeFace> Facades;
#if WITH_EDITORONLY_DATA
    UPROPERTY() FName WindowSource;
    UPROPERTY() bool bAutoCandidate = false;
#endif
    FDeepLevelCityBuildingDecorationEntry MakeEntry() const;
};

USTRUCT(BlueprintType)
struct DEEPLEVELDESIGNPCG_API FDeepLevelCityWindowState
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, Category="Identity") FGuid EntryGuid;
    UPROPERTY(EditAnywhere, Category="Window") FGuid WindowGuid;
    UPROPERTY(EditAnywhere, Category="Lighting") bool bEnabled = false;
    UPROPERTY(EditAnywhere, Category="Lighting",meta=(HideAlphaChannel)) FLinearColor Color = FLinearColor::White;
    UPROPERTY(EditAnywhere, Category="Lighting",meta=(ClampMin="0")) float Brightness = 1.f;
    UPROPERTY(EditAnywhere, Category="Lighting") TSoftObjectPtr<UMaterialInterface> Material;
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

/** One arrangement within a selection group. Empty entries omit that group. */
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

	// Choose one arrangement per group. None is the original base arrangement.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variant")
	FName SelectionGroup;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variant", meta = (TitleProperty = "Name"))
	TArray<FDeepLevelCityBuildingDecorationEntry> Entries;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Window Lighting")
    TArray<FDeepLevelCityWindowState> WindowStates;

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

    UPROPERTY(EditAnywhere, Category="Window Definitions",meta=(TitleProperty="Name"))
    TArray<FDeepLevelCityWindowDefinition> WindowDefinitions;

    FDeepLevelCityBuildingDecorationVariant BuildVariantView(const FDeepLevelCityBuildingDecorationVariant& Variant) const;
#if WITH_EDITOR
    void ExpandWindowEntries();
    void CompactWindowEntries(FGuid GeometryOwnerVariant = {});
#endif

#if WITH_EDITORONLY_DATA
    UPROPERTY(EditAnywhere, Category = "Window Lighting")
    TSoftObjectPtr<UMaterialInterface> WindowMaterial;
    UPROPERTY(EditAnywhere, Category="Window Lighting", meta=(TitleProperty="Name"))
    TArray<FDeepLevelCityWindowAppearance> WindowAppearances;
#endif

	bool Validate(FText& OutError) const;
	void GetValidationIssues(TArray<FDeepLevelCityBuildingDecorationIssue>& Issues) const;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;

};
