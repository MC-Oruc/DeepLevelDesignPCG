// Copyright <--\, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "AssetTypeActions_Base.h"
#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "SplineComponentVisualizer.h"
#include "ScopedTransaction.h"
#include "Road/DeepLevelRoadPCG.h"
#include "UnrealWidget.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/GCObject.h"
#include "AdvancedPreviewScene.h"
#include "SEditorViewport.h"
#include "Widgets/Views/SListView.h"
#include "DeepLevelRoadEditor.generated.h"




class ADeepLevelRoadNetworkActor;

class FDeepLevelRoadNetworkActorDetails final : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FReply AddRoadBranch();

	TWeakObjectPtr<ADeepLevelRoadNetworkActor> Network;
};



class ADeepLevelRoadNetworkActor;
class UDeepLevelRoadSplineComponent;

class FDeepLevelRoadSplineAuthoringService final
{
public:
	static UDeepLevelRoadSplineComponent* AddBranch(ADeepLevelRoadNetworkActor& Network, FText& OutError);
};

class FDeepLevelRoadEditorRefreshService final
{
public:
	static void Initialize();
	static void Shutdown();

private:
	static void HandleRoadNetworkChanged(ADeepLevelRoadNetworkActor& Network, EDeepLevelRoadNetworkChange Change);
	static FDelegateHandle ChangeHandle;
};

class FScopedTransaction;

class FDeepLevelRoadSplineComponentVisualizer final : public FSplineComponentVisualizer, public IInputProcessor
{
public:
	virtual ~FDeepLevelRoadSplineComponentVisualizer() override;
	virtual bool ShouldShowForSelectedSubcomponents(const UActorComponent* Component) override;
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	virtual void Tick(float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual const TCHAR* GetDebugName() const override { return TEXT("DeepLevel Road Grid Authoring"); }

	static FVector SnapWorldToGrid(const FVector& WorldLocation, const FVector& GridOrigin, double GridSize);
	static void SimplifyGridPath(const TArray<FVector>& GridPath, TArray<FVector>& OutSplinePoints);
	static void ComposeReshapedGridPath(
		const TArray<FVector>& FixedPath,
		const TArray<FVector>& DragPath,
		TArray<FVector>& OutPath);
	static int32 FindSplineEndpointAtGridCell(
		const UDeepLevelRoadSplineComponent& Spline,
		const FVector& GridCell,
		const FVector& GridOrigin,
		double GridSize);

private:
	ADeepLevelRoadNetworkActor* GetSelectedRoadNetwork() const;
	bool UpdateHoveredCell();
	bool FindEndpointAtHoveredCell(ADeepLevelRoadNetworkActor& Network, UDeepLevelRoadSplineComponent*& OutSpline, int32& OutPointIndex) const;
	void AppendHoveredCellToPath();
	void BuildReplacementPath(TArray<FVector>& OutPath) const;
	bool DoesDragPathOverlapExistingRoad() const;
	void ShowOverlapWarning() const;
	bool ToggleHoveredCellRemoval();
	bool SetHoveredCellOverride(EDeepLevelRoadCellOverrideMode Mode, bool bRequireSelectedMesh);
	bool ClearHoveredCellOverride();
	void ApplyDrag();
	void FinishDrag();
	void ResetDrag();

	TWeakObjectPtr<ADeepLevelRoadNetworkActor> HoveredNetwork;
	FVector HoveredCell = FVector::ZeroVector;
	bool bHasHoveredCell = false;
	bool bDragging = false;
	bool bCreatedSpline = false;
	bool bEditingSplineEndpoint = false;
	bool bReplaceSplineStart = false;
	TWeakObjectPtr<ADeepLevelRoadNetworkActor> DraggedNetwork;
	TWeakObjectPtr<UDeepLevelRoadSplineComponent> DraggedSpline;
	int32 EditedSegmentIndex = INDEX_NONE;
	FVector DragAnchor = FVector::ZeroVector;
	TArray<FVector> DragPath;
	bool bDragOverlapsExistingRoad = false;
	TUniquePtr<FScopedTransaction> DragTransaction;
};



class FDeepLevelRoadTileCatalogAssetTypeActions final : public FAssetTypeActions_Base
{
public:
	explicit FDeepLevelRoadTileCatalogAssetTypeActions(EAssetTypeCategories::Type InCategory) : Category(InCategory) {}
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override { return FColor(40, 180, 100); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return Category; }
	virtual void OpenAssetEditor(const TArray<UObject*>& Objects, TSharedPtr<IToolkitHost> EditWithinLevelEditor) override;

private:
	EAssetTypeCategories::Type Category;
};




FText GetRoadTileTypeDisplayName(const FDeepLevelRoadTileDefinition& Tile);

UCLASS()
class UDeepLevelRoadTileEditorProxy : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "Tile")
	TSoftObjectPtr<UStaticMesh> TileMesh;

	UPROPERTY(EditAnywhere, Category = "Tile")
	TSoftObjectPtr<UMaterialInterface> TileMaterialOverride;

	UPROPERTY(EditAnywhere, Category = "Placement Volume")
	FVector VolumeCenter = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Placement Volume")
	FRotator VolumeRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, Category = "Placement Volume", meta = (ClampMin = "1.0", UIMin = "1.0"))
	double VolumeHeight = 100.0;

	UPROPERTY(VisibleAnywhere, Category = "Placement Volume")
	double TileSize = 500.0;

	UPROPERTY(EditAnywhere, Category = "Selection", meta = (ClampMin = "0.01", UIMin = "0.01"))
	double SelectionWeight = 1.0;

	UPROPERTY(VisibleAnywhere, Category = "Calibration")
	bool bCalibrated = false;
};



class IDetailsView;
class SDeepLevelRoadTileCatalogPreviewViewport;
class UDeepLevelRoadTileCatalog;
class UDeepLevelRoadTileEditorProxy;

class FDeepLevelRoadTileCatalogEditorToolkit final : public FAssetEditorToolkit, public FGCObject
{
public:
	virtual ~FDeepLevelRoadTileCatalogEditorToolkit() override;
	void InitEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host, UDeepLevelRoadTileCatalog* InCatalog);
	virtual FName GetToolkitFName() const override { return TEXT("DeepLevelRoadTileCatalogEditor"); }
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("Road Tile Catalog"); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.1f, 0.6f, 0.35f); }
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FDeepLevelRoadTileCatalogEditorToolkit"); }
	FVector GetPreviewWidgetLocation() const;
	void ApplyPreviewWidgetDelta(const FVector& Drag, const FRotator& Rotation, const FVector& Scale, UE::Widget::EWidgetMode WidgetMode);
	void CycleRoadConnection(int32 ConnectionMask);

private:
	TSharedRef<SDockTab> SpawnWorkspaceTab(const FSpawnTabArgs& Args);
	TSharedRef<SWidget> BuildWorkspace();
	TSharedRef<ITableRow> GenerateTileRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& Owner);
	void RefreshItems();
	void SelectTile(int32 Index);
	void LoadProxy();
	void CommitProxy(const FPropertyChangedEvent& Event);
	void RefreshPreview();
	void RefreshValidation();
	void ModifyCatalog(const FText& TransactionText, TFunctionRef<void()> Mutation);
	FReply AddTile();
	FReply RemoveTile();
	FReply AutoFit();
	FReply ResetTile();
	FText GetValidationText() const;
	void OnSelectionChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo);

	TObjectPtr<UDeepLevelRoadTileCatalog> Catalog;
	TObjectPtr<UDeepLevelRoadTileEditorProxy> Proxy;
	int32 SelectedTile = INDEX_NONE;
	bool bLoadingProxy = false;
	FText ValidationText;
	TArray<TSharedPtr<int32>> TileItems;
	TSharedPtr<SListView<TSharedPtr<int32>>> TileList;
	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<SDeepLevelRoadTileCatalogPreviewViewport> PreviewViewport;
	static const FName WorkspaceTabId;
};



class AStaticMeshActor;
class FDeepLevelRoadTileCatalogEditorToolkit;
class UStaticMesh;
class UMaterialInterface;

struct HDeepLevelRoadConnectionProxy final : public HHitProxy
{
	DECLARE_HIT_PROXY();

	explicit HDeepLevelRoadConnectionProxy(const int32 InConnectionMask)
		: HHitProxy(HPP_UI), ConnectionMask(InConnectionMask)
	{
	}

	int32 ConnectionMask = 0;
};

class FDeepLevelRoadTileCatalogViewportClient final : public FEditorViewportClient
{
public:
	FDeepLevelRoadTileCatalogViewportClient(FAdvancedPreviewScene& Scene, const TSharedRef<SEditorViewport>& Viewport, TWeakPtr<FDeepLevelRoadTileCatalogEditorToolkit> Toolkit);
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual FVector GetWidgetLocation() const override;
	virtual bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rotation, FVector& Scale) override;
	virtual FMatrix GetWidgetCoordSystem() const override;
	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;
	virtual void DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) override;

private:
	TWeakPtr<FDeepLevelRoadTileCatalogEditorToolkit> Toolkit;
};

class SDeepLevelRoadTileCatalogPreviewViewport final : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SDeepLevelRoadTileCatalogPreviewViewport) {}
		SLATE_ARGUMENT(TWeakPtr<FDeepLevelRoadTileCatalogEditorToolkit>, Toolkit)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	virtual ~SDeepLevelRoadTileCatalogPreviewViewport() override;
	void PreviewTile(const FDeepLevelRoadTileDefinition* Definition, double GridCellSize);
	bool AutoFitVolume(UStaticMesh* TileMesh, double GridCellSize, FVector& OutCenter, FVector& OutExtent);
	void DrawTileGuides(FPrimitiveDrawInterface* PDI) const;
	void DrawPortLabels(const FSceneView* View, FCanvas* Canvas) const;
	FVector GetVolumeCenterWorld() const;
	FRotator GetVolumeRotationWorld() const;

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	void ClearPreview();
	AStaticMeshActor* SpawnTile(UStaticMesh* TileMesh, UMaterialInterface* TileMaterialOverride);

	TSharedPtr<FAdvancedPreviewScene> PreviewScene;
	TSharedPtr<FDeepLevelRoadTileCatalogViewportClient> ViewportClient;
	TWeakPtr<FDeepLevelRoadTileCatalogEditorToolkit> Toolkit;
	TWeakObjectPtr<AStaticMeshActor> PreviewActor;
	FDeepLevelRoadTileDefinition PreviewDefinition;
	double PreviewGridCellSize = 500.0;
	bool bHasDefinition = false;
};
