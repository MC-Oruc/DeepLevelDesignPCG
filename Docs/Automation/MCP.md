# UE 5.8 PCG automation / MCP

The editor module registers `DeepLevelDesignPCGEditor.DeepLevelPCGToolset` with UE 5.8's native ToolsetRegistry. The existing Unreal MCP server exposes it. No independent server, Python bridge, runtime dependency or project-specific policy is introduced.

Two tools: `Describe()` returns the versioned DSL contract; `Execute(Request)` accepts one JSON string and returns a native UE 5.8 asynchronous result. The resolved value retains the existing success/report/image response shape. Synchronous decoration and catalog edits complete immediately; actor generation awaits its terminal PCG event without blocking the editor game thread. Discover once, then combine inspection, changes, validation and optional image capture in each Execute call.

## Ownership

```mermaid
flowchart TD
    MCP[UE native MCP / ToolsetRegistry] --> Adapter[Automation/MCP: transport and PNG encoding]
    Adapter --> Dispatch[Automation: version and domain dispatch]
    Dispatch --> Road[Road/Automation: road DSL]
    Dispatch --> Building[Building/Automation: building DSL]
    Dispatch --> Decoration[City/Decoration/Automation: decoration DSL]
    Road --> RoadAuthoring[Road: network and catalog authoring]
    Building --> BuildingAuthoring[Building: layout and catalog authoring]
    Road --> Generation[Automation: PCG completion lifecycle]
    Building --> Generation
    Generation --> Result[Verified result, optional save and fresh capture]
    Decoration --> Document[City/Decoration: existing authoring document]
    Document --> Profile[City-owned building decoration profile]
    Decoration --> Preview[City/Decoration: isolated preview and image readback]
```

The adapter owns no domain state. The dispatcher owns the common request envelope and domain routing. Decoration owns its operations, identities, validation and preview. Shared response data contains only JSON and optional pixels; domain executors do not depend on MCP.

Road and Building now join the same dispatcher. Each owns its typed DSL parser and authoring boundary. Catalog Edit/AutoFit services are shared with Slate; open catalog editors listen to property-change notifications. Runtime knows nothing about MCP. Generation lifecycle is shared because both domains use the same PCG terminal events, not because their data ownership is shared.

## Decoration scope

Existing profiles linked through a City Decoration Set: inspect catalog/profile data; add/update/duplicate/remove variants and entries; reorder entries; create/update/remove transform links. Meshes, actor Blueprints and decal materials use the same asset classification and authoring rules as drag-and-drop in the editor. Links support different assets, independent location/rotation/scale synchronization, mirror/copy rotation and offsets.

Decoration asset creation, decoration catalog editing, profile assignment and city-decoration regeneration remain outside the decoration DSL scope. No arbitrary property writes or function execution are exposed.

## Calls

Use exact object paths returned by inspection, not asset-name guesses. A catalog inspection omits `building`:

```json
{"version":1,"domain":"decoration","set":"/Game/_SoC/Environment/PCG/CityDecoration/DA_SoC_CityDecorationSet.DA_SoC_CityDecorationSet","operations":[]}
```

Supply a returned building class path to inspect its profile. The response includes GUID identities for variants and entries. Names are labels, not identities. New identities can be referenced as `$alias` later in the same batch.

```json
{
  "version":1,
  "domain":"decoration",
  "set":"<set object path>",
  "building":"<catalog building class path>",
  "operations":[
    {"op":"variant.add","name":"Facade","as":"v"},
    {"op":"entry.add","variant":"$v","asset":"/Engine/BasicShapes/Cube.Cube","location":[100,200,300],"as":"a"},
    {"op":"link.create","variant":"$v","entry":"$a","axis":"Y","as":"b"}
  ],
  "variant":"$v",
  "capture":true,
  "save":false
}
```

Positions are building-local centimeters. Rotations are `[pitch,yaw,roll]` degrees. Scale and scale multipliers are dimensionless. Symmetry planes come from the calibrated building placement volume. Existing target links preserve placements by capturing offsets; specified offsets override those values.

## Application and image contract

- Every batch uses the existing document methods on transient staged profile data. All operations and final profile validation must succeed before one live document transaction commits the variants. Failure identifies a zero-based operation index when applicable and leaves live variants unchanged.
- One Undo/Redo applies to the entire batch. Open decoration editors receive the normal property-change notification. A no-op creates no transaction.
- `dryRun:true` validates and optionally captures staged data without a live commit. It cannot be combined with `save:true`.
- `save:false` is the default. `save:true` saves only the selected profile package, including any already-unsaved changes in that profile. It does not save the set, map or unrelated packages. A save failure leaves applied changes undoable and is reported separately.
- `capture:true` draws a separate decoration preview and returns a native `FToolsetImage` PNG in the same tool response. It does not move the user's camera, open an asset editor, run PIE or regenerate a level.
- Optional `captureSize:[width,height]` supports integer sides from 128 to 2048. Optional camera requires `location`, `rotation` and optional `fov` (10–150). Otherwise the preview focuses the building. Capture parameters require `capture:true`.
- `variant` identifies the capture arrangement; it can be an existing GUID or a batch alias. Otherwise the variant explicitly selected by the last operation is used. An inspection-only capture should provide a variant.
- Output includes application/save/capture status, operation IDs and optional full state (`includeState:false` reduces output). Captures include camera metadata and projected entry pivot labels. Pivot labels identify projected positions, not an occlusion/visibility guarantee.
- Capture/readback/PNG failure is explicit and does not undo successfully applied edits. Rendering is unavailable under NullRHI. Check `captured` and `captureError` independently of application success; no stale screenshot is substituted.
- Requests require the editor game thread and no active PIE session. Version 1 rejects unknown domains, operations and fields. Limits: 256 operations and 262144 request characters.

## Verification

`Scripts/Validate-SoC.ps1 -Filter DeepLevelDesignPCG` builds and runs plugin automation. Tests cover atomic rejection, aliases, dry-run, one Undo/Redo, external editor notification, linked transforms, strict fields and native registration/schema. Its NullRHI run verifies explicit unavailable-render behavior; GPU capture verification uses the normally running editor, never the global -RenderOffscreen process flag. Interactive Editor/PIE acceptance remains separate.

Earlier decoration verification: initial editor build and all 85 plugin tests passed. After the preview-size correction, the editor build and all four decoration automation tests passed. With the corrected DLL loaded, `CaptureContract` also passed through the running editor's native MCP AutomationTestToolset with no errors or warnings. That GPU test verifies 256x256 metadata, PNG MIME and nonempty image payload, plus projected decoration pivots. The native `EditorToolset.EditorAppToolset.CaptureViewport` tool separately returned a nonempty PNG. No PIE, production-level test or production asset save was performed.

Fresh independent preview viewports are sized with the engine's `SetInitialSize` API. `SetViewportSize` requires a Slate window and cannot initialize an unattached preview. No window-size recovery fallback is used.

Do not launch GPU verification with `-RenderOffscreen`. That flag changes all native Slate windows into texture-backed windows; the project's FSR Frame Generation callback assumes a normal RHI viewport and asserts before the decoration test begins. This is distinct from reading pixels from a preview render target in a normally running editor. No FSR, engine or third-party code was modified.

For GPU verification after loading the current binary, use the existing editor's AutomationTestToolset: DiscoverTests, then RunTests with `DeepLevelDesignPCG.Editor.City.DecorationAutomation.CaptureContract`. This uses transient-fixture dry-run authoring, not PIE or a production-scene change. No separate offscreen editor process or upscaler configuration change is needed.

## Road and Building request envelope

Use `domain:"road"` or `domain:"building"` with `target` set to an exact **loaded editor actor path** or an existing catalog asset object path. Obtain loaded actor paths using the native Unreal scene tools; asset names and actor labels are not identities. `operations:[]` inspects the target without regenerating or changing it. There are still exactly two MCP tools.

Common options: `dryRun`, `save`, `capture`, `includeState`, `camera`, `captureSize`. Actor requests additionally support `generate:true`; Building Roadside requests support `align:true`. `timeoutSeconds` is 1..300, default 120. Actor dry-run validates authoring only: it never calls PCG/final alignment and cannot render staged geometry. Catalog dry-run can capture its staged preview. Capture options require `capture:true`; catalog `preview` is also capture-only.

### Road

- Network: `network.configure` sets City/catalog/collision profile. Grid origin and tile size remain City-owned, returned in inspection. No duplicate grid or graph-node seed authority is introduced.
- `branch.add`, `branch.update`, `branch.remove` operate on owned spline paths. New branches support `as`, later referenced as `$alias`; committed identities are component object paths. Points are world-space centimeters, must form nonzero grid-axis-aligned segments and lie on the City grid. `snap:true` explicitly snaps them. Arrays are limited to 4096 points.
- `cell.set` upserts a grid-cell override: `x/y`, `mode:add|modify|remove`, optional `kind:road|sidewalk`, mesh/material and tile-local transform. Add requires a mesh. `cell.clear` removes the override and restores normal planner ownership.
- Catalog: `catalog.configure` sets sidewalk width; `tile.add/update/remove/autoFit` manage mesh/material, connection and junction masks, weight and calibrated placement volume. Connection bits: +X=1, +Y=2, -X=4, -Y=8. Zero means sidewalk. Junction approach is zero or exactly one connected bit. `tile` is the **current staged array index**, so removals shift later indices. Half extents must be >=1cm and XY must be square.
- Catalog capture selects `preview:<tile index>`, default 0. Network capture uses a fresh editor-world viewport without moving the user's camera.

```json
{
  "version":1,"domain":"road","target":"<loaded RoadNetwork actor path>",
  "operations":[
    {"op":"branch.add","points":[[0,0,0],[2000,0,0]],"snap":true,"as":"branch"},
    {"op":"branch.update","branch":"$branch","points":[[0,0,0],[2500,0,0]],"snap":true},
    {"op":"cell.set","x":2,"y":0,"mode":"remove"}
  ],
  "generate":true,"capture":true,"save":false
}
```

Replace example coordinates with the inspected City grid's world coordinates. Owner notifications remain active: Road authoring can trigger its normal initial/editor generation even without explicit `generate:true`. The asynchronous call observes an immediately started generation; use `generate:true` whenever a fresh output/capture is required.

### Building

- Catalog: `building.add/update/remove/autoFit` address a PackedLevelActor class path. OBB `center/extent/rotation` is class-local; extent is a **half extent**, not full size. `exposure` keys positiveX/negativeX/positiveY/negativeY accept required/preferred/neutral/forbidden. Explicit `calibrated:true` marks manual calibration; AutoFit marks it automatically.
- `preset.add/update/duplicate/remove` address unique preset names. `buildings` is the ordered class-path array, so replacing it also reorders the sequence. Preset references must stay inside the staged catalog; delete/edit references in the same batch before removing a building.
- Catalog capture requires `preview:{"class":"<class path>"}` or `preview:{"preset":"<name>","seed":1337}`.
- Layout: `layout.configure` sets City/catalog, seed, variety, corner preference and corner mask. Setback, depth tolerance and boundary margin are Roadside-only. `line.update` edits BuildingLine world-space points and optional closed-loop state.
- Roadside: `frontage.generate` rebuilds from City snapshot and must be the last authoring command; new GUIDs are returned. `frontage.add/replace/update/remove/exclude` use frontage GUIDs and optional batch aliases. Automatic geometry is read-only: use replacement or exclusion. Manual additions can use custom world-space points, replacement can inherit an automatic frontage's points.
- `generate:true` calls the existing owner generation method, which regenerates frontages before Roadside placement. `align:true` then calls final volume alignment, or aligns already-current output in an inspection-only request. Alignment has its own Undo transaction. Response includes output-current state, rejects, placement transforms and stable placement identities from the provider contract.

```json
{
  "version":1,"domain":"building","target":"<BuildingPlacementCatalog object path>",
  "operations":[
    {"op":"building.add","class":"<PackedLevelActor class path>"},
    {"op":"building.autoFit","class":"<same class path>"},
    {"op":"preset.add","preset":"FacadePair","buildings":["<same class path>","<same class path>"]}
  ],
  "capture":true,"preview":{"preset":"FacadePair"},"save":false
}
```

```json
{
  "version":1,"domain":"building","target":"<loaded Roadside actor path>",
  "operations":[{"op":"layout.configure","seed":1337,"variety":0.8}],
  "generate":true,"align":true,"capture":true,"save":false
}
```

### Readiness, completion, cancellation and saving

Catalog authoring may be incomplete, exactly as in the editor. Inspection/authoring reports `validForGeneration` and `validation`; structural errors, unknown fields and invalid asset types reject the whole batch. Readiness is required before requested actor generation. `dryRun` does not claim to predict SpawnActor/PCG success.

`applied`, `generated`, `aligned`, `saved` and `captured` are separate facts. Successful authoring is not successful generation. The call binds PCG start/completion/cancellation before application, waits for a terminal event, then defers finalization past owner callbacks so Building output verification has completed. Timeout, cancellation, destroyed target, editor-world change or PIE start prevents saving and capture. Successfully applied authoring remains undoable; no automatic rollback or stale-image substitution is attempted.

Standalone `generation.cancel` cancels an active target component. Standalone `generation.cleanup` uses PCG's immediate editor cleanup and refuses active generation. They cannot be mixed with authoring, dry-run, save, capture, generation or alignment. An inspection request with `operations:[]` reports `generating` without waiting for somebody else's job.

Saving writes only the **target package**: a catalog package, external actor package, or actor-owning map package. It also writes preexisting unsaved changes in that same package. Referenced catalogs/City assets and unrelated packages are not saved. Generation-managed output packages are not recursively saved; if a project graph creates separate generated actor packages, their persistence remains the normal map/World Partition save workflow. Save failure is reported separately and does not roll back applied authoring.

GPU captures use fresh preview/editor-world FSceneViewport render targets sized with SetInitialSize and the existing Draw/GetViewportScreenShot path. They do not use SceneCapture2D, -RenderOffscreen, user viewport readback or any FSR/engine modification. NullRHI explicitly reports unavailable rendering.

Automation now also covers Road catalog transactions/strict schema/grid ownership, Building preset integrity/line ownership, exact two-tool native async schema, and synthetic PCG completion/cancel/timeout/delegate cleanup. Synthetic lifecycle tests do not run PCG or PIE; interactive production generation and GPU appearance remain separate acceptance checks.

Current Road/Building verification: the SoCEditor Development build passed and all 92 DeepLevelDesignPCG automation tests passed. Regression checks also cover reported world coordinates after a City-origin change, copied frontage positions/tangents, and pending-call cleanup at plugin shutdown. World capture framing includes the target's own PCG-managed actors; it does not scan the world for alternative output ownership. This run did not verify live MCP transport, Road/Building GPU capture or production generation. Restart the editor with the new binary before using the changed native async Execute signature.

Live read-only verification after editor restart: the native MCP server and toolset discovery responded successfully. Describe advertised exactly two tools and three domains. Execute returned success for existing Road and Building catalogs and the City Decoration Set with empty operations, dryRun:true, save:false and capture:false. All three responses reported applied/saved/generated/captured:false. Road and Building catalogs reported validForGeneration:true. No production asset/map was edited or saved; no generation, capture or PIE was started.
