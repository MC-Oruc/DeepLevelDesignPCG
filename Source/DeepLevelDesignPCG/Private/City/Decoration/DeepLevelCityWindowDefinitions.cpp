#include "City/Decoration/DeepLevelCityBuildingDecorationProfile.h"

FDeepLevelCityBuildingDecorationEntry FDeepLevelCityWindowDefinition::MakeEntry() const
{
    FDeepLevelCityBuildingDecorationEntry Entry;
    Entry.EntryGuid = WindowGuid;
    Entry.Name = Name;
    Entry.bWindowLight = true;
    Entry.Mesh = Mesh;
    Entry.LocalTransform = LocalTransform;
    Entry.Rendering = Rendering;
    Entry.bAutoFacade = bAutoFacade;
    Entry.Facades = Facades;
#if WITH_EDITORONLY_DATA
    Entry.WindowDefinitionGuid = WindowGuid;
    Entry.WindowRoomGuid = RoomGuid;
    Entry.WindowSource = WindowSource;
    Entry.bWindowAutoCandidate = bAutoCandidate;
#endif
    return Entry;
}

FDeepLevelCityBuildingDecorationVariant UDeepLevelCityBuildingDecorationProfile::BuildVariantView(
    const FDeepLevelCityBuildingDecorationVariant& Variant) const
{
    FDeepLevelCityBuildingDecorationVariant View = Variant;
    View.WindowStates.Reset();
    if (Variant.WindowStates.IsEmpty() && Variant.SelectionGroup != FName(TEXT("NightWindows"))) return View;
    TMap<FGuid,const FDeepLevelCityWindowState*> States;
    for (const auto& State : Variant.WindowStates) States.Add(State.WindowGuid,&State);
    for (const auto& Definition : WindowDefinitions)
    {
        auto Entry = Definition.MakeEntry();
        Entry.bEnabled = false;
        Entry.EntryGuid = FDeepLevelCityStableId::MakePlacementId(Variant.VariantGuid,Definition.WindowGuid,0);
        if (const auto* const* Found = States.Find(Definition.WindowGuid))
        {
            const auto& State = **Found;
            Entry.EntryGuid = State.EntryGuid;
            Entry.bEnabled = State.bEnabled;
            Entry.WindowColor = State.Color;
            Entry.WindowIntensity = State.Brightness;
            Entry.WindowMaterialOverride = State.Material;
        }
        View.Entries.Add(MoveTemp(Entry));
    }
    return View;
}

#if WITH_EDITOR
void UDeepLevelCityBuildingDecorationProfile::ExpandWindowEntries()
{
    for (auto& Variant : Variants) Variant = BuildVariantView(Variant);
}

void UDeepLevelCityBuildingDecorationProfile::CompactWindowEntries(const FGuid GeometryOwnerVariant)
{
    auto UpdateDefinition = [](FDeepLevelCityWindowDefinition& Definition,const FDeepLevelCityBuildingDecorationEntry& Entry)
    {
        Definition.Name = Entry.Name;
        Definition.Mesh = Entry.Mesh;
        Definition.LocalTransform = Entry.LocalTransform;
        Definition.Rendering = Entry.Rendering;
        Definition.bAutoFacade = Entry.bAutoFacade;
        Definition.Facades = Entry.Facades;
        Definition.RoomGuid = Entry.WindowRoomGuid.IsValid() ? Entry.WindowRoomGuid : Definition.WindowGuid;
        Definition.WindowSource = Entry.WindowSource;
        Definition.bAutoCandidate = Entry.bWindowAutoCandidate;
    };
    for (auto& Variant : Variants)
    {
        TArray<FDeepLevelCityBuildingDecorationEntry> Ordinary;
        for (const auto& Entry : Variant.Entries)
        {
            if (!Entry.bWindowLight) { Ordinary.Add(Entry); continue; }
            auto* Definition = WindowDefinitions.FindByPredicate([&](const auto& D) { return D.WindowGuid == Entry.WindowDefinitionGuid; });
            const bool bHasReference = Definition != nullptr;
            if (!Definition)
            {
                Definition = WindowDefinitions.FindByPredicate([&](const auto& D)
                {
                    return D.Name == Entry.Name && D.Mesh == Entry.Mesh && D.LocalTransform.Equals(Entry.LocalTransform,UE_DOUBLE_SMALL_NUMBER)
                        && D.Rendering == Entry.Rendering && D.bAutoFacade == Entry.bAutoFacade && D.Facades == Entry.Facades;
                });
            }
            if (!Definition)
            {
                Definition = &WindowDefinitions.Emplace_GetRef();
                Definition->WindowGuid = FGuid::NewGuid();
                UpdateDefinition(*Definition,Entry);
            }
            else if (bHasReference && (!GeometryOwnerVariant.IsValid() || Variant.VariantGuid == GeometryOwnerVariant))
            {
                UpdateDefinition(*Definition,Entry);
            }
            FDeepLevelCityWindowState State;
            State.EntryGuid = Entry.EntryGuid;
            State.WindowGuid = Definition->WindowGuid;
            State.bEnabled = Entry.bEnabled;
            State.Color = Entry.WindowColor;
            State.Brightness = Entry.WindowIntensity;
            State.Material = Entry.WindowMaterialOverride;
            Variant.WindowStates.RemoveAll([&](const auto& Existing) { return Existing.WindowGuid == State.WindowGuid; });
            Variant.WindowStates.Add(MoveTemp(State));
        }
        Variant.Entries = MoveTemp(Ordinary);
    }
}
#endif
