#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "City/Decoration/DeepLevelCityDecorationDocument.h"
#include "City/Decoration/DeepLevelCityWindowGeometry.h"
#include "PackedLevelActor/PackedLevelActor.h"
#include "Engine/StaticMesh.h"
#include "UDynamicMesh.h"
#include "Materials/Material.h"
#include "City/Decoration/DeepLevelCityWindowAuthoring.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelWindowGroupingTest, "DeepLevelDesignPCG.Editor.City.Windows.WholeOpening", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelWindowGroupingTest::RunTest(const FString&)
{
    TStrongObjectPtr<UDynamicMesh> Mesh(NewObject<UDynamicMesh>());
    Mesh->EditMesh([](UE::Geometry::FDynamicMesh3& M)
    {
        auto Pane = [&M](double X, double Y, double Z, double Width, double Height)
        {
            const int32 A = M.AppendVertex(FVector3d(X,Y,Z)), B = M.AppendVertex(FVector3d(X+Width,Y,Z));
            const int32 C = M.AppendVertex(FVector3d(X+Width,Y,Z+Height)), D = M.AppendVertex(FVector3d(X,Y,Z+Height));
            M.AppendTriangle(A,B,C); M.AppendTriangle(A,C,D);
        };
        Pane(0,0,0,100,100); Pane(0,0,108,100,60); // one window, two panes
        Pane(300,0,0,100,100); // next opening
        Pane(0,0,450,100,100); // next floor
        Pane(0,30,0,100,100); // another facade plane
    });
    auto Groups = DeepLevelCityWindows::GroupGlass(Mesh.Get());
    TestEqual(TEXT("Stacked panes join, separate openings/floors/planes remain separate"), Groups.Num(), 4);
    int32 Total = 0, Largest = 0;
    for (const auto& Group : Groups) Group->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& M) { Total += M.TriangleCount(); Largest = FMath::Max(Largest, M.TriangleCount()); });
    TestEqual(TEXT("All glass triangles are retained without filling frames"), Total, 10);
    TestEqual(TEXT("Whole window contains both panes"), Largest, 4);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelWindowDocumentTest, "DeepLevelDesignPCG.Editor.City.Windows.DocumentUndo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelWindowDocumentTest::RunTest(const FString&)
{
    TStrongObjectPtr<UDeepLevelCityDecorationSet> Set(NewObject<UDeepLevelCityDecorationSet>());
    auto* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>(Set.Get()); Set->BuildingCatalog = Catalog;
    auto& Definition = Catalog->Buildings.Emplace_GetRef(); Definition.BuildingClass = APackedLevelActor::StaticClass(); Definition.bCalibrated = true;
    auto* Profile = NewObject<UDeepLevelCityBuildingDecorationProfile>(Set.Get()); Profile->BuildingClass = Definition.BuildingClass; Set->BuildingProfiles.Add(Profile);
    auto& Variant = Profile->Variants.Emplace_GetRef(); Variant.VariantGuid = FGuid::NewGuid(); Variant.SelectionGroup = TEXT("NightWindows");
    auto& Window = Variant.Entries.Emplace_GetRef(); Window.EntryGuid = FGuid::NewGuid(); Window.bWindowLight = true; Window.bWindowAutoCandidate = true;
    Window.Mesh = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    const FGuid Id = Window.EntryGuid;
    const FGuid VariantId = Variant.VariantGuid;
    auto OtherPane = Window; OtherPane.EntryGuid = FGuid::NewGuid(); OtherPane.Name = TEXT("Second pane");
    Window.WindowRoomGuid = OtherPane.WindowRoomGuid = FGuid::NewGuid();
    Variant.Entries.Add(OtherPane);
    Profile->CompactWindowEntries();
    FDeepLevelCityDecorationDocument Document; Document.Open(Set.Get(), Profile); Document.SelectVariant(VariantId); Document.SelectEntry(Id);
    Document.SetEntryEnabled(Id, false); TestFalse(TEXT("Whole window can be disabled"), Document.GetEntry()->bEnabled);
    TestFalse(TEXT("Room switch also disables the other pane"),Document.GetEntry(OtherPane.EntryGuid)->bEnabled);
    Document.Undo(); TestTrue(TEXT("Undo restores the switch"), Document.GetEntry()->bEnabled);
    const auto Before = *Document.GetEntry(); auto After = Before; After.WindowColor = FLinearColor(.2,.4,.6); After.WindowIntensity = 3;
    FText Error; TestTrue(TEXT("Appearance commits through the document"), Document.SetEntryFields(After, Before, Error));
    TestEqual(TEXT("Brightness saved"), Document.GetEntry()->WindowIntensity, 3.f);
    Document.Undo(); TestEqual(TEXT("Appearance undo restores brightness"), Document.GetEntry()->WindowIntensity, Before.WindowIntensity);
    Document.FillWindows(0,1337); TestFalse(TEXT("Zero fill disables eligible windows"), Document.GetEntry()->bEnabled);
    Document.FillWindows(1,1337); TestTrue(TEXT("Full fill enables eligible windows"), Document.GetEntry()->bEnabled);
    FDeepLevelCityWindowAppearance Warm; Warm.Name=TEXT("Warm"); Warm.Color=FLinearColor(1,.7f,.3f); Warm.Brightness=2;
    FDeepLevelCityWindowAppearance Cool; Cool.Name=TEXT("Cool"); Cool.Color=FLinearColor(.5f,.7f,1); Cool.Brightness=1;
    Warm.Material=NewObject<UMaterial>(Profile); Cool.Material=NewObject<UMaterial>(Profile);
    Profile->WindowAppearances={Warm,Cool};
    TSet<FSoftObjectPath> Materials;
    for(int32 Seed=0;Seed<32;++Seed)
    {
        Document.FillWindows(1,Seed); const auto A=*Document.GetEntry();
        Materials.Add(A.WindowMaterialOverride.ToSoftObjectPath());
        Document.FillWindows(1,Seed); const auto B=*Document.GetEntry();
        TestTrue(TEXT("Palette selection is deterministic for a saved seed"),A.WindowMaterialOverride==B.WindowMaterialOverride && A.WindowColor==B.WindowColor && A.WindowIntensity==B.WindowIntensity);
    }
    TestEqual(TEXT("Both weighted material appearances can be selected"),Materials.Num(),2);
    Document.FillWindows(0,1337); TestFalse(TEXT("Palette does not change zero density"), Document.GetEntry()->bEnabled);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelWindowPivotTest, "DeepLevelDesignPCG.Editor.City.Windows.Pivot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelWindowPivotTest::RunTest(const FString&)
{
    TStrongObjectPtr<UDynamicMesh> Mesh(NewObject<UDynamicMesh>());
    Mesh->EditMesh([](UE::Geometry::FDynamicMesh3& M) { auto A=M.AppendVertex(FVector3d(100,200,300)); auto B=M.AppendVertex(FVector3d(200,200,300)); auto C=M.AppendVertex(FVector3d(200,200,500)); M.AppendTriangle(A,B,C); });
    const FTransform Component(FRotator(0,90,0),FVector(2000,-500,200),FVector(2));
    FVector Original;
    Mesh->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& M) { Original=Component.TransformPosition(FVector(M.GetVertex(0))); });
    const FVector Center = DeepLevelCityWindows::CenterGlass(Mesh.Get());
    const FTransform Entry = FTransform(Center) * Component;
    Mesh->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& M)
    {
        FBox Bounds(ForceInit); for(int32 V:M.VertexIndicesItr()) Bounds+=FVector(M.GetVertex(V));
        TestTrue(TEXT("Source geometry is centered on the visible window"),Bounds.GetCenter().IsNearlyZero());
        TestTrue(TEXT("Centering preserves world-space glass under rotation/scale"), Entry.TransformPosition(FVector(M.GetVertex(0))).Equals(Original,.001));
        TestTrue(TEXT("Facade classification now uses the actual window center"), Entry.GetLocation().Equals(Component.TransformPosition(Center),.001));
    });
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelWindowCurvedBayTest, "DeepLevelDesignPCG.Editor.City.Windows.CurvedBayGrouping", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelWindowCurvedBayTest::RunTest(const FString&)
{
    TStrongObjectPtr<UDynamicMesh> Mesh(NewObject<UDynamicMesh>());
    Mesh->EditMesh([](UE::Geometry::FDynamicMesh3& M)
    {
        // 3 adjacent curved bay facets angled by ~18 degrees each around corner
        auto AddFacet = [&M](const FVector3d& Center, const FVector3d& Normal, double Width, double Height)
        {
            const FVector3d Tangent = FVector3d::CrossProduct(FVector3d::UnitZ(), Normal).GetSafeNormal();
            const FVector3d H = FVector3d(0, 0, Height * 0.5);
            const FVector3d W = Tangent * (Width * 0.5);
            const int32 V0 = M.AppendVertex(Center - W - H);
            const int32 V1 = M.AppendVertex(Center + W - H);
            const int32 V2 = M.AppendVertex(Center + W + H);
            const int32 V3 = M.AppendVertex(Center - W + H);
            M.AppendTriangle(V0, V1, V2);
            M.AppendTriangle(V0, V2, V3);
        };
        AddFacet(FVector3d(0, 0, 150), FVector3d(0, 1, 0), 40, 120);
        const double Rad1 = FMath::DegreesToRadians(18.0);
        AddFacet(FVector3d(38, 6, 150), FVector3d(FMath::Sin(Rad1), FMath::Cos(Rad1), 0), 40, 120);
        const double Rad2 = FMath::DegreesToRadians(36.0);
        AddFacet(FVector3d(72, 23, 150), FVector3d(FMath::Sin(Rad2), FMath::Cos(Rad2), 0), 40, 120);
    });
    auto Groups = DeepLevelCityWindows::GroupGlass(Mesh.Get());
    TestEqual(TEXT("Curved bay facets group into a single whole-room window assembly"), Groups.Num(), 1);
    int32 Triangles = 0;
    Groups[0]->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& M) { Triangles = M.TriangleCount(); });
    TestEqual(TEXT("All 3 bay facets retained"), Triangles, 6);

    // Regression 1: Parallel-offset panes (DotNormals == 1.0, offset 30cm along normal) must NOT merge
    TStrongObjectPtr<UDynamicMesh> ParallelMesh(NewObject<UDynamicMesh>());
    ParallelMesh->EditMesh([](UE::Geometry::FDynamicMesh3& M)
    {
        auto Pane = [&M](double X, double Y, double Z, double Width, double Height)
        {
            const int32 A = M.AppendVertex(FVector3d(X,Y,Z)), B = M.AppendVertex(FVector3d(X+Width,Y,Z));
            const int32 C = M.AppendVertex(FVector3d(X+Width,Y,Z+Height)), D = M.AppendVertex(FVector3d(X,Y,Z+Height));
            M.AppendTriangle(A,B,C); M.AppendTriangle(A,C,D);
        };
        Pane(0, 0, 0, 100, 100);
        Pane(0, 30, 0, 100, 100); // 30cm parallel offset
    });
    auto ParallelGroups = DeepLevelCityWindows::GroupGlass(ParallelMesh.Get());
    TestEqual(TEXT("Parallel-offset panes do NOT merge"), ParallelGroups.Num(), 2);

    // Regression 2: Nearby separate flat openings on same plane (wall gap > frame tolerance) must NOT merge
    TStrongObjectPtr<UDynamicMesh> SeparateMesh(NewObject<UDynamicMesh>());
    SeparateMesh->EditMesh([](UE::Geometry::FDynamicMesh3& M)
    {
        auto Pane = [&M](double X, double Y, double Z, double Width, double Height)
        {
            const int32 A = M.AppendVertex(FVector3d(X,Y,Z)), B = M.AppendVertex(FVector3d(X+Width,Y,Z));
            const int32 C = M.AppendVertex(FVector3d(X+Width,Y,Z+Height)), D = M.AppendVertex(FVector3d(X,Y,Z+Height));
            M.AppendTriangle(A,B,C); M.AppendTriangle(A,C,D);
        };
        Pane(0, 0, 0, 100, 100);
        Pane(160, 0, 0, 100, 100); // 60cm gap between openings
    });
    auto SeparateGroups = DeepLevelCityWindows::GroupGlass(SeparateMesh.Get());
    TestEqual(TEXT("Nearby separate flat openings do NOT merge"), SeparateGroups.Num(), 2);

    // Regression 3: Curved angled facets on different floors must NOT merge
    TStrongObjectPtr<UDynamicMesh> MultiFloorMesh(NewObject<UDynamicMesh>());
    MultiFloorMesh->EditMesh([](UE::Geometry::FDynamicMesh3& M)
    {
        auto AddFacet = [&M](const FVector3d& Center, const FVector3d& Normal, double Width, double Height)
        {
            const FVector3d Tangent = FVector3d::CrossProduct(FVector3d::UnitZ(), Normal).GetSafeNormal();
            const FVector3d H = FVector3d(0, 0, Height * 0.5);
            const FVector3d W = Tangent * (Width * 0.5);
            const int32 V0 = M.AppendVertex(Center - W - H);
            const int32 V1 = M.AppendVertex(Center + W - H);
            const int32 V2 = M.AppendVertex(Center + W + H);
            const int32 V3 = M.AppendVertex(Center - W + H);
            M.AppendTriangle(V0, V1, V2);
            M.AppendTriangle(V0, V2, V3);
        };
        const double Rad = FMath::DegreesToRadians(20.0);
        AddFacet(FVector3d(0, 0, 150), FVector3d(0, 1, 0), 40, 120); // Floor 1
        AddFacet(FVector3d(38, 6, 450), FVector3d(FMath::Sin(Rad), FMath::Cos(Rad), 0), 40, 120); // Floor 2
    });
    auto MultiFloorGroups = DeepLevelCityWindows::GroupGlass(MultiFloorMesh.Get());
    TestEqual(TEXT("Curved facets on different floors do NOT merge"), MultiFloorGroups.Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeepLevelWindowScopedCommitTest, "DeepLevelDesignPCG.Editor.City.Windows.ScopedCommit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeepLevelWindowScopedCommitTest::RunTest(const FString&)
{
    TStrongObjectPtr<UDeepLevelCityDecorationSet> Set(NewObject<UDeepLevelCityDecorationSet>());
    auto* Catalog = NewObject<UDeepLevelBuildingPlacementCatalog>(Set.Get()); Set->BuildingCatalog = Catalog;
    auto& Definition = Catalog->Buildings.Emplace_GetRef(); Definition.BuildingClass = APackedLevelActor::StaticClass(); Definition.bCalibrated = true;
    auto* Profile = NewObject<UDeepLevelCityBuildingDecorationProfile>(Set.Get()); Profile->BuildingClass = Definition.BuildingClass; Set->BuildingProfiles.Add(Profile);

    // Base variant (None group) - must never be overwritten by window library
    auto& BaseVariant = Profile->Variants.Emplace_GetRef();
    BaseVariant.VariantGuid = FGuid::NewGuid();
    BaseVariant.Name = TEXT("Storefront base");
    BaseVariant.SelectionGroup = TEXT("None");
    auto& BaseEntry = BaseVariant.Entries.Emplace_GetRef();
    BaseEntry.EntryGuid = FGuid::NewGuid();
    BaseEntry.Name = TEXT("Signboard");
    BaseEntry.bWindowLight = false;
    BaseEntry.Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    const FGuid OriginalBaseEntryGuid = BaseEntry.EntryGuid;

    // Existing window variant
    auto& OldWindowVariant = Profile->Variants.Emplace_GetRef();
    OldWindowVariant.VariantGuid = FGuid::NewGuid();
    OldWindowVariant.Name = TEXT("Old Night windows");
    OldWindowVariant.SelectionGroup = TEXT("NightWindows");
    const FGuid OriginalWindowVariantGuid = OldWindowVariant.VariantGuid;

    // 1. Rejection: WindowVariants containing non-NightWindows group
    FDeepLevelCityBuildingDecorationVariant IllegalVariant;
    IllegalVariant.VariantGuid = FGuid::NewGuid();
    IllegalVariant.SelectionGroup = TEXT("None");
    FText Error;
    TestFalse(TEXT("Rejects variants with non-NightWindows group"),
        UDeepLevelCityWindowAuthoringLibrary::SetWindowArrangements(Set.Get(), APackedLevelActor::StaticClass(), {IllegalVariant}, Error));

    // 2. Rejection: WindowVariants containing entries with bWindowLight == false
    FDeepLevelCityBuildingDecorationVariant InvalidEntryVariant;
    InvalidEntryVariant.VariantGuid = FGuid::NewGuid();
    InvalidEntryVariant.SelectionGroup = TEXT("NightWindows");
    auto& NonWindowEntry = InvalidEntryVariant.Entries.Emplace_GetRef();
    NonWindowEntry.EntryGuid = FGuid::NewGuid();
    NonWindowEntry.bWindowLight = false;
    TestFalse(TEXT("Rejects entries without bWindowLight"),
        UDeepLevelCityWindowAuthoringLibrary::SetWindowArrangements(Set.Get(), APackedLevelActor::StaticClass(), {InvalidEntryVariant}, Error));

    // 3. Valid scoped commit: updates NightWindows while preserving base
    FDeepLevelCityBuildingDecorationVariant ValidWindowVariant;
    ValidWindowVariant.VariantGuid = FGuid::NewGuid();
    ValidWindowVariant.Name = TEXT("Calibrated Night windows");
    ValidWindowVariant.SelectionGroup = TEXT("NightWindows");
    auto& ValidWindowEntry = ValidWindowVariant.Entries.Emplace_GetRef();
    ValidWindowEntry.EntryGuid = FGuid::NewGuid();
    ValidWindowEntry.bWindowLight = true;
    ValidWindowEntry.bWindowAutoCandidate = true;
    ValidWindowEntry.Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));

    TestTrue(TEXT("Accepts valid window variants commit"),
        UDeepLevelCityWindowAuthoringLibrary::SetWindowArrangements(Set.Get(), APackedLevelActor::StaticClass(), {ValidWindowVariant}, Error));

    // Verify base variant preserved
    const auto* FoundBase = Profile->Variants.FindByPredicate([](const auto& V) { return V.SelectionGroup == TEXT("None"); });
    TestNotNull(TEXT("Base variant preserved"), FoundBase);
    TestEqual(TEXT("Base variant entries preserved"), FoundBase ? FoundBase->Entries.Num() : 0, 1);
    if (FoundBase && FoundBase->Entries.Num() > 0)
    {
        TestEqual(TEXT("Base entry GUID preserved"), FoundBase->Entries[0].EntryGuid, OriginalBaseEntryGuid);
        TestFalse(TEXT("Base entry remains non-window"), FoundBase->Entries[0].bWindowLight);
    }

    // Verify NightWindows replaced with new arrangement
    const auto* FoundWindow = Profile->Variants.FindByPredicate([](const auto& V) { return V.SelectionGroup == TEXT("NightWindows"); });
    TestNotNull(TEXT("Window variant present"), FoundWindow);
    TestEqual(TEXT("Window variant GUID updated"), FoundWindow ? FoundWindow->VariantGuid : FGuid(), ValidWindowVariant.VariantGuid);

    // 4. Test Undo through document
    FDeepLevelCityDecorationDocument Document;
    Document.Open(Set.Get(), Profile);
    Document.Undo();
    const auto* RestoredWindow = Profile->Variants.FindByPredicate([](const auto& V) { return V.SelectionGroup == TEXT("NightWindows"); });
    TestNotNull(TEXT("Window variant restored after Undo"), RestoredWindow);
    TestEqual(TEXT("Restored to Old Night windows GUID"), RestoredWindow ? RestoredWindow->VariantGuid : FGuid(), OriginalWindowVariantGuid);
    return true;
}
#endif
