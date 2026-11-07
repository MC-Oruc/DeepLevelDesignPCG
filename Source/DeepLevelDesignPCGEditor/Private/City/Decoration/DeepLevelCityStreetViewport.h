#pragma once
#include "AdvancedPreviewScene.h"
#include "City/DeepLevelCityDecoration.h"
#include "SEditorViewport.h"
#include "UObject/StrongObjectPtr.h"
#include "Materials/MaterialInterface.h"

class UMeshComponent;
class UDecalComponent;
class ULightComponent;
class SDeepLevelCityStreetViewport;
class FDeepLevelStreetViewportClient final : public FEditorViewportClient
{
public:
    FDeepLevelStreetViewportClient(const TSharedRef<FAdvancedPreviewScene>& Scene, const TSharedRef<SEditorViewport>& Widget);
    virtual void ProcessClick(FSceneView&, HHitProxy*, FKey, EInputEvent, uint32, uint32) override;
    virtual void Draw(const FSceneView*, FPrimitiveDrawInterface*) override;
private:
    TSharedRef<FAdvancedPreviewScene> SceneOwner;
};

class SDeepLevelCityStreetViewport final : public SEditorViewport
{
public:
    SLATE_BEGIN_ARGS(SDeepLevelCityStreetViewport) {}
        SLATE_EVENT(TDelegate<void(FGuid)>, OnRuleSelected)
        SLATE_ATTRIBUTE(bool, IsActive)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    void SetSource(ADeepLevelCityLayoutActor* City, bool bForce);
    void SetPlacements(TConstArrayView<FDeepLevelCityResolvedDecoration> Placements);
    void SelectRule(FGuid Rule);
    void SetFocusedRules(const TSet<FGuid>& Rules);
    bool IsRuleFocused(FGuid Id) const { return FocusedRules.IsEmpty() || FocusedRules.Contains(Id); }
    void PickRule(FGuid Rule);
    void PickActor(AActor* Actor);
    void DrawRules(FPrimitiveDrawInterface* PDI) const;
    void Frame();
    void SetNight(bool bNight);
    int32 GetRoadComponentCount() const { return RoadComponents.Num(); }
    int32 GetPreviewActorCount() const { return DecorationActors.Num(); }
    virtual ~SDeepLevelCityStreetViewport() override;
    virtual bool IsVisible() const override { return Active.Get(true) && SEditorViewport::IsVisible(); }
protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
    virtual void OnFocusViewportToSelection() override { Frame(); }
private:
    friend class FDeepLevelStreetViewportTest;
    TSharedPtr<FAdvancedPreviewScene> Scene;
    TSharedPtr<FDeepLevelStreetViewportClient> Client;
    TWeakObjectPtr<ADeepLevelCityLayoutActor> Source;
    TArray<TWeakObjectPtr<UActorComponent>> RoadComponents;
    TArray<TWeakObjectPtr<AActor>> DecorationActors;
    TArray<FDeepLevelCityResolvedDecoration> Resolved;
    FBox RoadBounds{ForceInit};
    FGuid SelectedRule;
    TDelegate<void(FGuid)> RuleSelected;
    TAttribute<bool> Active;
    struct FMeshAppearance
    {
        TWeakObjectPtr<UMeshComponent> Component;
        TArray<TStrongObjectPtr<UMaterialInterface>> Materials;
        FGuid Rule;
        bool bCastShadow = false;
    };
    struct FDecalAppearance { TWeakObjectPtr<UDecalComponent> Component; TStrongObjectPtr<UMaterialInterface> Material; FGuid Rule; };
    struct FLightAppearance { TWeakObjectPtr<ULightComponent> Component; FGuid Rule; bool bVisible = false; };
    TArray<FMeshAppearance> MeshAppearances;
    TArray<FDecalAppearance> DecalAppearances;
    TArray<FLightAppearance> LightAppearances;
    TSet<FGuid> FocusedRules;
    TStrongObjectPtr<UMaterialInterface> ContextMaterial;
    TStrongObjectPtr<UMaterialInterface> ContextDecalMaterial;
    void ClearDecorations();
    void ShutdownPreview();
    FDelegateHandle EditorPreExitHandle;
    bool bPreviewShutdown = false;
};
