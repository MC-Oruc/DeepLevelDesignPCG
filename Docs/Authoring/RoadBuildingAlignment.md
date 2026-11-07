# Roadside alignment and sidewalk infills

Generate Buildings prepares and verifies building placements. Align Buildings Vol Final moves eligible placements and computes the vacated footprint surfaces. Road Network consumes these surfaces as sidewalk infills.

The roadside building actor owns the serialized generated buildings and sidewalk infill anchors. PreparedLayout is temporary generation input, never the only persistent source of generated infills. Verified completion publishes the output, refreshes the City Layout snapshot and notifies Road Network. Cleanup and duplication clear the generated output.

If Road Network is already generating when an infill change arrives, it coalesces the change and generates again from the latest snapshot after completion. No per-frame polling is used.

Save the completed building and road actor packages. In the level viewport, Game View hides authoring helpers; spline and grid show flags also affect visibility. Frontage splines retain their native drawing and selection behavior.

Automation: `DeepLevelDesignPCG.Editor.BuildingLine.SidewalkInfillPersistence` verifies serialized infill publication without a prepared plan. RoadNetwork and BuildingLine tests cover geometry and planning. For live acceptance, wait for both building and road generation to finish before reading counts or saving; the building DSL's combined generate/align result can precede the second asynchronous generation's completion.
