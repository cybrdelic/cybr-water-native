# CYBR WATER — open-domain splash repair

This revision continues the recovered native CYBR WATER engine. It uses the
existing quadratic APIC / staggered MAC pressure solver, simulated liquid-marker
states, anisotropic 3D surface reconstruction, and native deterministic
reflection/refraction renderer. It does not use image generation, motion
interpolation, repeated frames, or image-space denoising.

## Delivered shot

`CYBR_WATERBENDING_OPEN_SPLASH_10S.mp4` is a ten-second, 960 × 540, 24 fps video.
Its 240 images correspond to actual solver states at times n/24, n = 0…239.
State 240 is the additional ten-second endpoint used for numerical checks; it is
not added as a 241st video frame. Full decoding, timestamps, state/image hashes,
frame uniqueness, source reconstruction records and nonfinite-path checks are
recorded in `VERIFICATION.json`.

## Changes from the retained-trick baseline

1. **Remove invisible lateral walls.** All four horizontal faces are open. The
   physical bed and rocks remain the collision geometry and are also rendered.
   The region is enlarged before the splash can reach its numerical exits. The
   final measured sequence has no primary-liquid outflow and no markers within
   two grid cells of a lateral boundary. See the measured minimum clearance in
   the verification file; keeping mass is not used as the sole wall test.
2. **Finer actual fluid state.** The uniform velocity grid is 3 cm instead of
   4.5 cm. Three samples per grid axis produce 1 cm initial marker spacing and
   439,737 primary markers, rather than 38,027. This is real resimulation, not
   visual subdivision of the previous mesh. The initial seeded liquid volume
   changes slightly with sampling: 0.439737 m³ here versus 0.433151 m³ previously.
3. **Coherent fragments.** The premature neighbour-count ≤3 rule is replaced,
   when `--coherent-fragments` is enabled, by nearest-distance hysteresis tracked
   through persistent primary-marker IDs. Detach above 3.2 marker spacings;
   rejoin below 2.2. Sparse connected ligaments remain reconstructed liquid.
   The original classifier is retained for tests and explicit comparisons.
4. **Less inflated reconstruction.** The surface lattice is 1.2 cm, support
   radius 3.2 cm and scalar smoothing three passes. The prior settings were
   1.8 cm, 8.1 cm and five passes. The numerical volume-correction acceptance
   check is retained for every frame. No radius-only shrinking hides liquid.
5. **Flow-derived secondary samples.** The existing spray/foam/bubble transport
   is enabled at rate 18,000 with capacity 180,000. These are one-way visual
   samples, not a second physical-liquid mass, a coupled air-pressure solve,
   or a validated droplet-size distribution.

## Exactly what happens at 9 seconds

The first domain is [-3.3, -0.12, -2.52] to [3.3, 3.3, 2.52] metres. It remains
open laterally. At frame 216 / 9 seconds, the saved liquid is transferred into
[-6, -0.12, -5.1] to [4.02, 2.7, 7.5] metres at the same 3 cm grid spacing.
This avoids visible liquid reaching an open outlet during the final second.

A byte-level test verifies that **every primary position, velocity, volume and
ID is unchanged by this handoff**. Saved secondary positions, velocities, radii,
ages, lifetimes and type are also imported. The display snapshot does NOT carry
APIC affine matrices or pressure warm-start data: affine velocity gradients are
reconstructed from the transferred velocity field and pressure starts anew.
This is an explicit numerical domain transfer, **not a bitwise checkpoint
continuation**, and not proof of full affine-energy conservation. The render
mesh is reconstructed from the resulting evolving states on both sides.
`DOMAIN_HANDOFF.json` and `handoff_conservation_test.json` document this step.

The smaller-domain outlet probe was rejected when it lost visible liquid.
Its outputs are not included as successful video frames. The final sequence
uses the preserved prefix and the recomputed expanded-domain suffix.

## Rendering and its limits

The native deterministic direct-light Whitted preview uses four fixed AA
samples and up to twelve reflection/refraction bounces. It traces the simulated
liquid mesh and actual static scene; the look is not painted over an earlier
video. It avoids Monte Carlo sampling grain without temporal blur. It is an
approximation to indirect illumination, not converged path-traced GI.

Specular glints on small physical/reconstructed features can remain and should
not be confused with random sampling noise. The mesh can still be rough at its
resolution limit. Larger disconnected primary markers still represent finite
numerical liquid volumes: their equivalent sphere diameter is about 12.4 mm,
versus 27.9 mm previously. This is reduced sampling-scale coarseness, NOT a
prediction of real microscopic atomization. Sub-grid capillary breakup, fully
coupled air, validated foam physics and adaptive spatial refinement remain
outside this revision. The broad outward sweep is not a resolved whip tip.

## Tests

The package contains actual test sources and their run reports:

- existing solver tests for APIC transfers, projection, solids, split/merge
  bookkeeping, inlets, and compatible/incompatible checkpoint restart;
- native optics tests;
- open-face marker behavior and an old-closed-wall negative control;
- fragment hysteresis, restart idempotence, sparse-ligament preservation and an
  old neighbour-count negative control;
- original guide continuity/feedback and exactly zero guidance after 8.55 s;
- byte-exact primary-state preservation across the 9 s domain transfer.

`VERIFICATION.json` also reports sampled translational kinetic plus gravitational
potential energy after release. It is a diagnostic, not a full APIC energy
balance: affine kinetic energy and free-surface energy are not included.

## Rebuild and reproduce

Requirements: a Linux C++17 compiler with OpenMP, FFmpeg/ffprobe, Python 3.10+
with NumPy, pandas and Pillow, several GB RAM and disk for the full cache.

```sh
python -m pip install numpy pandas pillow
python reproduce.py --output /absolute/path/to/new-water-run \
    --sim-threads 3 --render-threads 4
```

The destination must be new or empty and outside this package. The script
compiles the full source, executes the tests, exports matching solid geometry,
simulates to frame 216, performs the documented domain transfer, finishes all
241 states, reconstructs/renders frames 0–239, encodes the MP4 and verifies it.
It stops rather than publishing success when a subprocess or acceptance check
fails. A changed compiler, hardware, thread count or floating-point contraction
may change the numerical trajectory/hashes; the original records are retained
for comparison, not claimed as universal bitwise results.

The archive includes representative saved states, the exact used 9 s handoff,
all per-frame render records and numerical CSVs. It deliberately does not
include the multi-gigabyte full state cache or all raw PNGs; the provided driver
recreates them. Do not label this a full physical-capability upgrade for all 17
scenes: the full-duration visual validation in this delivery is for this
waterbending shot.
