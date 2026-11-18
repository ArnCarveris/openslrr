# Rendering overhaul (#201)

The active tracker is https://github.com/EliasFD3S/openslrr/issues/201.
This document records delivered increments separately from planned systems.
HDR/exposure/tonemapping and image effects are tracked in #193.

## First increment: coherent shadow reception

Previously vehicles received sunlight without sampling the map shadow, while
world reflection-capture views explicitly skipped shadow reception. Scenery and
vehicles now share one visibility function and CPU binding contract. Captured
world meshes and prop batches receive the same field as the main camera.
The existing garage shadow field identifies its local source; its occlusion
attenuates that lamp, not unrelated sunlight. Ambient/environment illumination
is preserved. Preview and emissive vehicle draws opt out; vehicle receivers do
not sample the separate car-depth map containing themselves.

The GPU regression uses a controlled depth field to isolate receiver behavior.
It verifies actual production shader output, including view 72 used for
reflection captures, not only program linkage. The city smoke exercises the
normal game path but is not a complete before/after visual or FPS comparison.

## Still outstanding

- Matched baseline scenes and diagnostic views for lighting, normals, shadow
  coverage and cubemap faces; dealer, garage and addon scene validation.
- Stable directional cascades, useful distance, camera/sun update policy,
  alpha foliage, moving casters and receiver bias validation. Existing range
  and cache thresholds are unchanged in the first increment.
- Shared scene light records and real headlight/street/interior emitters,
  including their controls, damage, occlusion, lifetime and GPU budgets.
- Linear-light PBR material representation, legacy SCX adaptation, clearcoat,
  roughness filtering and a specified optional material-format extension.
- Sky geometry independence, continuous Scene.java inputs and cloud quality.
- Complete cubemap/scene-light consistency and capture update performance;
  the first increment fixes receiver shading but does not certify every face,
  probe, material or interior. Reflection resolution and far clip are unchanged.
- Retire legacy simulated beams/shadows only as their replacements work.

Scene.java was inspected: it selects discrete sky/light/fog/environment presets
and skips exterior sunlight setup for internalScene. No Java workaround is
needed for the receiver defects fixed here; no Java patch is shipped yet.

## Validation contract

Use copies of game data/saves for diagnostic runs. Report build, pixel tests,
manual visuals and timing separately. Do not close #201 on the strength of one
increment or turn an unvalidated screenshot into a compatibility claim.
