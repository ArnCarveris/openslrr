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

## Second increment: radial cascades and continuous sun updates

Outdoor shadows now use four camera-centred radii: 32, 100, 320 and 1,000 m.
A 4096-square D32F atlas contains four 2048-square tiles (64 MiB depth payload,
compared with 16 MiB for the prior outdoor map). The last 15% of each inner
radius crossfades to the next, and the outer shadow fades over 900-1,000 m.
Receiver filtering samples the selected level and its neighbour; bias is
expressed as 2 cm along the light depth plus the receiver-plane correction.

The light-space basis is independent of camera orientation. Its X/Y translation
snaps to tile texels, and the sun projection is refreshed every frame, replacing
the old eight-metre/angular cache gates. This removes those particular temporal
thresholds; complete motion/shimmer validation is still required in game.
These are radial distance cascades, not tightly fitted camera-frustum splits.

Shadow frusta collect/upload off-camera scenery casters. Their mesh cache entries
survive into the following frame to avoid upload churn and are released after
leaving shadow/main-view usage (with one frame of retirement overlap). Vehicle
casters still use the separate nearby vehicle-depth path; this increment does
not add distant cars or skinned pedestrians to the scenery atlas. Garage local
shadows retain their single map. Existing exterior scene-detection limitations
for dealers/addons remain; a 1 km receiver range does not extend the main camera
far clip or guarantee geometry streaming at that distance.

GPU tests cover distinct atlas tiles, radial selection at 10/70/220/700 m,
blend values, final fade and sub-texel camera stability/small sun changes.
A city smoke and captures exercise production submission. Four refreshed passes
increase CPU submission/collection work; see the validation evidence for measured
cost. They must not be described as a free quality/performance improvement.

## Third increment: CPU work and texture filtering

The cascade implementation now collects outer candidates once, skips unused
character palette completion and filters render-space bounds for each level.
Cached depth groups merge contiguous opaque index ranges across material
changes; cutout settings and transparent holes retain their boundaries. None of
these optimizations lowers the four cascade radii or their update frequency.

The comparison footprint is now a symmetric nine-tap tent to reduce thin-caster
aliasing. GPU tests include a one-texel stripe shifted across the footprint;
this does not establish that every reported temporal shimmer is resolved.

Anisotropic world-texture filtering is exposed in the patched video menu and
debug overlay, off/hardware maximum, default on, persisted through the existing
host options file. It is separate from shadow comparison filtering. See
HOST_NATIVE_UTILITIES.md for index 6 and assets/validation/shadow-performance-filtering.txt
for tests and measurements. Previous timing numbers remain historical evidence,
not the current performance claim.

## Stationary moving-sun edge follow-up

The user confirmed that stopping the clock stops the remaining shimmer. A
stationary far-origin numeric sequence reproduced almost a one-texel grid jump
from tiny sun changes. A nearby floating origin reduces that discontinuity;
caster model transforms and receiver coordinates now use the same origin. A
separate depth vertex shader avoids combined-MVP cancellation of large world
positions. Camera travel can rebase the origin on a common multiple of all
cascade texel sizes, preserving grid phase.

Comparison sampling also replaces raw interpolated-depth thresholding for the
nearby vehicle shadow. Exact unchanged-state atlas reuse avoids redundant draws
when matrices, origin and caster records agree, while any small sun/caster change
invalidates it. Real-map and pixel/projection regressions cover these contracts.
This targets identified defects; full temporal acceptance on the user's view
remains open. See assets/validation/shadow-origin-stability.txt for evidence.

## Still outstanding

- Matched baseline scenes and diagnostic views for lighting, normals, shadow
  coverage and cubemap faces; dealer, garage and addon scene validation.
- Broader cascade motion/quality validation: alpha foliage, moving casters,
  sunset/low angles, split transitions and bias on actual assets; reduce CPU
  collection/submission cost without reintroducing visible temporal jumps.
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
