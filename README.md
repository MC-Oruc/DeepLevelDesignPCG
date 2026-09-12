# DeepLevelDesignPCG

Reusable Unreal Engine 5.8 tools for deterministic city layouts, road networks, building lines, and categorized decoration.

The plugin is independent from host-project code and content. It ships generic Blueprint actors, PCG graphs, editor calibration workspaces, and source icons. Meshes, Packed Level Actors, and populated catalog Data Assets remain owned by the host project.

## Installation

```bash
git submodule add https://github.com/MC-Oruc/DeepLevelDesignPCG.git Plugins/DeepLevelDesignPCG
git submodule update --init --recursive
```

Enable `DeepLevelDesignPCG` in the project descriptor and build the Editor target. The built-in `PCG` plugin is enabled as a dependency.

## Shipped Assets

- `/DeepLevelDesignPCG/Building/BP_DeepLevelBuildingLine`
- `/DeepLevelDesignPCG/Building/PCG_DeepLevelBuildingLine`
- `/DeepLevelDesignPCG/City/DA_DeepLevelCityGrid_500`
- `/DeepLevelDesignPCG/Road/BP_DeepLevelRoadNetwork`
- `/DeepLevelDesignPCG/Road/PCG_DeepLevelRoadNetwork`

The shipped graphs intentionally have no catalog assigned.

## Host Project Setup

1. Place one `Deep Level City Layout` actor and assign a City Grid Profile. The shipped 500 cm profile is a ready starting point.
2. Set the city origin, ground height, and grid extent on that actor.
3. Create a `Deep Level Building Placement Catalog` Data Asset and add calibrated Packed Level Actors.
4. Create a `Deep Level Road Tile Catalog` Data Asset and calibrate the road and sidewalk tile set. Assign the same City Grid Profile used by the City Layout.
5. Place Road Network and Building Line actors, assign their City Layout and catalogs. Select the Building path source below, then use **Generate Buildings**.
6. Create focused Decoration Category assets, compose them in one Decoration Set, and assign that set to the City Layout.
7. Use `Regenerate City Decoration` on the City Layout. Normal regeneration rebuilds dirty chunks; force regeneration rebuilds every output chunk.

Catalogs and populated Decoration assets are project data. They contain mesh/class references, placement volumes, matching rules, connectivity, weights, and calibration state; they are not copied into the plugin.

## Building Path Sources

The same Building Line actor, calibrated catalog, packing solver, seed, variety, and corner settings support both modes:

| Path Source | City Layout registration | Placement source |
| --- | --- | --- |
| `AuthoredSpline` (default) | `LayoutProviders` | The actor's existing open or closed spline; no Road required |
| `RoadSidewalkEdges` | `DerivedLayoutProviders` | Road/sidewalk semantics from the base City Layout snapshot |

Keep Road Network actors in `LayoutProviders`. One Roadside Building actor per City Layout consumes all suitable Road edges; additional authored Building actors remain in the base list and block roadside placements through their occupancy. Duplicate registrations, wrong modes/lists, missing edges, and incompatible grids fail explicitly. Changing modes requires moving the provider registration to the corresponding list. Existing authored registrations and catalogs remain valid.

Roadside placement follows the **block-side outer sidewalk boundary**, not the curb decoration anchor. Compatible edges join into paths; gaps, width changes, ambiguous turns, and junction approaches split them. The existing solver packs each path. Road, sidewalk, existing Building occupancy, junction clearance, city bounds, and accepted roadside footprints reject conflicting candidates. Rejected candidates leave gaps without random retries or repacking. No fitting buildings is valid empty output; missing Road semantics is an error.

Use **Generate Buildings** on the actor for validation before PCG starts. The Building node emits the accepted actor-owned plan; its optional spline pin preserves authored spline metadata. The existing graph's Spawn Actor node must consume `ActorClass` and spawn each whole PLA without merging. Keep **Is Partitioned** disabled. Generation uses PCG managed resources and runs only in the editor, on demand, at whole-Building-actor scope. It does not regenerate Road meshes or introduce per-frontage actors.

Road, base occupancy, grid, catalog/calibration, mode, spline, and placement settings invalidate the result. `Output Current`, `Last Generation Error`, and `Rejected Placement Count` expose its status. After successful PLA generation, the saved fragment publishes occupancy and facade/corner anchors, and configured Decoration updates from the final snapshot. Stale or failed roadside output blocks snapshot publication and Decoration regeneration. Prevalidation errors preserve existing output; failures after PCG starts have no automatic actor rollback. Correct the error and generate again, or explicitly clean up.

Save the level after generation. Saved PLA output loads without a runtime rebuild. Chunk-based Decoration updates remain separate from Building generation; partial chunk/frontage PLA regeneration is not implemented. Manual PLAs outside the provider system are not occupancy inputs and must be handled separately before using the same area for Roadside Building. This feature does not migrate an existing map or remove manual actors.

## City Layout and Decoration

The City Layout is the composition root. It owns the shared translated grid, generation bounds, base/derived provider lists, decoration seed, Decoration Set, persistent overrides, immutable merged snapshot, and materialized output chunks. Base providers build first; Roadside Building consumes that snapshot and publishes its successfully generated fragment before final composition. Road and Building remain authoritative for their own geometry. Road never accesses Building classes or catalogs.

Decoration Categories are reusable rule groups. Each entry filters semantic anchor tags, required or blocked occupancy, probability, deterministic anchor interval, spacing, and clearance, then emits a Static Mesh, Actor, or Decal. `AnchorPhase` explicitly selects the grid-cell phase; it is independent of the seed and entry identity. Matching interval/phase with local offsets forms furniture groups. `bStaggerOppositeEdges` offsets reverse-facing segment anchors by half an even interval. Keep minimum spacing below the intended opposite-curb distance so it does not erase the authored rhythm. A Decoration Set composes categories and defines deterministic priority. Stable placement IDs derive from the source, anchor, slot, and entry identity, so `Remove`, `Modify`, and `Add` overrides survive regeneration. Solid Mesh and Actor outputs also respect physical clearance across placement slots; decals do not block solid props.

Generated outputs are saved instance components owned by the City Layout and grouped by chunk:

- Static Mesh entries are grouped into Hierarchical Instanced Static Mesh components.
- Actor entries use Child Actor components, allowing project-owned Blueprint props such as functional street lights.
- Decal entries create saved decal components.

Editor regeneration replaces the owned output set and marks the City Layout package dirty. Saved maps load the generated components directly in the editor and packaged game; `BeginPlay` does not rebuild them. The catalog, seed, providers, and overrides remain authoritative inputs for deliberate regeneration.

The resolver rejects invalid profiles, provider/grid mismatches, duplicate stable identities, invalid output entries, and invalid overrides with explicit diagnostics. It does not silently rebuild or substitute missing authoring data.

Road publishes calibrated road/sidewalk surfaces plus oriented sidewalk-edge anchors. Edge anchors carry road tangent, usable sidewalk depth, nearby-junction safety, dead-end, and local/arterial road classification. Decoration rules can therefore align furniture along curbs, keep it clear of crossings, restrict traffic control to endpoints, and select lighting by road width. Road and Building revisions plus grid-chunk differences determine dirty chunks. Regeneration replaces only those chunks unless force regeneration is requested. Moving the City Layout establishes a new translated grid origin; rotation and scale are intentionally unsupported.

## Local Road Overrides

Road Network actors own persistent, grid-cell overrides for final placement corrections. Overrides survive PCG cleanup and regeneration.

- `Remove` suppresses the generated placement in one occupied cell.
- `Modify` keeps the generated placement and can replace its mesh or material and apply a local transform adjustment.
- `Add` creates an explicit road or sidewalk placement in an empty cell and requires a mesh.

Select a Road Network to see hovered grid coordinates and authored override outlines in the level viewport. `Shift + Left Drag` keeps drawing or reshaping roads. Use `Shift + Right Click` to exclude or restore a cell, `Ctrl + Shift + Left Click` to create or update a Modify override, `Ctrl + Shift + Right Click` to add the Static Mesh selected in the Content Browser, and `Ctrl + Shift + Middle Click` to clear any override. Edit complete `Modify` and `Add` values under **Local Overrides** in the actor Details panel. Red outlines are removed cells, orange outlines are modified cells, and green outlines are added cells. Duplicate overrides and operations targeting the wrong occupancy state fail generation with an actionable error instead of being silently ignored.

## Module Boundaries

- `DeepLevelDesignPCG`: runtime actors, catalogs, planners, solvers, PCG nodes, and deterministic tests.
- `DeepLevelDesignPCGEditor`: asset actions, Details customization, calibration editors, viewport authoring, notifications, and editor integration tests.

Runtime code has no dependency on Slate or host-project modules. Editor feedback crosses the module boundary through one editor-only event. There are no compatibility adapters, runtime recovery paths, or duplicated host implementations.

## Source Artwork

Editable PNG sources and final runtime/editor images are under `Resources/Icons`. `Resources/Icon128.png` is the plugin browser icon; `Content/Editor/Icons/T_RoadNetwork` is the editor-only Road Network marker texture.
