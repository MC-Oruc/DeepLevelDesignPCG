# Window lighting authoring

Open the existing Building Decoration editor for the city's decoration set. Use the permanent Window Lighting tab, then select a building. Its preview and selection are independent from Building Decorations, with the same root save scope.

- Select the shared window material, then Import / refresh whole windows. Only separate translucent glass surfaces are accepted; opaque facade panels, door glass and entrances are excluded. Source geometry and materials stay unchanged.
- Each row represents an opening assembled from nearby coplanar panes. Checkbox switches that opening. Inspector exposes enabled state, window color, brightness and the existing transform/render settings. Undo/Redo uses the decoration document.
- Apply fill uses density and seed to choose eligible upper-floor openings. Short transoms/canopies start off. The saved switches, not a runtime random generator, drive output. Reimport preserves switches/color/brightness by source identity.
- Window arrangements use the independent NightWindows selection group. Existing base arrangements remain visible as preview context and remain independently selected in the city. Save assets, then regenerate city decoration to update map output.
- Imported geometry is cached in WindowModules beside the building profile. Geometry is shared across arrangements; tint and brightness use four instance custom-data floats. No per-window material instance, light actor, shadow, collision, navigation or tick is required. Draw distance is 160 m by default.

Opaque windows embedded in wall materials cannot be inferred safely. Import reports this limitation. Use explicit authored window geometry/placements in the decoration workflow rather than enabling arbitrary opaque facade sections. Artists must check opening grouping for unusual source geometry; geometry heuristics cannot infer architectural intent.

Source: editor owns extraction, asset creation and document changes; runtime only resolves enabled decorations and their instance data. Geometry Scripting dependencies remain editor-only. Existing masters, source meshes and previous merged-window assets are preserved.


## Appearance palette and correct window pivots

Appearance palette / import material opens the selected profile's window settings. Add shared materials with names, RGB tint, brightness and relative weight. Apply fill uses the chosen density/seed for eligible upper-floor windows and independently chooses an appearance by weight. Same seed and entry identities reproduce the same result; no runtime randomization or per-window material-instance allocation is required. Individual entries retain editable material override, color and brightness. Four shared project examples are warm curtain, neutral blinds, cool glass and dim amber.

Imported modules now have their pivot at the actual glass center. The matching entry transform compensates the mesh translation, preserving world-space geometry under rotation/scale. This is required because native facade visibility uses the entry position. Old source-space pivots could classify the wrong facade. Reimport uses a versioned new geometry asset path and preserves old mesh/material assets. Original base arrangements stay unchanged.

The window preview starts under subdued lighting so emission differences are visible. It remains an authoring preview; final bloom and exposure depend on map post-processing. Automatic extraction still requires separate translucent glass; opaque window textures embedded in walls are not guessed or illuminated as arbitrary facade rectangles.
