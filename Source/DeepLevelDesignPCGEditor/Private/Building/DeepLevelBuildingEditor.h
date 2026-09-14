// Copyright <--\, Inc. All Rights Reserved.

#pragma once

#include "AssetTypeActions_Base.h"
#include "CoreMinimal.h"
#include "Building/DeepLevelBuildingPCG.h"
#include "UnrealWidget.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/GCObject.h"
#include "AdvancedPreviewScene.h"
#include "SEditorViewport.h"
#include "SplineComponentVisualizer.h"
#include "Widgets/Views/SListView.h"
#include "DeepLevelBuildingEditor.generated.h"



class FDeepLevelBuildingCatalogAssetTypeActions : public FAssetTypeActions_Base
{
public:
	explicit FDeepLevelBuildingCatalogAssetTypeActions(EAssetTypeCategories::Type InCategory) : Category(InCategory) {}
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override { return FColor(70, 170, 220); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return Category; }
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor) override;
private:
	EAssetTypeCategories::Type Category;
};

class FDeepLevelRoadsideFrontageVisualizer final : public FSplineComponentVisualizer
{
public:
	virtual void DrawVisualizationHUD(
		const UActorComponent* Component,
		const FViewport* Viewport,
		const FSceneView* View,
		FCanvas* Canvas) override;
	virtual bool HandleInputKey(
		FEditorViewportClient* ViewportClient,
		FViewport* Viewport,
		FKey Key,
		EInputEvent Event) override;
};




UENUM()
enum class EDeepLevelPreviewGuideVisibility : uint8
{
	Hidden,
	Visible
};

UCLASS()
class UDeepLevelBuildingCalibrationProxy : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category = "Building", meta = (AllowedClasses = "/Script/Engine.PackedLevelActor"))
	TSoftClassPtr<AActor> BuildingClass;

	UPROPERTY(EditAnywhere, Category = "Placement Volume")
	FVector VolumeCenter = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Placement Volume")
	FRotator VolumeRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, Category = "Placement Volume", meta = (ClampMin = "1.0"))
	FVector VolumeExtent = FVector(500.0);

	UPROPERTY(EditAnywhere, Category = "Street Exposure")
	FDeepLevelBuildingFaceExposureRules Exposure;

	UPROPERTY(EditAnywhere, Category = "Preview")
	EDeepLevelBuildingVolumeFace PreviewStreetFace = EDeepLevelBuildingVolumeFace::NegativeY;

	UPROPERTY(VisibleAnywhere, Category = "Calculated")
	double Width = 0.0;

	UPROPERTY(VisibleAnywhere, Category = "Calculated")
	double Depth = 0.0;

	UPROPERTY(VisibleAnywhere, Category = "Calculated")
	double Height = 0.0;

	UPROPERTY(VisibleAnywhere, Category = "Calculated")
	FVector ActorToVolumePivot = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Guides")
	EDeepLevelPreviewGuideVisibility SideGuides = EDeepLevelPreviewGuideVisibility::Visible;

	UPROPERTY(EditAnywhere, Category = "Guides")
	EDeepLevelPreviewGuideVisibility GroundGuide = EDeepLevelPreviewGuideVisibility::Visible;

	UPROPERTY(EditAnywhere, Category = "Selection", meta = (ClampMin = "0.0"))
	double SelectionWeight = 1.0;

	UPROPERTY(VisibleAnywhere, Category = "Calibration")
	bool bCalibrated = false;
};

UCLASS()
class UDeepLevelBuildingPresetProxy : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Preview")
	int32 PreviewSeed = 1337;

	UPROPERTY(EditAnywhere, Category = "Preset")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Preset", meta = (ClampMin = "0.0"))
	double SelectionWeight = 1.0;

	UPROPERTY(EditAnywhere, Category = "Preset", meta = (AllowedClasses = "/Script/Engine.PackedLevelActor"))
	TArray<TSoftClassPtr<AActor>> Buildings;
};



class IDetailsView;
class FScopedTransaction;
class SDeepLevelBuildingCatalogPreviewViewport;
class UDeepLevelBuildingCalibrationProxy;
class UDeepLevelBuildingPlacementCatalog;
class UDeepLevelBuildingPresetProxy;
enum class EDeepLevelBuildingVolumeFace : uint8;

class FDeepLevelBuildingCatalogEditorToolkit : public FAssetEditorToolkit, public FGCObject
{
public:
	virtual ~FDeepLevelBuildingCatalogEditorToolkit() override;
	void InitEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host, UDeepLevelBuildingPlacementCatalog* InCatalog);
	virtual FName GetToolkitFName() const override { return TEXT("DeepLevelBuildingCatalogEditor"); }
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("Building Catalog"); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.15f, 0.55f, 0.8f); }
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FDeepLevelBuildingCatalogEditorToolkit"); }

	double GetPreviewStride() const;
	FVector GetPreviewWidgetLocation() const;
	void ApplyPreviewWidgetDelta(const FVector& Drag, const FRotator& Rotation, const FVector& Scale, UE::Widget::EWidgetMode WidgetMode);
	void CycleExposureRule(EDeepLevelBuildingVolumeFace Face);
	void BeginPreviewFaceDrag();
	void ResizePreviewFace(EDeepLevelBuildingVolumeFace Face, double Distance);
	void EndPreviewFaceDrag();
	EDeepLevelBuildingVolumeFace GetPreviewStreetFace() const;

private:
	enum class ESection { Buildings, Presets };
	TSharedRef<SWidget> BuildRootWidget();
	TSharedRef<SDockTab> SpawnWorkspaceTab(const FSpawnTabArgs& Args);
	TSharedRef<SWidget> BuildBuildingsWidget();
	TSharedRef<SWidget> BuildPresetsWidget();
	TSharedRef<ITableRow> GenerateBuildingRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& Owner);
	TSharedRef<ITableRow> GeneratePresetRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& Owner);
	TSharedRef<ITableRow> GeneratePaletteRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& Owner);
	TSharedRef<ITableRow> GenerateSequenceRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& Owner);
	void RefreshLists();
	void RefreshSequenceItems();
	void SelectBuilding(int32 Index);
	void SelectPreset(int32 Index);
	void LoadBuildingProxy();
	void LoadPresetProxy();
	void CommitBuildingProxy(const FPropertyChangedEvent& Event);
	void CommitPresetProxy(const FPropertyChangedEvent& Event);
	FReply SwitchSection(ESection NewSection);
	FReply AddBuilding();
	FReply RemoveBuilding();
	FReply AutoFit();
	FReply ResetBuilding();
	FReply AddPreset();
	FReply DuplicatePreset();
	FReply RemovePreset();
	FReply AddPaletteBuilding();
	FReply RemovePresetBuilding();
	FReply MovePresetBuilding(int32 Delta);
	FReply EditPresetBuilding();
	FText GetPresetSummary() const;
	FText GetValidationText() const;
	void RefreshValidation();
	void ModifyCatalog(const FText& TransactionText, TFunctionRef<void()> Mutation);
	void RefreshPreview();
	void OnBuildingSelectionChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo);
	void OnPresetSelectionChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo);
	void OnSequenceSelectionChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo);
	void OnPaletteSelectionChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo);

	TObjectPtr<UDeepLevelBuildingPlacementCatalog> Catalog;
	TObjectPtr<UDeepLevelBuildingCalibrationProxy> BuildingProxy;
	TObjectPtr<UDeepLevelBuildingPresetProxy> PresetProxy;
	ESection Section = ESection::Buildings;
	int32 SelectedBuilding = INDEX_NONE;
	int32 SelectedPreset = INDEX_NONE;
	int32 SelectedPaletteBuilding = INDEX_NONE;
	int32 SelectedPresetBuilding = INDEX_NONE;
	bool bLoadingProxy = false;
	FText CachedValidationText;
	TUniquePtr<FScopedTransaction> FaceDragTransaction;

	TArray<TSharedPtr<int32>> BuildingItems;
	TArray<TSharedPtr<int32>> PresetItems;
	TArray<TSharedPtr<int32>> PaletteItems;
	TArray<TSharedPtr<int32>> SequenceItems;
	TSharedPtr<SListView<TSharedPtr<int32>>> BuildingList;
	TSharedPtr<SListView<TSharedPtr<int32>>> PresetList;
	TSharedPtr<SListView<TSharedPtr<int32>>> PaletteList;
	TSharedPtr<SListView<TSharedPtr<int32>>> SequenceList;
	TSharedPtr<SWidgetSwitcher> SectionSwitcher;
	TSharedPtr<IDetailsView> BuildingDetails;
	TSharedPtr<IDetailsView> PresetDetails;
	TSharedPtr<SDeepLevelBuildingCatalogPreviewViewport> BuildingPreviewViewport;
	TSharedPtr<SDeepLevelBuildingCatalogPreviewViewport> PresetPreviewViewport;
	static const FName WorkspaceTabId;
};



class AActor;
class FDeepLevelBuildingCatalogEditorToolkit;
class UDeepLevelBuildingPlacementCatalog;
enum class EDeepLevelPreviewGuideVisibility : uint8;

struct HDeepLevelBuildingFaceProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();
	EDeepLevelBuildingVolumeFace Face;
	HDeepLevelBuildingFaceProxy(EDeepLevelBuildingVolumeFace InFace) : HHitProxy(HPP_UI), Face(InFace) {}
};
class FDeepLevelBuildingCatalogViewportClient : public FEditorViewportClient
{
public:
	FDeepLevelBuildingCatalogViewportClient(FAdvancedPreviewScene& InScene, const TSharedRef<SEditorViewport>& InViewport, TWeakPtr<FDeepLevelBuildingCatalogEditorToolkit> InToolkit);
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual FVector GetWidgetLocation() const override;
	virtual bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale) override;
	virtual FMatrix GetWidgetCoordSystem() const override;
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
	virtual bool InputAxis(const FInputKeyEventArgs& EventArgs) override;
	virtual void MouseMove(FViewport* InViewport, int32 X, int32 Y) override;
	virtual void CapturedMouseMove(FViewport* InViewport, int32 X, int32 Y) override;
	virtual void ProcessClick(class FSceneView& View, class HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;
	virtual void DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) override;
private:
	double ProjectCursorOntoDragAxis();

	TWeakPtr<FDeepLevelBuildingCatalogEditorToolkit> Toolkit;
	TOptional<EDeepLevelBuildingVolumeFace> DraggedFace;
	FVector DragAxis = FVector::ZeroVector;
	FVector DragPlaneOrigin = FVector::ZeroVector;
	FVector DragPlaneNormal = FVector::ZeroVector;
	double PreviousDragDistance = 0.0;
	bool bFaceDragMoved = false;
};

class SDeepLevelBuildingCatalogPreviewViewport : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SDeepLevelBuildingCatalogPreviewViewport) {}
		SLATE_ARGUMENT(TWeakPtr<FDeepLevelBuildingCatalogEditorToolkit>, Toolkit)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	virtual ~SDeepLevelBuildingCatalogPreviewViewport() override;
	void PreviewBuilding(const UDeepLevelBuildingPlacementCatalog& Catalog, int32 BuildingIndex);
	void SetPreviewStreetFace(EDeepLevelBuildingVolumeFace Face) { PreviewStreetFace = Face; }
	void SetGuideVisibility(EDeepLevelPreviewGuideVisibility InSide, EDeepLevelPreviewGuideVisibility InGround);
	void PreviewPreset(const UDeepLevelBuildingPlacementCatalog& Catalog, int32 PresetIndex, int32 Seed);
	void ClearPreview();
	bool AutoFitVolume(UClass* BuildingClass, const FRotator& VolumeRotation, FVector& OutCenter, FVector& OutExtent);
	FVector GetPrimaryLocation() const;
	FVector GetVolumeCenterWorld() const;
	FVector GetVolumePivotWorld() const;
	FRotator GetVolumeRotationWorld() const;
	FVector GetFaceCenterWorld(EDeepLevelBuildingVolumeFace Face) const;
	FVector GetFaceNormalWorld(EDeepLevelBuildingVolumeFace Face) const;
	double GetGuideStride() const { return GuideStride; }
	void DrawCalibration(FPrimitiveDrawInterface* PDI) const;
	void DrawLabels(const FSceneView* View, FCanvas* Canvas) const;

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	AActor* SpawnBuilding(UClass* BuildingClass, const FTransform& Transform);
	void SpawnDefinition(const FDeepLevelBuildingPlacementDefinition& Definition, const FTransform& Transform, bool bPrimary);
	FTransform MakeTransform(const FDeepLevelBuildingPlacementDefinition& Definition, double Cursor) const;

	TSharedPtr<FAdvancedPreviewScene> PreviewScene;
	TSharedPtr<FDeepLevelBuildingCatalogViewportClient> ViewportClient;
	TWeakPtr<FDeepLevelBuildingCatalogEditorToolkit> Toolkit;
	TArray<TWeakObjectPtr<AActor>> PreviewActors;
	TWeakObjectPtr<AActor> PrimaryActor;
	double GuideStride = 0.0;
	FTransform PreviewVolumeTransform;
	FVector PreviewVolumeExtent = FVector::ZeroVector;
	FDeepLevelBuildingFaceExposureRules PreviewExposure;
	EDeepLevelBuildingVolumeFace PreviewStreetFace = EDeepLevelBuildingVolumeFace::NegativeY;
	EDeepLevelPreviewGuideVisibility SideGuideVisibility;
	EDeepLevelPreviewGuideVisibility GroundGuideVisibility;
	bool bHasPreviewVolume = false;
};
