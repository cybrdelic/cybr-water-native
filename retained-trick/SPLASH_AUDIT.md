# Splashdown review — existing retained-trick render

No source changes or replacement render were made during this review.

## Direct observations

At 9.25 s the rebound contains broad perforated sheets with chunky rims. At 9.83 s detached droplets appear as repeated, similarly sized beads. These observations do not alone establish the cause of the rebound height or validate physical breakup.

## Evidence from supplied source and records

Source archive SHA-256: `a01760c0fd4c908ef21136c214e3cae3175ec527ae583e6c111bec9ffe98d58e`.

- Uniform MAC spacing: 4.5 cm; marker spacing: 2.25 cm.
- All 38027 stored primary-marker volumes at frame 228 have the same value: 1.13906253318e-05 cubic metres.
- `src/surface.cpp:475` exports isolated primary markers as spheres with `radius = cbrt(3 * volume / (4 * PI))`. This gives a diameter of 2.79158 cm for every such sphere in this cache. This is a marker-volume rendering choice, not a predicted fine-spray size spectrum.
- Source README states reseeding and secondary whitewater are disabled for this take.
- Surface extraction uses a 1.8 cm lattice and five scalar-field smoothing passes; support radius is approximately 8.1 cm before anisotropic transformation. Neither physical resolution nor breakup accuracy follows from triangle count or total-volume agreement.
- Closed lateral numerical boundaries are in `gallery_scenes.hpp:642`; marker handling in `fluid.hpp:585–586` clamps an out-of-bounds position just inside a closed face and zeros outward normal velocity. These walls have no corresponding rendered box.
- The first sampled state with markers within 1 cm of a domain face is frame 215 (8.95833 s). The record counts 99 markers. Minimum distance is 0.0037756 m.
- The peak sampled count within 1 cm is 2503 at frame 226 (9.41667 s). Proximity counts are not exact collision-event counts.
- Verification explicitly acknowledges post-release boundary contact, no adaptive refinement, no resolved surface tension or coupled air, and no physically modeled diffuse whitewater.

## Rendering records

|   frame |   seconds |   isolated_spheres |   secondary_parcels |   smoothing_passes |   voxel_cm |   support_radius_cm |
|--------:|----------:|-------------------:|--------------------:|-------------------:|-----------:|--------------------:|
|     204 |   8.5     |                  9 |                   0 |                  5 |        1.8 |                 8.1 |
|     210 |   8.75    |                  0 |                   0 |                  5 |        1.8 |                 8.1 |
|     216 |   9       |                 14 |                   0 |                  5 |        1.8 |                 8.1 |
|     222 |   9.25    |                 99 |                   0 |                  5 |        1.8 |                 8.1 |
|     228 |   9.5     |                277 |                   0 |                  5 |        1.8 |                 8.1 |
|     239 |   9.95833 |                246 |                   0 |                  5 |        1.8 |                 8.1 |

## Interpretation and limits

The artificial boundary and equal-volume sphere export are confirmed implementation limitations. Large support and smoothing can contribute to thickened features; their contribution here needs a same-state reconstruction comparison. A pressure/kinetic-energy defect at impact is not established by this review; it needs an energy/work and collision audit. The render is deterministic, so these visible defects cannot be dismissed as the earlier Monte Carlo grain.

## Proposed correction order

First move artificial boundaries outside the entire splash excursion or model visible containment with accounted outflow. Compare the same impact without boundary contact. Next resolve and reconstruct the impact sheet at sufficient thickness resolution, with local support and smoothing that do not bridge air gaps; validate detached-liquid parcel sizes rather than treating each coarse marker as one physical droplet. Finally improve aeration/foam and exposure after the geometry and motion are credible.
