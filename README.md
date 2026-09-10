# DeepLevelDesignPCG

Reusable Unreal Engine 5.8 PCG authoring tools for deterministic road networks and building lines.

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
- `/DeepLevelDesignPCG/Road/BP_DeepLevelRoadNetwork`
- `/DeepLevelDesignPCG/Road/PCG_DeepLevelRoadNetwork`

The shipped graphs intentionally have no catalog assigned.

## Host Project Setup

1. Create a `Deep Level Building Placement Catalog` Data Asset and add calibrated Packed Level Actors.
2. Assign it to the Building Line node in a project-owned graph copy or graph instance.
3. Create a `Deep Level Road Tile Catalog` Data Asset and calibrate the road and sidewalk tile set.
4. Assign it to the Road Network node in a project-owned graph copy or graph instance.
5. Place the corresponding Blueprint actor and author its spline geometry.

Catalogs are project data. They contain mesh/class references, placement volumes, exposure rules, connectivity, weights, and calibration state; they are not copied into the plugin.

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
