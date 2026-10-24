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

## Linked symmetry

Select decorations and open the viewport context menu. Start mirror placement for the selection, or link two existing selected entries while retaining their placements. Existing pairs expose a selectable cyan center between their members. Move this center with the gizmo to adjust the linked arrangement; synchronization settings control location, rotation and scale independently. Offsets allow different assets and asymmetric authored details without discarding the link.

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

Closing the workspace or exiting Unreal stops preview rendering and releases its actors before the preview world is torn down.

The preview ground matches the calibrated placement-volume base used by Building Network.

Relevant regression suites are `DeepLevelDesignPCG.Editor.BuildingCalibration.*`, `DeepLevelDesignPCG.Editor.BuildingPresetEditor.*` and `DeepLevelDesignPCG.Editor.City.Decoration*`. Compilation and automation cover data and interaction contracts; inspect the actual viewport for visual acceptance.

## Facade visibility

Building Network publishes its calibrated oriented placement volumes. After all providers have contributed to the shared snapshot, Building computes the free regions between those footprints. Regions connected to the outside or containing published road cells expose their adjacent facade intervals. Enclosed courtyards remain hidden; sidewalk infill alone does not open them. The computation uses polygon geometry rather than rasterizing openings at the city tile size.

This is ground-plan frontage visibility. Upper floors above shorter neighbors and complete decoration mesh clearance are not evaluated by this contract. The entry's actor-local anchor point determines membership in the exposed interval. Snapshot changes recompute the topology across providers and invalidate affected buildings, including a distant courtyard opened by another provider.

Each profile variant remains a complete weighted arrangement. Each entry can independently select a facade association:

- `bAutoFacade=true` is the default. Runtime chooses the geometrically nearest calibrated OBB facade to the entry's actor-local point. If that chosen face is forbidden or the point is occluded/outside its visible interval, the entry is omitted; resolver does not try another face.
- With `bAutoFacade=false`, `Facades` must contain unique valid faces. The entry is emitted when any listed face considers its actor-local point visible. This supports intentional corner placement.
- There is no facade bypass for ground props. They use automatic assignment and follow the same visibility rules.
- Building `Required` and `Preferred` exposure remain placement rules and do not change. Building-owned forbidden exposure and facade visibility determine decoration eligibility.

The decoration point is `LocalTransform`'s translation in building actor-local space. Visibility is supplied by the canonical building layout snapshot; decoration resolution must not load source catalogs or query the world for geometry.

DSL `entry.add` and `entry.update` accept `facade` as exact string `"auto"` or a nonempty array of exact enum names: `PositiveX`, `NegativeX`, `PositiveY`, `NegativeY`. Omission preserves the existing entry value on update. Explicit arrays use OR semantics. Empty arrays, repeated names, invalid names, and other strings are rejected. State emits the same representation.
