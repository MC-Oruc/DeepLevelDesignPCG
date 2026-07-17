# DeepLevelDesignPCG

Reusable Unreal Engine 5.7 PCG authoring tools for deterministic road networks and building lines.

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

## Module Boundaries

- `DeepLevelDesignPCG`: runtime actors, catalogs, planners, solvers, PCG nodes, and deterministic tests.
- `DeepLevelDesignPCGEditor`: asset actions, Details customization, calibration editors, viewport authoring, notifications, and editor integration tests.

Runtime code has no dependency on Slate or host-project modules. Editor feedback crosses the module boundary through one editor-only event. There are no compatibility adapters, runtime recovery paths, or duplicated host implementations.

## Source Artwork

Editable PNG sources and final runtime/editor images are under `Resources/Icons`. `Resources/Icon128.png` is the plugin browser icon; `Content/Editor/Icons/T_RoadNetwork` is the editor-only Road Network marker texture.
