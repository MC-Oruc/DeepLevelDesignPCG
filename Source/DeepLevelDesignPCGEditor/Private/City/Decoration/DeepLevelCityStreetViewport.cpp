#include "City/Decoration/DeepLevelCityStreetViewport.h"
#include "Road/DeepLevelRoadPCG.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Components/LightComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DecalActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "Editor.h"
#include "Slate/SceneViewport.h"

struct HDeepLevelStreetRule final : public HHitProxy
{
    DECLARE_HIT_PROXY();
    FGuid Rule;
    explicit HDeepLevelStreetRule(FGuid Id) : HHitProxy(HPP_World), Rule(Id) {}
};
IMPLEMENT_HIT_PROXY(HDeepLevelStreetRule, HHitProxy);

FDeepLevelStreetViewportClient::FDeepLevelStreetViewportClient(const TSharedRef<FAdvancedPreviewScene>& Scene, const TSharedRef<SEditorViewport>& Widget)
    : FEditorViewportClient(nullptr, &Scene.Get(), Widget), SceneOwner(Scene)
{
    bSetListenerPosition = false;
    SetRealtime(true);
    EngineShowFlags.SetBillboardSprites(false);
}
void FDeepLevelStreetViewportClient::ProcessClick(FSceneView& View, HHitProxy* Proxy, FKey Key, EInputEvent Event, uint32 X, uint32 Y)
{
    if (Proxy && Proxy->IsA(HDeepLevelStreetRule::StaticGetType()) && Key == EKeys::LeftMouseButton)
    {
        if (auto PreviewWidget = EditorViewportWidget.Pin()) StaticCastSharedPtr<SDeepLevelCityStreetViewport>(PreviewWidget)->PickRule(static_cast<HDeepLevelStreetRule*>(Proxy)->Rule);
        return;
    }
    if (Key == EKeys::LeftMouseButton)
    {
        if (Proxy && Proxy->IsA(HActor::StaticGetType()))
            if (auto PreviewWidget = EditorViewportWidget.Pin()) StaticCastSharedPtr<SDeepLevelCityStreetViewport>(PreviewWidget)->PickActor(static_cast<HActor*>(Proxy)->Actor);
        return;
    }
    FEditorViewportClient::ProcessClick(View, Proxy, Key, Event, X, Y);
}
void FDeepLevelStreetViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
    FEditorViewportClient::Draw(View,PDI);
    if (auto PreviewWidget = EditorViewportWidget.Pin()) StaticCastSharedPtr<SDeepLevelCityStreetViewport>(PreviewWidget)->DrawRules(PDI);
}

void SDeepLevelCityStreetViewport::Construct(const FArguments& Args)
{
    RuleSelected = Args._OnRuleSelected;
    Active = Args._IsActive;
    ContextMaterial.Reset(LoadObject<UMaterialInterface>(nullptr,TEXT("/DeepLevelDesignPCG/Editor/Materials/M_DecorationContext.M_DecorationContext")));
    ContextDecalMaterial.Reset(LoadObject<UMaterialInterface>(nullptr,TEXT("/DeepLevelDesignPCG/Editor/Materials/M_DecorationDecalContext.M_DecorationDecalContext")));
    checkf(ContextMaterial.IsValid() && ContextDecalMaterial.IsValid(), TEXT("Decoration preview context materials must be installed with the editor plugin."));
    Scene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
    Scene->SetEnvironmentVisibility(false);
    SEditorViewport::Construct(SEditorViewport::FArguments());
    EditorPreExitHandle = FEditorDelegates::OnEditorPreExit.AddRaw(this, &SDeepLevelCityStreetViewport::ShutdownPreview);
}
SDeepLevelCityStreetViewport::~SDeepLevelCityStreetViewport() { ShutdownPreview(); }
void SDeepLevelCityStreetViewport::ShutdownPreview()
{
    if (bPreviewShutdown) return;
    bPreviewShutdown = true;
    FEditorDelegates::OnEditorPreExit.Remove(EditorPreExitHandle);
    EditorPreExitHandle.Reset();
    Client->SetRealtime(false);
    if (const auto Viewport = GetSceneViewport()) Viewport->SetViewportClient(TStrongPtrVariant<FViewportClient>(nullptr));
    ClearDecorations();
    for (auto Weak : RoadComponents) if (auto* Component=Cast<USceneComponent>(Weak.Get())) { Scene->RemoveComponent(Component); Component->DestroyComponent(); }
    RoadComponents.Reset(); Source.Reset();
    ContextMaterial.Reset(); ContextDecalMaterial.Reset();
    Client.Reset(); Scene.Reset();
}
TSharedRef<FEditorViewportClient> SDeepLevelCityStreetViewport::MakeEditorViewportClient()
{
    Client = MakeShared<FDeepLevelStreetViewportClient>(Scene.ToSharedRef(), SharedThis(this));
    return Client.ToSharedRef();
}
void SDeepLevelCityStreetViewport::ClearDecorations()
{
    for (auto Weak : DecorationActors) if (AActor* Actor = Weak.Get()) Scene->GetWorld()->DestroyActor(Actor);
    DecorationActors.Reset(); Resolved.Reset();
    MeshAppearances.Reset(); DecalAppearances.Reset(); LightAppearances.Reset();
}
void SDeepLevelCityStreetViewport::SetSource(ADeepLevelCityLayoutActor* City, bool bForce)
{
    if (!bForce && Source.Get() == City) return;
    for (auto Weak : RoadComponents) if (auto* Component = Cast<USceneComponent>(Weak.Get())) { Scene->RemoveComponent(Component); Component->DestroyComponent(); }
    RoadComponents.Reset(); RoadBounds = FBox(ForceInit);
    Source = City;
    if (!City) { ClearDecorations(); Client->Invalidate(); return; }
    for (TActorIterator<ADeepLevelRoadNetworkActor> It(City->GetWorld()); It; ++It)
    {
        if (It->GetCityLayoutOwner() != City) continue;
        TArray<UStaticMeshComponent*> Meshes;
        It->GetComponents(Meshes);
        for (auto* Original : Meshes)
        {
            if (!Original->GetStaticMesh() || !Original->IsVisible()) continue;
            auto* Copy = NewObject<UInstancedStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
            Copy->SetStaticMesh(Original->GetStaticMesh());
            Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Copy->SetCanEverAffectNavigation(false);
            for (int32 Slot = 0; Slot < Original->GetNumMaterials(); ++Slot) Copy->SetMaterial(Slot, Original->GetMaterial(Slot));
            Scene->AddComponent(Copy, FTransform::Identity);
            TArray<FTransform> Transforms;
            if (auto* Instances = Cast<UInstancedStaticMeshComponent>(Original))
            {
                for (int32 I = 0; I < Instances->GetInstanceCount(); ++I) { FTransform T; Instances->GetInstanceTransform(I,T,true); Transforms.Add(T); }
            }
            else Transforms.Add(Original->GetComponentTransform());
            Copy->AddInstances(Transforms, false, true, false);
            RoadComponents.Add(Copy);
            RoadBounds += Original->Bounds.GetBox();
        }
    }
    Frame();
}
void SDeepLevelCityStreetViewport::SetPlacements(TConstArrayView<FDeepLevelCityResolvedDecoration> Placements)
{
    ClearDecorations();
    FActorSpawnParameters Params;
    Params.ObjectFlags = RF_Transient;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    for (const auto& Placement : Placements)
    {
        AActor* Actor = nullptr;
        if (Placement.Output == EDeepLevelCityDecorationOutput::Mesh)
        {
            auto* Mesh = Placement.Mesh.LoadSynchronous();
            if (!Mesh) continue;
            Actor = Scene->GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), Placement.Transform, Params);
            auto* Component = NewObject<UInstancedStaticMeshComponent>(Actor);
            Actor->SetRootComponent(Component); Actor->AddInstanceComponent(Component);
            Component->SetStaticMesh(Mesh);
            if (auto* Material = Placement.MaterialOverride.LoadSynchronous()) Component->SetMaterial(0,Material);
            Component->NumCustomDataFloats = Placement.InstanceData.Num();
            Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Component->SetCastShadow(Placement.Rendering.bCastShadow);
            Component->RegisterComponent();
            Actor->SetActorTransform(Placement.Transform);
            const int32 Index = Component->AddInstance(FTransform::Identity);
            if (!Placement.InstanceData.IsEmpty()) Component->SetCustomData(Index,Placement.InstanceData,true);
        }
        else if (Placement.Output == EDeepLevelCityDecorationOutput::Actor)
        {
            if (UClass* Class = Placement.ActorClass.LoadSynchronous()) Actor = Scene->GetWorld()->SpawnActor<AActor>(Class,Placement.Transform,Params);
        }
        else if (auto* Material = Placement.DecalMaterial.LoadSynchronous())
        {
            auto* Decal = Scene->GetWorld()->SpawnActor<ADecalActor>(ADecalActor::StaticClass(),Placement.Transform,Params);
            Decal->SetActorTransform(Placement.Transform); Decal->SetDecalMaterial(Material); Decal->GetDecal()->DecalSize = Placement.DecalSize;
            Actor = Decal;
        }
        if (!Actor) continue;
        Actor->SetActorEnableCollision(false); Actor->SetActorTickEnabled(false);
        TArray<UActorComponent*> Components; Actor->GetComponents(Components);
        for (auto* Component : Components)
        {
            Component->SetComponentTickEnabled(false);
            if (auto* Mesh = Cast<UMeshComponent>(Component))
            {
                FMeshAppearance Appearance;
                Appearance.Component=Mesh; Appearance.Rule=Placement.EntryGuid; Appearance.bCastShadow=Mesh->CastShadow;
                for (int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot) Appearance.Materials.Emplace(Mesh->GetMaterial(Slot));
                MeshAppearances.Add(MoveTemp(Appearance));
            }
            if (auto* Decal = Cast<UDecalComponent>(Component)) DecalAppearances.Add({Decal,TStrongObjectPtr<UMaterialInterface>(Decal->GetDecalMaterial()),Placement.EntryGuid});
            if (auto* Light = Cast<ULightComponent>(Component)) LightAppearances.Add({Light,Placement.EntryGuid,Light->IsVisible()});
        }
        DecorationActors.Add(Actor); Resolved.Add(Placement);
    }
    SetFocusedRules(FocusedRules);
    Client->Invalidate();
}
void SDeepLevelCityStreetViewport::SetFocusedRules(const TSet<FGuid>& Rules)
{
    FocusedRules=Rules;
    for (const auto& Appearance : MeshAppearances)
    {
        if (auto* Mesh=Appearance.Component.Get())
        {
            const bool Focused=IsRuleFocused(Appearance.Rule);
            for(int32 Slot=0;Slot<Appearance.Materials.Num();++Slot) Mesh->SetMaterial(Slot,Focused ? Appearance.Materials[Slot].Get() : ContextMaterial.Get());
            Mesh->SetCastShadow(Focused && Appearance.bCastShadow);
        }
    }
    for(const auto& Appearance:DecalAppearances) if(auto* Decal=Appearance.Component.Get()) Decal->SetDecalMaterial(IsRuleFocused(Appearance.Rule) ? Appearance.Material.Get() : ContextDecalMaterial.Get());
    for(const auto& Appearance:LightAppearances) if(auto* Light=Appearance.Component.Get()) Light->SetVisibility(Appearance.bVisible && IsRuleFocused(Appearance.Rule));
    Client->Invalidate();
}
void SDeepLevelCityStreetViewport::SelectRule(FGuid Rule) { SelectedRule = Rule; Client->Invalidate(); }
void SDeepLevelCityStreetViewport::PickRule(FGuid Rule) { SelectRule(Rule); RuleSelected.ExecuteIfBound(Rule); }
void SDeepLevelCityStreetViewport::PickActor(AActor* Actor)
{
    const int32 Index = DecorationActors.IndexOfByPredicate([Actor](const auto& Candidate) { return Candidate.Get() == Actor; });
    PickRule(Resolved.IsValidIndex(Index) ? Resolved[Index].EntryGuid : FGuid());
}
void SDeepLevelCityStreetViewport::DrawRules(FPrimitiveDrawInterface* PDI) const
{
    for (int32 I = 0; I < Resolved.Num(); ++I)
    {
        const auto& Placement = Resolved[I];
        const FVector P = Placement.Transform.GetLocation() + FVector(0,0,30);
        PDI->SetHitProxy(new HDeepLevelStreetRule(Placement.EntryGuid));
        PDI->DrawPoint(P, Placement.EntryGuid == SelectedRule ? FLinearColor::Yellow : IsRuleFocused(Placement.EntryGuid) ? FLinearColor(.15f,.8f,.45f) : FLinearColor(.2f,.3f,.35f,.3f), IsRuleFocused(Placement.EntryGuid) ? 9 : 4, SDPG_Foreground);
        PDI->SetHitProxy(nullptr);
        if (Placement.EntryGuid == SelectedRule)
        {
            if (const auto* Actor = DecorationActors[I].Get()) DrawWireBox(PDI,Actor->GetComponentsBoundingBox(true),FLinearColor::Yellow,SDPG_Foreground);
        }
    }
}
void SDeepLevelCityStreetViewport::Frame()
{
    FBox Bounds = RoadBounds;
    if (SelectedRule.IsValid())
    {
        FBox Selection(ForceInit);
        for (int32 I=0; I<Resolved.Num(); ++I) if (Resolved[I].EntryGuid == SelectedRule && DecorationActors[I].IsValid()) Selection += DecorationActors[I]->GetComponentsBoundingBox(true);
        if (Selection.IsValid) Bounds = Selection;
    }
    if (Bounds.IsValid && Client) Client->FocusViewportOnBox(Bounds);
    if (Client) Client->Invalidate();
}
void SDeepLevelCityStreetViewport::SetNight(bool bNight)
{
    Scene->SetLightBrightness(bNight ? .15f : 3.f);
    Scene->SetSkyBrightness(bNight ? .08f : 1.f);
    Client->Invalidate();
}

