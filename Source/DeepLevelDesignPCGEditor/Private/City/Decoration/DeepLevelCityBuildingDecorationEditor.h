// Copyright <--\, Inc. All Rights Reserved.
#pragma once

#include "AdvancedPreviewScene.h"
#include "AssetTypeActions_Base.h"
#include "City/Decoration/DeepLevelCityDecorationDocument.h"
#include "City/Decoration/DeepLevelCityDecorationSurface.h"
#include "SEditorViewport.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/GCObject.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STreeView.h"
#include "DeepLevelCityBuildingDecorationEditor.generated.h"

class IDetailsView;
class FUICommandInfo;
class FUICommandList;
class FMenuBuilder;
class SDeepLevelCityBuildingDecorationViewport;
struct FAssetData;

UCLASS()
class UDeepLevelCityDecorationVariantProxy : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Variant")
	FName Name;
	UPROPERTY(EditAnywhere, Category = "Variant", meta = (ClampMin = "0.0"))
	double SelectionWeight = 1.0;
};

UCLASS()
class UDeepLevelCityDecorationEntryProxy : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Decoration", meta = (ShowOnlyInnerProperties))
	FDeepLevelCityBuildingDecorationEntry Entry;
	UPROPERTY()
	bool bHasSymmetry = false;
	UPROPERTY(VisibleAnywhere, Category = "Linked Transform", meta = (EditCondition = "bHasSymmetry", EditConditionHides, HideEditConditionToggle))
	FName Source;
	UPROPERTY(VisibleAnywhere, Category = "Linked Transform", meta = (EditCondition = "bHasSymmetry", EditConditionHides, HideEditConditionToggle))
	FName Target;
	UPROPERTY(EditAnywhere, Category = "Linked Transform", meta = (EditCondition = "bHasSymmetry", EditConditionHides, HideEditConditionToggle))
	FDeepLevelCityDecorationSymmetryPair Symmetry;
};

UCLASS()
class UDeepLevelCityDecorationSelectionProxy : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category = "Selection")
	int32 DecorationCount = 0;
	UPROPERTY(EditAnywhere, Category = "Selection")
	FTransform Transform;
};

UCLASS()
class UDeepLevelCityDecorationBuildingProxy : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category = "Building")
	TSoftClassPtr<AActor> BuildingClass;
	UPROPERTY(VisibleAnywhere, Category = "Building")
	bool bCalibrated = false;
	UPROPERTY(VisibleAnywhere, Category = "Placement")
	FVector Center;
	UPROPERTY(VisibleAnywhere, Category = "Placement")
	FRotator Rotation;
	UPROPERTY(VisibleAnywhere, Category = "Placement")
	FVector Extent;
};

UCLASS()
class UDeepLevelCityDecorationSetProxy : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "City Style")
	TArray<TObjectPtr<UDeepLevelCityDecorationCategory>> Categories;
	UPROPERTY(VisibleAnywhere, Category = "Building Assignments")
	TArray<TObjectPtr<UDeepLevelCityBuildingDecorationProfile>> BuildingProfiles;
};

class FDeepLevelCityDecorationSetAssetActions final : public FAssetTypeActions_Base
{
public:
	explicit FDeepLevelCityDecorationSetAssetActions(EAssetTypeCategories::Type InCategory) : Category(InCategory) {}
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override { return FColor(170, 110, 220); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return Category; }
	virtual void OpenAssetEditor(const TArray<UObject*>& Objects, TSharedPtr<IToolkitHost> Host) override;
private:
	EAssetTypeCategories::Type Category;
};

enum class EDeepLevelCityDecorationOutlineKind : uint8 { Building, Variant, Entry };

/** Read-only presentation identity; document owns selection and all editable records. */
struct FDeepLevelCityDecorationOutlineItem
{
	EDeepLevelCityDecorationOutlineKind Kind = EDeepLevelCityDecorationOutlineKind::Building;
	FSoftObjectPath Building;
	FGuid Variant;
	FGuid Entry;
	FText Label;
	FText Detail;
	FName Icon;
	TArray<TSharedPtr<FDeepLevelCityDecorationOutlineItem>> Children;
	FString Key() const { return Building.ToString() + Variant.ToString() + Entry.ToString(); }
};

struct FDeepLevelCityDecorationViewportShortcut
{
	FText Key;
	FText Action;
};

class FDeepLevelCityBuildingDecorationEditor final : public FAssetEditorToolkit, public FGCObject
{
public:
	virtual ~FDeepLevelCityBuildingDecorationEditor() override;
	void InitEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host, UDeepLevelCityDecorationSet* Set);
	virtual FName GetToolkitFName() const override { return TEXT("DeepLevelCityBuildingDecorationEditor"); }
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitName() const override;
	virtual FText GetToolkitToolTipText() const override;
	virtual bool IncludeAssetInRestoreOpenAssetsPrompt(UObject* Asset) const override;
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("Building Decoration"); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.6f, 0.25f, 0.8f); }
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& Manager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FDeepLevelCityBuildingDecorationEditor"); }

	const FDeepLevelCityBuildingDecorationEntry* GetSelectedEntry() const { return Document->GetEntry(); }
	FGuid GetSelectedEntryId() const { return Document->GetEntryId(); }
	void SelectEntry(const FGuid& Id, bool bToggle = false, bool bAdd = false);
	void SelectSymmetryPivot(const FGuid& Id) { bSetSettings = false; Document->SelectSymmetryPivot(Id); }
	bool IsSymmetryPivotSelected() const { return Document->IsSymmetryPivotSelected(); }
	const TArray<FGuid>& GetSelectedEntryIds() const { return Document->GetSelectedEntryIds(); }
	FTransform GetSelectionTransform() const { return Document->GetSelectionTransform(); }
	TConstArrayView<FDeepLevelCityDecorationSymmetryPair> GetSymmetryPairs() const;
	FVector GetSymmetryCenter(const FDeepLevelCityDecorationSymmetryPair& Pair) const { return Document->GetSymmetryCenter(Pair); }
	void BeginSymmetryPlacement(EAxis::Type Axis = EAxis::None);
	bool UpdateSymmetryPlacement(const FVector& Center, const FSceneView* View = nullptr, bool bFreeAxis = false);
	void ToggleSymmetryPlacementAxis(EAxis::Type Axis);
	void RefreshSymmetryPlacement();
	FQuat GetSymmetryPlacementBasis() const;
	EAxis::Type GetSymmetrySnappedAxis() const { return SymmetrySnappedAxis; }
	FDeepLevelCityDecorationViewportShortcut GetViewportShortcut(int32 Row) const;
	FText GetViewportStatus() const;
	bool CommitSymmetryPlacement();
	void CancelSymmetryPlacement();
	bool IsPlacingSymmetry() const { return SymmetryPlacementAxis.IsSet(); }
	bool HasSymmetryPreview() const { return SymmetryPlacementCenter.IsSet(); }
	FVector GetSymmetryPlacementCenter() const { return SymmetryPlacementCenter.Get(FVector::ZeroVector); }
	FVector GetSymmetryPlacementNormal() const { return SymmetryPlacementNormal; }
	void ApplyWidgetDelta(const FVector& Drag, const FRotator& Rotation, const FVector& Scale, const FHitResult* SurfaceHit = nullptr);
	void EndWidgetDrag() { Document->EndDrag(); }
	void PlaceSelectedOnSurface(const FVector& Location, const FVector& Normal);
	bool AddDroppedAssets(const TArray<FAssetData>& Assets, const FVector& Location, const FVector& Normal);
	bool CanTransformSelection() const { return HasEntry() && !IsPlacingSymmetry(); }
	const FDeepLevelCityDecorationSymmetryPair* GetSelectedSymmetryPair() const { return Document->GetSymmetryPair(); }
	const FDeepLevelCityBuildingDecorationEntry* GetDecoration(const FGuid& Id) const { return Document->GetEntry(Id); }
	bool IsSurfacePlacementEnabled() const;
	void ReportPreviewError(const FText& Error, FGuid Entry = {});
	void ClearPreviewErrors() { PreviewErrors.Reset(); }
	void BindViewportCommands(FUICommandList& Commands);
	TSharedPtr<SWidget> BuildSelectionMenu();

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FDeepLevelCityDecorationToolkitIntegrationTest;
	friend class FDeepLevelCityDecorationSetSelectionTest;
	friend class FDeepLevelCityDecorationOutlineTest;
	friend class FDeepLevelCityDecorationSymmetryPreviewTest;
	friend class FDeepLevelCityDecorationViewportSelectionTest;
	friend class FDeepLevelCityDecorationFlexibleLinkInspectorTest;
	friend class FDeepLevelCityDecorationMultiSelectionPreviewTest;
	friend class FDeepLevelCityDecorationSymmetryPlacementTest;
	friend class FDeepLevelCityDecorationSymmetrySnappingTest;
#endif
	TSharedRef<SDockTab> SpawnWorkspace(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnEnvironment(const FSpawnTabArgs& Args);
	TSharedRef<SWidget> BuildWorkspace();
	TSharedRef<SWidget> BuildViewportControls();
	TSharedRef<SWidget> BuildViewportOverlay();
	void DocumentChanged(EDeepLevelCityDecorationChange Change, FGuid Entry);
	void RefreshOutline();
	void SynchronizeOutlineSelection();
	TSharedRef<SWidget> BuildOutline();
	TSharedRef<SWidget> BuildSetup();
	void BuildSymmetryMenu(FMenuBuilder& Menu);
	void BuildExistingSymmetryMenu(FMenuBuilder& Menu, EAxis::Type Axis);
	void OutlineClicked(TSharedPtr<FDeepLevelCityDecorationOutlineItem> Item);
	void OutlineSelected(TSharedPtr<FDeepLevelCityDecorationOutlineItem> Item, ESelectInfo::Type);
	void OutlineChildren(TSharedPtr<FDeepLevelCityDecorationOutlineItem> Item, TArray<TSharedPtr<FDeepLevelCityDecorationOutlineItem>>& Children) const;
	void OutlineExpansionChanged(TSharedPtr<FDeepLevelCityDecorationOutlineItem> Item, bool bExpanded);
	TSharedRef<ITableRow> OutlineRow(TSharedPtr<FDeepLevelCityDecorationOutlineItem> Item, const TSharedRef<STableViewBase>& Owner);
	FText GetOutlineHint() const;
	void RefreshInspector();
	void RefreshIssues();
	void CommitInspector(const FPropertyChangedEvent& Event);
	void ChooseSet(const FAssetData& Asset);
	void ChooseCatalog(const FAssetData& Asset);
	void AttachProfile(const FAssetData& Asset);
	FReply CreateProfile();
	FReply UnlinkProfile();
	FReply UnlinkIssue(TSharedPtr<FDeepLevelCityDecorationDocumentIssue> Issue);
	bool CanUnlinkProfile() const { return Document->GetSet() && Document->GetProfile(); }
	FReply ShowSetSettings();
	void SetWidgetMode(UE::Widget::EWidgetMode Mode);
	void IssueSelected(TSharedPtr<FDeepLevelCityDecorationDocumentIssue> Item, ESelectInfo::Type);
	void SearchChanged(const FText& Text);
	TSharedRef<ITableRow> IssueRow(TSharedPtr<FDeepLevelCityDecorationDocumentIssue> Item, const TSharedRef<STableViewBase>& Owner);
	FText GetStatusText() const;
	FString GetSetPath() const;
	FString GetCatalogPath() const;
	bool CanEdit() const { return Document->CanEdit() && !IsPlacingSymmetry(); }
	bool HasVariant() const { return CanEdit() && Document->GetVariant(); }
	bool HasEntry() const { return CanEdit() && Document->GetEntry(); }
	bool CanCreateProfile() const { return Document->CanCreateProfile(); }
	bool CanEditInspector() const { return !IsPlacingSymmetry() && (bSetSettings ? Document->GetSet() != nullptr : CanEdit()); }

	TUniquePtr<FDeepLevelCityDecorationDocument> Document;
	TObjectPtr<UDeepLevelCityDecorationVariantProxy> VariantProxy;
	TObjectPtr<UDeepLevelCityDecorationEntryProxy> EntryProxy;
	TObjectPtr<UDeepLevelCityDecorationSelectionProxy> SelectionProxy;
	TObjectPtr<UDeepLevelCityDecorationBuildingProxy> BuildingProxy;
	TObjectPtr<UDeepLevelCityDecorationSetProxy> SetProxy;
	FDeepLevelCityBuildingDecorationEntry InspectorBaseline;
	FTransform SelectionBaseline;
	TSharedPtr<IDetailsView> Inspector;
	TSharedPtr<SDeepLevelCityBuildingDecorationViewport> Preview;
	TArray<TSharedPtr<FDeepLevelCityDecorationOutlineItem>> OutlineItems;
	TSet<FString> ExpandedOutlineItems;
	TSharedPtr<STreeView<TSharedPtr<FDeepLevelCityDecorationOutlineItem>>> Outline;
	TArray<TSharedPtr<FDeepLevelCityDecorationDocumentIssue>> IssueItems;
	TSharedPtr<SListView<TSharedPtr<FDeepLevelCityDecorationDocumentIssue>>> IssueList;
	TMap<FGuid, FText> PreviewErrors;
	FText OperationError;
	FString Search;
	bool bRefreshing = false;
	bool bSetSettings = false;
	bool bShowPlacementGuide = false;
	TOptional<EAxis::Type> SymmetryPlacementAxis;
	TOptional<FVector> SymmetryPlacementCenter;
	FVector SymmetryPlacementNormal = FVector::ZeroVector;
	EAxis::Type SymmetrySnappedAxis = EAxis::None;
	bool bSymmetryFreeAxis = false;
	static const FName WorkspaceTab;
	static const FName EnvironmentTab;
};

class FDeepLevelCityBuildingDecorationViewportClient final : public FEditorViewportClient
{
public:
	FDeepLevelCityBuildingDecorationViewportClient(const TSharedRef<FAdvancedPreviewScene>& Scene, const TSharedRef<SEditorViewport>& Widget,
		TWeakPtr<FDeepLevelCityBuildingDecorationEditor> Editor);
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawCanvas(FViewport& Viewport, FSceneView& View, FCanvas& Canvas) override;
	virtual FVector GetWidgetLocation() const override;
	virtual UE::Widget::EWidgetMode GetWidgetMode() const override;
	virtual bool CanSetWidgetMode(UE::Widget::EWidgetMode NewMode) const override;
	virtual FMatrix GetWidgetCoordSystem() const override;
	virtual FMatrix GetLocalCoordinateSystem() const override { return GetWidgetCoordSystem(); }
	virtual bool InputWidgetDelta(FViewport* Viewport, EAxisList::Type Axis, FVector& Drag, FRotator& Rotation, FVector& Scale) override;
	virtual void TrackingStopped() override;
	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 X, uint32 Y) override;
	virtual void MouseMove(FViewport* Viewport, int32 X, int32 Y) override;
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
	virtual void LostFocus(FViewport* Viewport) override;
	bool TraceBuilding(FSceneView& View, int32 X, int32 Y, FHitResult& Hit) const;
	bool UpdateSymmetryCursor(FSceneView& View, int32 X, int32 Y);
private:
	TSharedRef<FAdvancedPreviewScene> SceneOwner;
	TWeakPtr<FDeepLevelCityBuildingDecorationEditor> Editor;
};

class SDeepLevelCityBuildingDecorationViewport final : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SDeepLevelCityBuildingDecorationViewport) {}
		SLATE_ARGUMENT(TWeakPtr<FDeepLevelCityBuildingDecorationEditor>, Editor)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	virtual ~SDeepLevelCityBuildingDecorationViewport() override;
	void ShutdownPreview();
	void Synchronize(const TSoftClassPtr<AActor>& BuildingClass, const UDeepLevelCityBuildingDecorationProfile* Profile, const FGuid& VariantId);
	void SetPlacementGuide(const FDeepLevelBuildingPlacementDefinition* Definition, bool bShowBounds = true);
	void DrawExposureLabels(const FSceneView* View, FCanvas* Canvas) const;
	AActor* GetEntryActor(const FGuid& Id) const;
	void UpdateEntry(const FGuid& Id, const FTransform& Transform);
	void DrawSelection(FPrimitiveDrawInterface* PDI, TConstArrayView<FGuid> Ids) const;
	bool SetSymmetryPreview(TConstArrayView<FDeepLevelCityBuildingDecorationEntry> Copies);
	void ClearSymmetryPreview();
	int32 GetSymmetryPreviewCount() const { return SymmetryPreviewActors.Num(); }
	AActor* GetSymmetryPreviewActor(const FGuid& Source) const;
	void DrawSymmetryPreview(FPrimitiveDrawInterface* PDI) const;
	void DrawSymmetryAxisLabels(const FSceneView* View, FCanvas* Canvas) const;
	FGuid FindEntryForActor(const AActor* Actor) const;
	AActor* GetBuildingActor() const { return BuildingActor.Get(); }
	bool TraceSurface(const FVector& Start, const FVector& End, FHitResult& Hit) const;
	bool GetSurfacePlacementTransform(const FGuid& Id, const FTransform& Desired, const FVector& Point, const FVector& Normal, FTransform& Result) const;
	void Focus(bool bSelection);
	TSharedRef<FAdvancedPreviewScene> GetPreviewScene() const { return PreviewScene.ToSharedRef(); }
	TSharedPtr<FDeepLevelCityBuildingDecorationViewportClient> GetClient() const { return Client; }
	virtual FReply OnDragOver(const FGeometry& Geometry, const FDragDropEvent& Event) override;
	virtual FReply OnDrop(const FGeometry& Geometry, const FDragDropEvent& Event) override;
protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	virtual void BindCommands() override;
	virtual void OnFocusViewportToSelection() override;
private:
	AActor* Spawn(UClass* Class, const FTransform& Transform);
	static void ApplyActorTransform(AActor* Actor, const FTransform& Transform);
	void ClearDecorations();
	AActor* SpawnEntry(const FDeepLevelCityBuildingDecorationEntry& Entry);
	TSharedPtr<FAdvancedPreviewScene> PreviewScene;
	TSharedPtr<FDeepLevelCityBuildingDecorationViewportClient> Client;
	TWeakPtr<FDeepLevelCityBuildingDecorationEditor> Editor;
	TWeakObjectPtr<AActor> BuildingActor;
	TMap<FGuid, TWeakObjectPtr<AActor>> DecorationActors;
	TMap<FGuid, FDeepLevelCityDecorationSurface> DecorationSurfaces;
	TMap<FGuid, FDeepLevelCityBuildingDecorationEntry> PreviewEntries;
	TMap<FGuid, TWeakObjectPtr<AActor>> SymmetryPreviewActors;
	TArray<FDeepLevelCityBuildingDecorationEntry> SymmetryPreviewEntries;
	TOptional<FDeepLevelBuildingPlacementVolume> PlacementGuide;
	bool bDrawPlacementBounds = true;
	FDeepLevelCityDecorationSurface Surface;
	FDelegateHandle EditorPreExitHandle;
	bool bPreviewShutdown = false;
};
