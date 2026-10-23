# Native waterbending duet / choreography V7

An opt-in choreography upgrade on the accepted V6 solver, mesher and dielectric renderer. The original V6 `waterbending_trick` remains selectable. The new scene is `waterbending_duet`.

## Performance

| Time (s) | Intended action |
|---|---|
| 0-0.90 | Draw water from the actual basin |
| 0.90-2.15 | Two separated rising cobra forms |
| 2.15-3.65 | Figure-eight braid with depth-separated crossings |
| 3.65-4.55 | Coiling anticipation |
| 4.55-5.70 | Alternating narrow whip sweeps, 0.20 s apart |
| 5.70-6.65 | Opposed tilted orbital catches |
| 6.65-7.80 | Convergence into a shield with a readable hold |
| 7.80-8.95 | Tilted spiral return with staggered release |
| 8.95-10.00 | Guidance exactly zero; native gravity/pressure/collisions |

The poses are force targets, **never rendered water geometry**. The shared MAC solver applies velocity feedback before pressure projection; particles move under that solved velocity. There are no permanent sources, teleports or phase-specific replacement meshes. Smooth periodic guides, quintic time transitions, moving-target feed-forward and bounded feedback avoid the unstable additive acceleration used by early experiments.

The controller tests verify finite values, loop closure, bounded target velocities/accelerations, transition continuity, distinct poses, exact force release and preservation of the original scene. Those mathematical checks do not on their own certify that the fluid visually follows the choreography. Native fluid renders are the visual evidence.

## Build, test and render

After checkout, run `python3 scripts/materialize_native.py` once from repository root if the readable sources are not yet present. Then:

```sh
cd choreography-v7
python3 -m pip install -r requirements-render.txt
python3 reproduce.py --tests-only
python3 reproduce.py --out runs/duet --width 960 --height 540
```

The default simulation remains the accepted 3 cm pressure grid, 1 cm marker spacing, APIC, surface tension and one-way flow-driven secondary particles. The default native output is 960 x 540. A faster review uses `--width 640 --height 360`; it is not upscaled or represented as the higher-resolution result.

`reproduce.py` recreates static geometry from source, simulates 241 states from t=0 to t=10, reconstructs 240 native frames, renders through the V6 optics, and invokes `verify_export.py`. The verifier rejects missing/repeated frames, mixed source revisions, changed frame hashes, nonfinite paths, bad surface-volume checks and wrong presentation timestamps. It only publishes the completed 10 s / 24 fps MP4 after full decoding and uniqueness checks. It does not manufacture missing frames.

Use a clean output directory for a new simulation. `--skip-simulation` only skips an already completed state cache. The renderer can resume verified completed frames. Native simulation and rendering may be expensive; there is no real-time performance claim.

## Source identity and scope

`engine/src/cinematic_choreography.hpp` contains the new controller. `gallery_scenes.hpp` registers the opt-in scene and `gallery_simulate.cpp` adds correct per-scene guide diagnostics. `fluid.hpp`, `surface.cpp` and `trace.cpp` remain byte-for-byte identical to accepted V6. The new header participates in Makefile dependencies for every affected target.

The first coarse pilot merged the whip streams; the candidate separates height and depth, uses an actual figure-eight projection, and slows the final return. Pilot frames are not passed off as the finished take. Local run evidence is distinct from the CI tests, which compile and check native operators but do not run a full expensive movie.

This remains externally guided fantasy fluid, with uniform 3 cm resolution, one-way whitewater and approximate preview indirect lighting. It does not add coupled air dynamics, resolve microscopic breakup, or guarantee thin-sheet physical accuracy. V6 has not been overwritten. No new license grant is introduced.
