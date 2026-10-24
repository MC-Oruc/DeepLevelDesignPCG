# Building decoration authoring

Use a Building Placement Catalog to calibrate buildings, then assign their decoration profiles through a City Decoration Set. A profile owns weighted variants; each variant owns decoration entries and transform links. Project meshes, Blueprints, decals and populated profiles remain host-project assets.

## Preview and camera

The building calibration and decoration viewports use the same exposure colors and labels: required is green, preferred blue, neutral gray and forbidden red. These guides describe the calibrated placement volume.

Exposure rules constrain building placement; profiles do not filter individual decorations against neighboring buildings. Use a required street-facing facade for essential signage. Decal poses describe the projector directly; size is its local X/Y/Z half-extents, with X controlling projection depth. The preview applies the same pose as generated output and refreshes the projector when its dimensions change.

Selecting a different building frames its bounds from the highest-priority exposure direction. Equally ranked adjacent faces produce a diagonal view showing both. Changing variants or refreshing the same building preserves the camera, so placements can be compared without losing the viewing angle.

## Selecting and transforming

Select decorations in the viewport or outline. Ctrl-click toggles a member; Shift-click adds one. Selected decorations share a transform gizmo for translation, rotation and scale. Group edits use one selection transform and preserve relative placement. The Details panel exposes the same selection through its group transform.

Duplicate, copy, paste and delete operate on the selection. Links belonging to copied entries are preserved within the copied group. Edits use the authoring document's undo/redo transactions; a gizmo drag commits as one edit.

Editor-specific shortcuts appear at the upper left of the viewport. Selection, coordinate space, grid step and symmetry mode appear at the upper right. The help changes with the active operation.

## Linked symmetry

Select decorations and open the viewport context menu. Start mirror placement for the selection, or link two existing selected entries while retaining their placements. Existing pairs expose a selectable cyan center between their members. Move this center with the gizmo to adjust the linked arrangement; synchronization settings control location, rotation and scale independently. Offsets allow different assets and asymmetric authored details without discarding the link.

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

Relevant regression suites are `DeepLevelDesignPCG.Editor.BuildingCalibration.*`, `DeepLevelDesignPCG.Editor.BuildingPresetEditor.*` and `DeepLevelDesignPCG.Editor.City.Decoration*`. Compilation and automation cover data and interaction contracts; inspect the actual viewport for visual acceptance.
