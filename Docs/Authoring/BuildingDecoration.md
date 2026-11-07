# Building decoration authoring

Use a Building Placement Catalog to calibrate buildings, then assign their decoration profiles through a City Decoration Set. A profile owns weighted variants; each variant owns decoration entries and transform links. Project meshes, Blueprints, decals and populated profiles remain host-project assets.

## Preview and camera

The building calibration and decoration viewports use the same exposure colors and labels: required is green, preferred blue, neutral gray and forbidden red. These guides describe the calibrated placement volume.

Exposure rules constrain building placement. Building profiles also filter individual entries using published facade visibility. Fit signage to the existing street-facing frontage without changing building exposure or calibration. Decal poses describe the projector directly; size is its local X/Y/Z half-extents, with X controlling projection depth. The preview applies the same pose as generated output and refreshes the projector when its dimensions change.

Selecting a different building frames its bounds from the highest-priority exposure direction. Equally ranked adjacent faces produce a diagonal view showing both. Changing variants or refreshing the same building preserves the camera, so placements can be compared without losing the viewing angle.

## Selecting and transforming

Select decorations in the viewport or outline. Ctrl-click toggles a member; Shift-click adds one. Selected decorations share a transform gizmo for translation, rotation and scale. Group edits use one selection transform and preserve relative placement. The Details panel exposes the same selection through its group transform.

Duplicate, copy, paste and delete operate on the selection. Links belonging to copied entries are preserved within the copied group. Edits use the authoring document's undo/redo transactions; a gizmo drag commits as one edit.

Editor-specific shortcuts appear at the upper left of the viewport. Selection, coordinate space, grid step and symmetry mode appear at the upper right. The help changes with the active operation.

Drag edits validate the complete candidate arrangement before committing. Symmetry validation indexes entry identities once per candidate so each link resolves its endpoints without rescanning the arrangement.

## Linked symmetry

Select decorations and open the viewport context menu. Start mirror placement for the selection, or link two existing selected entries while retaining their placements. Existing pairs always show subdued cyan links and a small selectable center, including when neither member is selected. Selecting a member brightens its link and enlarges the center; selecting the center highlights it yellow. This keeps other pairs discoverable while building a multi-selection with Ctrl-click or Shift-click on decorations. Move the center with the gizmo to adjust the linked arrangement; synchronization settings control location, rotation and scale independently. Offsets allow different assets and asymmetric authored details without discarding the link.

Mirror Geometry reflects the complete mesh using signed scale. Live decoration and ghost previews refresh their render state when transform handedness changes, preserving outward-facing surfaces when mirroring is toggled or undone.

New mirror placement shows ghost copies, the proposed center and extended axis guides before committing. Move the cursor to position the center:

| Input | Effect |
| --- | --- |
| Approach an axis guide | Attract placement to that axis |
| X / Y / Z | Lock or unlock that axis |
| Hold Shift | Free placement without axis attraction |
| Left click | Commit the preview |
| Escape / right click | Cancel without changing the profile |

Axis placement follows the viewport's local/world coordinate mode and Unreal's enabled translation-grid snap step. A preview that cannot produce valid linked copies cannot be committed. Preview actors have no collision, navigation or lighting contribution and are discarded on cancel.

## Saving and generation

Save the edited profiles and set explicitly. Previewing a profile does not regenerate the city. Use the owning City Layout's decoration regeneration action, then save the level to update baked output. Native automation edits the same authoring document; see [MCP](../Automation/MCP.md).

Restart restoration records only the set originally opened as the workspace. Profiles and other sets visited inside that workspace remain in its save scope without reopening as separate asset editors.

Closing the workspace or exiting Unreal stops preview rendering and releases its actors before the preview world is torn down.

The preview ground matches the calibrated placement-volume base used by Building Network.

Relevant regression suites are `DeepLevelDesignPCG.Editor.BuildingCalibration.*`, `DeepLevelDesignPCG.Editor.BuildingPresetEditor.*` and `DeepLevelDesignPCG.Editor.City.Decoration*`. Compilation and automation cover data and interaction contracts; inspect the actual viewport for visual acceptance.

## Facade visibility

Building Network publishes its calibrated oriented placement volumes. After all providers have contributed to the shared snapshot, Building computes the free regions between those footprints. Regions connected to the outside or containing published road cells expose their adjacent facade intervals. Enclosed courtyards remain hidden; sidewalk infill alone does not open them. City Layout owns **Facade Seam Tolerance** (default10cm). Polygon closing seals narrower placement seams for connectivity, then restores the original footprint boundary. It does not move buildings or close larger authored passages. The computation uses polygon geometry rather than rasterizing openings at the city tile size.

This is ground-plan frontage visibility. Upper floors above shorter neighbors and complete decoration mesh clearance are not evaluated by this contract. The entry's actor-local anchor point determines membership in the exposed interval. Snapshot changes recompute the topology across providers and invalidate affected buildings, including a distant courtyard opened by another provider.

Each profile variant remains a complete weighted arrangement. Each entry can independently select a facade association:

- `bAutoFacade=true` is the default. Runtime chooses the geometrically nearest calibrated OBB facade to the entry's actor-local point. If that chosen face is forbidden or the point is occluded/outside its visible interval, the entry is omitted; resolver does not try another face.
- With `bAutoFacade=false`, `Facades` must contain unique valid faces. The entry is emitted when any listed face considers its actor-local point visible. This supports intentional corner placement.
- There is no facade bypass for ground props. They use automatic assignment and follow the same visibility rules.
- Building `Required` and `Preferred` exposure remain placement rules and do not change. Building-owned forbidden exposure and facade visibility determine decoration eligibility.

The decoration point is `LocalTransform`'s translation in building actor-local space. Visibility is supplied by the canonical building layout snapshot; decoration resolution must not load source catalogs or query the world for geometry.

DSL `entry.add` and `entry.update` accept `facade` as exact string `"auto"` or a nonempty array of exact enum names: `PositiveX`, `NegativeX`, `PositiveY`, `NegativeY`. Omission preserves the existing entry value on update. Explicit arrays use OR semantics. Empty arrays, repeated names, invalid names, and other strings are rejected. State emits the same representation.


## Combining arrangements

A variant's **Selection Group** defines an independent weighted choice. `None` retains the original base arrangement. Named groups add one arrangement each; an empty variant can represent an absent optional layer. Each group needs positive total weight. Group names are case-insensitive; independent seeds preserve base arrangement selection and placement identities. The inspector previews the selected variant, while city generation composes all groups. This does not rebuild anything at runtime.

The native DSL accepts `group` for variant add/update/duplicate and includes the group in state inspection. The same document owns transactions and validation. Existing profiles load with `SelectionGroup=None` and require no migration.

## Streets and sidewalks

The City Decoration toolkit has three permanent tabs: **Building Decorations**, **Window Lighting**, and **Streets & Sidewalks**. It lists every category linked to the current city set, including street furniture, street lighting, road markings, roadside props and surface decals. Select a category to edit its actual asset in the native Details view: output asset, anchor tags/occupancy rules, probability, spacing, clearance, cadence, local transform and rendering/collision policy.

Use the asset picker to link an existing category, New category to create one, or Unlink category to remove its membership without deleting the asset. Set membership is owned by the existing decoration document; native property transactions own category contents. Both support Undo/Redo. Linked categories are included in the toolkit's save scope. Changing the city set refreshes the list and removes stale property selection.

Save the assets, then regenerate city decoration using the existing city generation workflow. This tab does not auto-regenerate the city on every keystroke and introduces no runtime behavior.

### Road-network map preview

Streets & Sidewalks includes a real-mesh 3D viewport plus an interactive 2D plan of the actual City Layout snapshot: roads, sidewalk cells, building occupancy context, optional anchor segments and selected-category decoration placements. Contiguous cells of the same type share a draw rectangle. It is a schematic plan, not a rendered mesh/material preview.

Select a City Layout associated with the current decoration set. A single matching editor-world layout is selected automatically; multiple matches require an explicit choice. Only loaded editor-world layout providers contribute. Refresh road data explicitly rebuilds the native snapshot. The full native decoration resolver uses the source city's seed and overrides before filtering the output to the selected category; cross-category slot competition/clearance therefore match normal generation.

Wheel zooms around the cursor; right/middle drag pans; F/Frame all fits the plan. Green markers and headings indicate resolved placements. Clicking one highlights all placements for its rule in yellow and scrolls/highlights the corresponding Entries row in native Details. Finishing a property edit or Undo/Redo refreshes the preview. No per-frame distribution calculation, map regeneration, light actors or preview meshes are introduced.


## Workspace ownership and 3D inspection

One registered asset toolkit owns two non-toolkit workspace controllers (building and windows) plus the street panel. Building/window widget trees, preview scenes and selection are independent. Both edit the same profile assets, use the existing document transactions and share the root toolkit save scope. Changing the City Set updates both contexts. No Slate content is moved between tabs and no unregistered asset toolkit is used as a view.

The street 3D preview copies actual static-mesh road/sidewalk components from the selected road network, including source material overrides and instance transforms. The selected category uses native resolved placements rendered as actual mesh, actor or decal outputs. It is an isolated editor preview, not a second gameplay simulation or a modification of the level. Scene lighting is a preview environment, not the map's complete lighting setup. Reference buildings and other categories are not duplicated. Inactive 3D/plan tabs do not keep the 3D preview visible.

Use native Unreal viewport navigation and F/Frame to inspect the layout. Clicking an actual decoration or its rule marker selects the corresponding category entry; selected-rule markers and bounds are highlighted. The plan view remains available for global placement/anchor inspection. Night preview changes only the street preview scene.

## Group focus and lighting preference

Street decoration now resolves all linked categories together and displays all of their outputs. Initial selection is empty. Ctrl/Shift selects multiple categories; selected groups retain their original preview materials and shadows, while other groups use shared translucent context materials. This is an editor visualization override only; source meshes, source materials and generated level output are untouched. Decals use a separate context decal material. Context lights do not illuminate focused groups. Clearing selection restores every original appearance. Changing focus does not re-resolve or respawn decorations.

Clicking a decoration in either preview selects its owning category and scrolls to its rule. The right inspector edits the active category; multi-selection controls visibility focus. Daylight and Nightlight explicitly select preview lighting. New workspaces default to night, and the choice is stored under DeepLevelDesignPCG.DecorationPreview / NightLighting in per-project user settings. These settings do not change game lighting.

## Preview shutdown lifecycle

Street preview owns a scene through its viewport client until that client is released. OnEditorPreExit stops/detaches rendering and clears preview actors/components before engine world cleanup. Later Slate destruction is idempotent and does not call DestroyActor on an already cleaned world. The same shutdown path handles ordinary workspace closure. Regression coverage includes a retained client after widget shutdown and final scene release.

Window overlays use the same published facade spans and forbidden faces as other building decorations. The resolver additionally rejects decoration origins inside another published building OBB, accounting for rotation, scale and height. This closes the recessed-origin case that an outer-face interval projection alone cannot detect; points above the neighboring roof remain eligible. Candidate neighboring bounds are collected once per building during generation, not queried per frame.

Current authored window arrangements target roughly25percent of room groups lit, preserve split panes within each room, and use75percent of their previous emissive intensity. The original profiles are preserved under Saved/Automation/WindowPolicy/Before. Window entries have MaxDrawDistance=0 so they do not impose an independent160m cutoff. Native World Partition/HLOD streaming remains responsible for distant representation; regenerate the existing Instancing HLOD layer after changing generated arrangements.

### Shared window definitions

`WindowDefinitions` owns each window's mesh, building-local transform, facade/rendering policy and room identity once per building profile. Variants hold `WindowStates` referencing those definitions: state identity, enabled flag, tint, emissive brightness and material. Missing state means off. Disabled states are omitted before render component/instance creation.

In **Window Lighting**, use **Shared window definitions & appearance palette** for geometry and palette authoring. Variant rows are window-state checklists; their inspector shows only enabled state and appearance. Room-linked parts switch and fill together. Geometry edits affect every referencing arrangement. Ordinary building decoration entries retain their existing ownership.

The editor document projects definitions/states into its existing preview editing representation and compacts the result within the same Undo transaction. The saved and cooked asset never needs repeated window geometry inside variants. Existing full-layout input is explicitly imported through `SetWindowArrangements`; there is no runtime/PostLoad legacy reconstruction. Validation rejects dangling references and window geometry left in ordinary variant entries.

The migration preserved4570existing state records and placed geometry in2969shared definitions. Backups of all15profiles and4complete material masters live under `Saved/Automation/WindowRefactor/Before`. Materials use restrained warm/neutral/cool indoor-white palettes, soft curtain gradients and filtered blind detail. No runtime lights or ticks are added.
