# Road Network traffic lanes

Road Network remains the source of road geometry and reciprocal cell connections. ZoneGraph is a derived output. The runtime road plugin exposes `BuildRoadPlan`; its Editor module converts the final plan into native `UZoneShapeComponent` instances owned by that Road Network. Consumers use Epic ZoneGraph, not the PCG actor or a duplicate navigation graph.

## Setup

Enable ZoneGraph. Define native lane profiles in Project Settings > Zone Graph:

- Single-tile profile: two equal lanes, ordered Forward, Backward; total width equals the City grid tile size.
- Double-tile profile: four equal lanes, ordered Forward, Forward, Backward, Backward; total width equals twice the tile size.

Assign these to the Road Network's `TwoWayLaneProfile` and `WideLaneProfile`, then enable `bGenerateTrafficLanes`. Profiles own lane dimensions and tags. Road Network references them; it does not maintain a second set of lane widths. SoC's 500 cm grid uses 250 cm lanes and the `Vehicle` ZoneGraph tag in project-owned Config/DefaultPlugins.ini.

Traffic follows the right side of the road regardless of spline authoring direction. Parallel tiles form one physical cross-section: a single tile has 1+1 lanes and a double tile has 2+2 lanes. Junctions and width transitions share a native polygon so opposite directions remain connected when a road narrows. Epic generates lane curves, links and the spatial index.

## Authoring and ownership

Generate the road after geometry edits. Traffic export runs after road PCG generation completes, instead of rebuilding hundreds of components during each spline drag. Traffic profile edits, Undo and City grid-origin changes also refresh the derived output. Export replaces only shapes marked `DeepLevel.Road.Traffic` on its own actor; other authored ZoneShapes are preserved.

Save the generated ZoneGraphData actors and road owner. Native ZoneShapes are editor-only. Cooked gameplay uses serialized `AZoneGraphData`; it does not regenerate roads or repair missing navigation at runtime. This integration supports one- and two-tile road cross-sections. Invalid profiles or an isolated junction without approaches produce an explicit authoring error.

## Verification

`DeepLevelDesignPCG.Editor.RoadNetwork.ZoneGraph` checks right-hand lane directions, output replacement, native cell connections and a 2+2-to-1+1 return route through Epic A*. Existing road authoring/planner tests remain applicable. SoC owns vehicle control and driving simulation tests; this reusable plugin does not depend on SoC.

Traffic spawning, signals, junction priority, dynamic lane reservations and vehicle motion planning are separate consumers/policies. Enabling ZoneGraph does not spawn ambient traffic.
