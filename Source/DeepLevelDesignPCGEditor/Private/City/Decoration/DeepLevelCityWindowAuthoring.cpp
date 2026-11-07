#include "City/Decoration/DeepLevelCityWindowAuthoring.h"
#include "City/Decoration/DeepLevelCityWindowGeometry.h"
#include "GeometryScript/MeshDecompositionFunctions.h"
#include "GeometryScript/MeshBasicEditFunctions.h"
#include "City/Decoration/DeepLevelCityBuildingDecorationEditor.h"
#include "City/Decoration/DeepLevelCityDecorationDocument.h"
#include "City/Decoration/DeepLevelCityDecorationAuthoring.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "PreviewScene.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "StaticMeshResources.h"
#include "UObject/StrongObjectPtr.h"
#include "UDynamicMesh.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "GeometryScript/MeshMaterialFunctions.h"
#include "GeometryScript/CreateNewAssetUtilityFunctions.h"

#define LOCTEXT_NAMESPACE "DeepLevelCityWindows"
FVector DeepLevelCityWindows::CenterGlass(UDynamicMesh* Glass)
{
    FBox Bounds(ForceInit);
    Glass->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& Mesh) { for (int32 Vertex : Mesh.VertexIndicesItr()) Bounds += FVector(Mesh.GetVertex(Vertex)); });
    const FVector Center = Bounds.IsValid ? Bounds.GetCenter() : FVector::ZeroVector;
    Glass->EditMesh([&](UE::Geometry::FDynamicMesh3& Mesh) { for (int32 Vertex : Mesh.VertexIndicesItr()) Mesh.SetVertex(Vertex, Mesh.GetVertex(Vertex) - FVector3d(Center)); });
    return Center;
}

TArray<TStrongObjectPtr<UDynamicMesh>> DeepLevelCityWindows::GroupGlass(UDynamicMesh* Glass)
{
    TArray<UDynamicMesh*> Pieces;
    UGeometryScriptLibrary_MeshDecompositionFunctions::SplitMeshByVertexOverlap(Glass, Pieces, nullptr, .01);
    struct FPane { TStrongObjectPtr<UDynamicMesh> Mesh; FBox Bounds; FVector Normal; };
    TArray<FPane> Panes;
    for (auto* Piece : Pieces)
    {
        FPane Pane{TStrongObjectPtr<UDynamicMesh>(Piece), FBox(ForceInit), FVector::ZeroVector};
        Piece->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& Mesh)
        {
            for (int32 Vertex : Mesh.VertexIndicesItr()) Pane.Bounds += FVector(Mesh.GetVertex(Vertex));
            for (int32 Triangle : Mesh.TriangleIndicesItr()) { Pane.Normal = FVector(Mesh.GetTriNormal(Triangle)); break; }
        });
        if (Pane.Bounds.IsValid) Panes.Add(MoveTemp(Pane));
    }
    TArray<int32> Parent;
    for (int32 I = 0; I < Panes.Num(); ++I) Parent.Add(I);
    auto Root = [&Parent](int32 I) { while (Parent[I] != I) I = Parent[I]; return I; };
    for (int32 A = 0; A < Panes.Num(); ++A)
    for (int32 B = A + 1; B < Panes.Num(); ++B)
    {
        const auto& P = Panes[A]; const auto& Q = Panes[B];
        const FVector Delta = Q.Bounds.GetCenter() - P.Bounds.GetCenter();
        if (FMath::Abs(P.Normal.Z) > .25f || FMath::Abs(Q.Normal.Z) > .25f) continue;

        const float DotNormals = FVector::DotProduct(P.Normal, Q.Normal);
        const double HeightA = P.Bounds.GetSize().Z, HeightB = Q.Bounds.GetSize().Z;

        // Coplanar stacked or side-by-side panes on the same flat facade plane
        if (DotNormals >= .98f && FMath::Abs(FVector::DotProduct(Delta, P.Normal)) <= 4.0)
        {
            const FVector Axis = FVector::CrossProduct(FVector::UpVector, P.Normal).GetSafeNormal().GetAbs();
            const double WidthA = FVector::DotProduct(Axis, P.Bounds.GetSize());
            const double WidthB = FVector::DotProduct(Axis, Q.Bounds.GetSize());
            const double WidthDelta = FMath::Abs(FVector::DotProduct(Delta, FVector::CrossProduct(FVector::UpVector, P.Normal).GetSafeNormal()));

            const bool bVertical = WidthDelta <= FMath::Max(8., FMath::Min(WidthA, WidthB) * .12)
                && FMath::Max(WidthA, WidthB) <= FMath::Min(WidthA, WidthB) * 1.35
                && FMath::Abs(Delta.Z) - (HeightA + HeightB) * .5 <= 28.0;
            const bool bHorizontal = FMath::Abs(Delta.Z) <= FMath::Max(8., FMath::Min(HeightA, HeightB) * .12)
                && FMath::Max(HeightA, HeightB) <= FMath::Min(HeightA, HeightB) * 1.35
                && WidthDelta - (WidthA + WidthB) * .5 <= 12.0;
            if (bVertical || bHorizontal) { Parent[Root(B)] = Root(A); continue; }
        }

        // Curved bay window: genuinely angled adjacent facets around a polygonal/cylindrical bay on the SAME floor
        if (DotNormals >= .80f && DotNormals < .98f && FMath::Abs(Delta.Z) <= 10.0
            && FMath::Max(HeightA, HeightB) <= FMath::Min(HeightA, HeightB) * 1.35)
        {
            const double WidthA = P.Bounds.GetSize().Size2D();
            const double WidthB = Q.Bounds.GetSize().Size2D();
            const double Dist2D = Delta.Size2D();
            if (Dist2D <= (WidthA + WidthB) * .5 + 15.0)
            {
                Parent[Root(B)] = Root(A);
                continue;
            }
        }
    }
    TMap<int32, int32> Groups;
    TArray<TStrongObjectPtr<UDynamicMesh>> Result;
    for (int32 I = 0; I < Panes.Num(); ++I)
    {
        const int32 R = Root(I);
        if (!Groups.Contains(R)) { Groups.Add(R, Result.Num()); Result.Emplace(NewObject<UDynamicMesh>()); }
        UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(Result[Groups[R]].Get(), Panes[I].Mesh.Get(), FTransform::Identity);
    }
    return Result;
}

namespace
{
    const FName WindowGroup(TEXT("NightWindows"));
    FGuid StableWindowId(const FString& Key)
    {
        FGuid Id;
        FGuid::ParseExact(FMD5::HashAnsiString(*Key), EGuidFormats::Digits, Id);
        return Id;
    }
    bool IsGlazing(UMaterialInterface* Material)
    {
        if (!Material || Material->GetBlendMode() != BLEND_Translucent) return false;
        const FString Name = Material->GetName();
        return Name.Contains(TEXT("Glass")) && !Name.Contains(TEXT("Door")) && !Name.Contains(TEXT("Entrance"));
    }
    bool CollectWindows(UClass* Class, const FString& AssetFolder, UMaterialInterface* Material,
        const double GroundZ, TArray<FDeepLevelCityBuildingDecorationEntry>& Entries, FText& Error)
    {
        FPreviewScene Scene{FPreviewScene::ConstructionValues()};
        FActorSpawnParameters Spawn;
        Spawn.ObjectFlags |= RF_Transient;
        AActor* Building = Scene.GetWorld()->SpawnActor<AActor>(Class, FTransform::Identity, Spawn);
        if (!Building) { Error = LOCTEXT("Spawn", "Cannot create the building preview."); return false; }
        TArray<UStaticMeshComponent*> Components;
        Building->GetComponents(Components);
        for (UStaticMeshComponent* Component : Components)
        {
            UStaticMesh* Source = Component->GetStaticMesh();
            if (!Source) continue;
            TArray<int32> GlassSlots;
            for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
                if (IsGlazing(Component->GetMaterial(Slot))) GlassSlots.Add(Slot);
            if (GlassSlots.IsEmpty()) continue;
            TStrongObjectPtr<UDynamicMesh> Glass(NewObject<UDynamicMesh>());
            EGeometryScriptOutcomePins Outcome;
            FGeometryScriptMeshReadLOD LOD;
            UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMesh(Source, Glass.Get(), {}, LOD, Outcome);
            if (Outcome != EGeometryScriptOutcomePins::Success) { Error = LOCTEXT("Copy", "Cannot read source window geometry."); return false; }
            TArray<UMaterialInterface*> Materials;
            TArray<int32> Slots;
            TArray<FName> Names;
            UGeometryScriptLibrary_StaticMeshFunctions::GetSectionMaterialListFromStaticMesh(Source, LOD, Materials, Slots, Names, Outcome);
            if (Outcome != EGeometryScriptOutcomePins::Success) return false;
            FString Key = Source->GetPathName() + Material->GetPathName() + Source->GetRenderData()->DerivedDataKey;
            for (int32 Section = 0; Section < Slots.Num(); ++Section)
            {
                if (GlassSlots.Contains(Slots[Section])) { Key += FString::FromInt(Section) + TEXT(";"); }
                else { int32 Deleted = 0; UGeometryScriptLibrary_MeshMaterialFunctions::DeleteTrianglesByMaterialID(Glass.Get(), Section, Deleted); }
            }
            bool bEmpty = true;
            FBox Bounds(ForceInit);
            Glass->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& Mesh)
            {
                bEmpty = Mesh.TriangleCount() == 0;
                for (const int32 Vertex : Mesh.VertexIndicesItr()) Bounds += FVector(Mesh.GetVertex(Vertex));
            });
            if (bEmpty) continue;
            auto Assemblies = DeepLevelCityWindows::GroupGlass(Glass.Get());
            for (int32 AssemblyIndex = 0; AssemblyIndex < Assemblies.Num(); ++AssemblyIndex)
            {
            UDynamicMesh* Assembly = Assemblies[AssemblyIndex].Get();
            FBox AssemblyBounds(ForceInit);
            Assembly->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& Mesh)
            { for (int32 Vertex : Mesh.VertexIndicesItr()) AssemblyBounds += FVector(Mesh.GetVertex(Vertex)); });
            const FVector Center = DeepLevelCityWindows::CenterGlass(Assembly);
            const FBox CenteredBounds(AssemblyBounds.Min - Center, AssemblyBounds.Max - Center);
            const FString AssemblyKey = TEXT("CenteredWindow_v3:") + Key + FString::FromInt(AssemblyIndex);
            const FString Path = AssetFolder / (TEXT("SM_Window_") + Source->GetName() + TEXT("_") + FMD5::HashAnsiString(*AssemblyKey).Left(12));
            UStaticMesh* Output = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
            if (!Output)
            {
                UGeometryScriptLibrary_MeshMaterialFunctions::ClearMaterialIDs(Assembly);
                FGeometryScriptCreateNewStaticMeshAssetOptions Options;
                Options.bEnableCollision = false;
                Options.bEnableNanite = false;
                Output = UGeometryScriptLibrary_CreateNewAssetFunctions::CreateNewStaticMeshAssetFromMesh(Assembly, Path, Options, Outcome);
                if (!Output || Outcome != EGeometryScriptOutcomePins::Success) { Error = LOCTEXT("Mesh", "Cannot create a window module asset."); return false; }
                Output->GetStaticMaterials().Reset();
                Output->GetStaticMaterials().Add(FStaticMaterial(Material));
                Output->GetSourceModel(0).BuildSettings.DistanceFieldResolutionScale = 0;
                Output->PostEditChange();
                if (!UEditorLoadingAndSavingUtils::SavePackages({Output->GetOutermost()}, true)) { Error = LOCTEXT("Save", "Cannot save the window module asset."); return false; }
            }
            auto* Instances = Cast<UInstancedStaticMeshComponent>(Component);
            const int32 Count = Instances ? Instances->GetInstanceCount() : 1;
            for (int32 Index = 0; Index < Count; ++Index)
            {
                FTransform Transform = Component->GetComponentTransform().GetRelativeTransform(Building->GetActorTransform());
                if (Instances)
                {
                    FTransform Instance;
                    Instances->GetInstanceTransform(Index, Instance, false);
                    Transform = Instance * Transform;
                }
                FDeepLevelCityBuildingDecorationEntry& Entry = Entries.Emplace_GetRef();
                Entry.WindowSource = FName(*FString::Printf(TEXT("%s_%d_%d"), *Component->GetName(), Index, AssemblyIndex));
                Entry.Name = FName(*FString::Printf(TEXT("%s_%03d_%02d"), *Source->GetName(), Index, AssemblyIndex));
                Entry.EntryGuid = StableWindowId(Entry.WindowSource.ToString());
                Entry.Mesh = Output;
                Entry.LocalTransform = FTransform(Center) * Transform;
                Entry.bWindowLight = true;
                Entry.bEnabled = false;
                Entry.Rendering.bCastShadow = false;
                Entry.Rendering.bAffectNavigation = false;
                Entry.Rendering.CollisionProfile = TEXT("NoCollision");
                Entry.Rendering.MaxDrawDistance = 16000;
                const FBox LocalBounds = CenteredBounds.TransformBy(Entry.LocalTransform.ToMatrixWithScale());
                const FVector Size = LocalBounds.GetSize();
                const double Width = FMath::Max(Size.X, Size.Y);
                Entry.bWindowAutoCandidate = Size.Z >= 80 && Width >= 25 && Width <= Size.Z * 3.5
                    && LocalBounds.Min.Z >= GroundZ + 200;
                if (!Entry.bWindowAutoCandidate)
                {
                    Entries.Pop();
                    continue;
                }
            }
            }
        }
        if (Entries.IsEmpty()) { Error = LOCTEXT("Empty", "No separate translucent window glass was found. Opaque walls, doors and entrances are not imported as glass."); return false; }
        return true;
    }
}

bool FDeepLevelCityDecorationDocument::ImportWindows(UMaterialInterface* Material, FText& Error)
{
    if (!CanEdit() || !Material) { Error = LOCTEXT("Context", "Select a building profile and a window lighting material first."); return false; }
    const auto* Definition = GetBuildingDefinition();
    TArray<FDeepLevelCityBuildingDecorationEntry> Windows;
    const FString Folder = FPackageName::GetLongPackagePath(Profile->GetOutermost()->GetName()) / TEXT("WindowModules");
    if (!CollectWindows(BuildingClass.LoadSynchronous(), Folder, Material,
        Definition->PlacementVolume.Center.Z - Definition->PlacementVolume.Extent.Z, Windows, Error)) return false;
    Edit(LOCTEXT("Import", "Import Whole Window Modules"), EDeepLevelCityDecorationChange::Structure, [&]
    {
        Profile->WindowMaterial = Material;
        if (!Profile->Variants.ContainsByPredicate([](const auto& V) { return V.SelectionGroup == WindowGroup; }))
        {
            auto& V = Profile->Variants.Emplace_GetRef();
            V.Name = TEXT("Window arrangement"); V.SelectionGroup = WindowGroup; V.VariantGuid = FGuid::NewGuid();
        }
        for (auto& Variant : Profile->Variants)
        {
            if (Variant.SelectionGroup != WindowGroup) continue;
            TMap<FName, FDeepLevelCityBuildingDecorationEntry> Previous;
            for (const auto& Entry : Variant.Entries) if (!Entry.WindowSource.IsNone()) Previous.Add(Entry.WindowSource, Entry);
            Variant.Entries.RemoveAll([](const auto& Entry) { return !Entry.WindowSource.IsNone(); });
            for (const auto& Window : Windows)
            {
                auto Entry = Window;
                Entry.EntryGuid = StableWindowId(Variant.VariantGuid.ToString() + Window.WindowSource.ToString());
                if (const auto* Old = Previous.Find(Window.WindowSource))
                { Entry.bEnabled = Old->bEnabled; Entry.WindowColor = Old->WindowColor; Entry.WindowIntensity = Old->WindowIntensity; Entry.WindowMaterialOverride = Old->WindowMaterialOverride; }
                else
                {
                    FRandomStream Random(GetTypeHash(Entry.EntryGuid));
                    Entry.bEnabled = Entry.bWindowAutoCandidate && Random.GetFraction() < .25f;
                }
                Variant.Entries.Add(Entry);
            }
            VariantId = Variant.VariantGuid;
        }
        EntryId.Invalidate();
    });
    return true;
}
void FDeepLevelCityDecorationDocument::SetEntryEnabled(const FGuid& Id, const bool bEnabled)
{
    const auto* Entry = GetEntry(Id);
    if (!Entry || Entry->bEnabled == bEnabled) return;
    const FGuid Room = Entry->WindowRoomGuid;
    Edit(LOCTEXT("Toggle", "Toggle Decoration"), EDeepLevelCityDecorationChange::Output, [&]
    {
        for (auto& Part : DeepLevelCityDecorationAuthoring::FindVariant(*Profile,VariantId)->Entries)
            if (Part.EntryGuid == Id || (Room.IsValid() && Part.bWindowLight && Part.WindowRoomGuid == Room)) Part.bEnabled = bEnabled;
    });
}
void FDeepLevelCityDecorationDocument::SetWindowMaterial(UMaterialInterface* Material)
{
    if (!CanEdit() || Profile->WindowMaterial.Get() == Material) return;
    Edit(LOCTEXT("Material", "Set Window Lighting Material"), EDeepLevelCityDecorationChange::Metadata, [&] { Profile->WindowMaterial = Material; });
}
void FDeepLevelCityDecorationDocument::FillWindows(const float Probability, const int32 Seed)
{
    if (!CanEdit() || !GetVariant() || !FMath::IsFinite(Probability)) return;
    float TotalWeight = 0;
    for (const auto& Appearance : Profile->WindowAppearances)
    {
        if (!FMath::IsFinite(Appearance.Weight) || Appearance.Weight < 0 || !FMath::IsFinite(Appearance.Brightness) || Appearance.Brightness < 0 || Appearance.Color.R < 0 || Appearance.Color.G < 0 || Appearance.Color.B < 0
            || !FMath::IsFinite(Appearance.Color.R) || !FMath::IsFinite(Appearance.Color.G) || !FMath::IsFinite(Appearance.Color.B)) return;
        TotalWeight += Appearance.Weight;
    }
    Edit(LOCTEXT("Fill", "Fill Window Arrangement"), EDeepLevelCityDecorationChange::Output, [&]
    {
        auto* Variant = DeepLevelCityDecorationAuthoring::FindVariant(*Profile, VariantId);
        for (auto& Entry : Variant->Entries)
        {
            if (!Entry.bWindowLight || !Entry.bWindowAutoCandidate) continue;
            const FGuid Room = Entry.WindowRoomGuid.IsValid() ? Entry.WindowRoomGuid : Entry.EntryGuid;
            FRandomStream Random(HashCombineFast(GetTypeHash(Seed), GetTypeHash(Room)));
            Entry.bEnabled = Random.GetFraction() < FMath::Clamp(Probability, 0.f, 1.f);
            if (TotalWeight > 0)
            {
                FRandomStream StyleRandom(HashCombineFast(GetTypeHash(Room), HashCombineFast(GetTypeHash(Seed), 0x9E3779B9u)));
                float Pick = StyleRandom.FRandRange(0, TotalWeight);
                for (const auto& Appearance : Profile->WindowAppearances)
                {
                    if (Appearance.Weight <= 0) continue;
                    Pick -= Appearance.Weight;
                    if (Pick <= 0)
                    {
                        Entry.WindowMaterialOverride = Appearance.Material;
                        Entry.WindowColor = Appearance.Color;
                        Entry.WindowIntensity = Appearance.Brightness;
                        break;
                    }
                }
            }
        }
    });
}
bool UDeepLevelCityWindowAuthoringLibrary::ImportWindows(UDeepLevelCityDecorationSet* Set, TSubclassOf<AActor> Building,
    UMaterialInterface* Material, FText& Error)
{
    FDeepLevelCityDecorationDocument Document;
    Document.Open(Set);
    return Document.SelectBuilding(TSoftClassPtr<AActor>(Building.Get())) && Document.ImportWindows(Material, Error);
}
bool UDeepLevelCityWindowAuthoringLibrary::FillArrangement(UDeepLevelCityDecorationSet* Set, TSubclassOf<AActor> Building, FGuid Variant, float Probability, int32 Seed)
{
    FDeepLevelCityDecorationDocument Document;
    Document.Open(Set);
    if (!Document.SelectBuilding(TSoftClassPtr<AActor>(Building.Get()))) return false;
    Document.SelectVariant(Variant);
    if (!Document.GetVariant() || !Document.CanEdit() || !FMath::IsFinite(Probability)) return false;
    Document.FillWindows(Probability,Seed);
    return true;
}
void UDeepLevelCityWindowAuthoringLibrary::OpenWindowEditor(UDeepLevelCityDecorationSet* Set, TSubclassOf<AActor> Building)
{
    if (!Set || !Building) return;
    if (auto* Existing = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Set, false))
    {
        if (Existing->GetEditorName() == FName(TEXT("DeepLevelCityBuildingDecorationEditor")))
        {
            static_cast<FDeepLevelCityBuildingDecorationEditor*>(Existing)->OpenWindowBuilding(TSoftClassPtr<AActor>(Building.Get()));
            Existing->FocusWindow(Set);
            return;
        }
    }
    const auto Editor = MakeShared<FDeepLevelCityBuildingDecorationEditor>();
    Editor->InitEditor(EToolkitMode::Standalone, {}, Set);
    Editor->OpenWindowBuilding(TSoftClassPtr<AActor>(Building.Get()));
}
bool UDeepLevelCityWindowAuthoringLibrary::SetWindowArrangements(UDeepLevelCityDecorationSet* Set, TSubclassOf<AActor> Building,
    const TArray<FDeepLevelCityBuildingDecorationVariant>& WindowVariants, FText& Error)
{
    FDeepLevelCityDecorationDocument Document;
    Document.Open(Set);
    if (!Document.SelectBuilding(TSoftClassPtr<AActor>(Building.Get())))
    {
        Error = LOCTEXT("BuildingNotFound", "Building not found in decoration set.");
        return false;
    }
    const auto* Profile = Document.GetProfile();
    if (!Profile)
    {
        Error = LOCTEXT("ProfileNotFound", "Building profile not found.");
        return false;
    }
    for (const auto& Variant : WindowVariants)
    {
        if (Variant.SelectionGroup != WindowGroup)
        {
            Error = LOCTEXT("InvalidWindowGroup", "Only NightWindows variants can be modified through the window authoring library.");
            return false;
        }
        for (const auto& Entry : Variant.Entries)
        {
            if (!Entry.bWindowLight)
            {
                Error = LOCTEXT("InvalidWindowEntry", "All entries in window arrangements must have Window Lighting enabled.");
                return false;
            }
        }
    }
    TArray<FDeepLevelCityBuildingDecorationVariant> CombinedVariants;
    for (const auto& ExistingVariant : Profile->Variants)
    {
        if (ExistingVariant.SelectionGroup != WindowGroup)
        {
            CombinedVariants.Add(ExistingVariant);
        }
    }
    CombinedVariants.Append(WindowVariants);
    return Document.CommitVariants(CombinedVariants, Error);
}
#undef LOCTEXT_NAMESPACE
