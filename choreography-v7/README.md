# Native waterbending duet / choreography V7

## V11 martial choreography (new; opt-in)

The newest controller is `waterbending_martial_v11` (alias `waterbending_martial`).
**The older V10, V8 combat, V7 duet and accepted native V6 are still selectable.**

This revision targets defects observed in the controller source, not an already
approved V11 beauty render:

- **No hand-trajectory snaps:** nonuniform cubic-Hermite hand tracks with shared
  knot tangents replace disjoint per-beat hand-position formulas. Continuous
  position and velocity are tested at all interior knots.
- **No abrupt force-regime cuts:** eight overlapping C2 quintic attack envelopes
  blend scoop, whip, redirect, slice, wave, vortex, counterrotation and slam.
  Force magnitude is weighted and normalized before the MAC pressure projection.
- **Actual whip transport:** two independently moving curved velocity-guide tubes
  with tapering tip influence and tangential transport; guide curves are never
  rendered meshes and primary liquid cannot be teleported.
- **Traveling wave, controlled spiral and thin slice:** motion-dependent fields
  act upon the shared pressure-projected fluid. These are force *requests*,
  not guarantees that the liquid reaches a prescribed shape.
- **No pre-suspended water:** V11 starts with the quiescent basin from the V6
  preset, whereas the experimental V10 scene intentionally pre-seeds an
  airborne crescent. Neither V11 nor V10 uses an ongoing liquid inlet.
- **Real release:** feedback and gravity support fade to zero by 8.90 s.
  The last 1.10 seconds are governed by the native fluid solver.

Regressions in `engine/tests/martial_v11_checks.cpp` check knot continuity,
force bounds, transient smoothness, exact release, initial-state absence of
pre-suspended material, absence of synthetic inlets, open lateral boundaries,
and preservation of the earlier named presets. Numerical unit-test success is
**not** evidence of full visual acceptance.

Run the **native 3 cm take** (960 x 540, 24 fps, 10 seconds; very expensive):

```sh
cd choreography-v7
python3 -m pip install -r requirements-render.txt
python3 reproduce.py --scene waterbending_martial_v11 --out runs/martial-v11 --dx .03 --width 960 --height 540
```

For a **coarse choreography diagnostic only** (not a beauty render), use
`--dx .12 --out runs/martial-v11-coarse` in a new output directory.
The exporter now records the actual fluid grid and scene identity rather than
mislabeling coarse reviews as V7 duet / production water. Do not resume or
replay a state cache under a different scene or grid spacing.

**Unresolved production issues:** no 3 cm V11 film take has been visually
approved; a 3 cm pressure grid cannot directly resolve centimeter-thin
sheets or realistic microscopic atomization; body/hand-driven fantasy forces
are not measured real-world biomechanics; air entrainment remains one-way;
caustic transport and true multiple-scattering water optics are approximations.
A stronger native render and comparative image review are still required before
calling the result photorealistic.

---

An opt-in choreography upgrade on the accepted V6 solver, mesher and dielectric renderer. The original V6 `waterbending_trick` remains selectable. The new scene is `waterbending_duet`.

## Performance

| Time (s) | Intended action |
|---|---|
| 0-0.90 | Draw water from the actual basin |
| 0.90-2.15 | Two separated rising cobra forms |
| 2.15-3.65 | Figure-eight braid with depth-separated crossings |
| 3.65-4.55 | Coiling anticipation |
| 4.55-5.70 | Broad staggered sweeping bands, 0.20 s apart |
| 5.70-6.65 | Opposed tilted orbital catches |
| 6.65-7.80 | Convergence into an upright open ring with a readable hold |
| 7.80-8.95 | Tilted return with staggered release |
| 8.95-10.00 | Guidance exactly zero; native gravity/pressure/collisions |

The poses are force targets, **never rendered water geometry**. The shared MAC solver applies velocity feedback before pressure projection; particles move under that solved velocity. There are no permanent sources, teleports or phase-specific replacement meshes. Smooth periodic guides, quintic time transitions, moving-target feed-forward and bounded feedback avoid the unstable additive acceleration used by early experiments.

The controller tests verify finite values, loop closure, bounded target velocities/accelerations, transition continuity, distinct poses, exact force release and preservation of the original scene. Those mathematical checks do not on their own certify that the fluid visually follows the choreography. Native fluid renders are the visual evidence.

## Verified completed take

The completed take is **960 x 540, 24 fps and 10 seconds**, with **240 unique native rendered frames and 240 unique decoded movie frames**. Full movie decoding, presentation timestamps, native state hashes and the reconstruction phase chain passed verification. All **241 native states (frames 0-240)** retain **439,624 unique primary IDs** and **0.43962399889 m3** of primary liquid. The audit records zero primary-volume change, zero added/outflow volume and zero volume-ledger error.

The reviewed native stills and contact sheet show broad staggered sweeps, an upright open ring, a tilted return and an active basin splash with small late foreground runoff. The camera audit counts 6,139 primary centers (1.3964%) outside the last movie frame; this projection count is separate from the zero domain outflow recorded by the conservation audit. Visual inspection covered the recorded stills and contact sheet; automated decoding covered the entire movie.

| Verified artifact | SHA-256 |
|---|---|
| `CYBR_WATERBENDING_DUET_V7_10S.mp4` | `5e319a8cd6231be032de8e7008a8bb1784bdc0c9389e7c8d0cd8ea8a2d36fcc5` |
| `SIMULATION_AUDIT.json` | `abb8bf212aff2b7676a7b3625ababf8116943777fc24a7e0207b5f106fa082d6` |

[Final take verification](evidence/FINAL_TAKE_VERIFICATION.json) records the compact export, conservation, framing and source-identity evidence, including the corrected contact-sheet hash. The rendering and physical-model limits below still apply.

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

### Resume an interrupted take

```sh
python3 reproduce.py --out runs/duet --resume --width 960 --height 540
```

Keep the original `gallery_sim` executable, recorded simulation command, and cache together. Native checkpoints include an executable/configuration fingerprint; rebuilding or changing solver options can make them incompatible. The runner preserves full frame-boundary checkpoints separately from transient domain-growth checkpoints. Recovery restores the checkpoint's exact grown grid bounds and original simulation options, keeps the completed frame prefix, archives the uncommitted suffix, trims ledger rows to the restart time, and preserves domain-growth history. It never reconstructs a restart from display particles. The native regression compares an uninterrupted run with this recovered run byte for byte, including primary states, secondary states, IDs and volumes.

Rendering resumes only with matching source, native binaries, scene geometry, optical settings and reconstruction-history hashes. PNG publication writes and synchronizes a temporary file before atomically replacing the final path, so an interrupted encoder cannot overwrite a completed image with partial bytes. Final export runs the complete raw-state audit once, reuses the hashes calculated during that read to check every render receipt, verifies the phase-history chain and surface-volume bounds, then fully decodes the 240-frame movie and checks its unique frames and presentation times. These checks establish provenance and continuity; visual acceptance still requires reviewing the actual result.

`render_take.py` exposes three scheduling controls: `--workers` sets concurrent frames, `--surface-threads` sets reconstruction threads, and `--trace-threads` sets native tile workers within each frame (default `1`, must be positive). On a shared CPU, `--workers 3 --surface-threads 2 --trace-threads 2` is a measured starting point. Reconstruction remains sequential to preserve its phase history. Trace threads share each frame's geometry and caches, and leave resolution, sampling, optics and the frozen native source/binary contract unchanged. The [frame-42 trace check](evidence/TRACE_THREAD_PARITY_1_2_4.json) matched decoded RGB and recorded transport statistics at one, two and four threads; the retained linear PFM payloads also matched byte for byte at two and four threads. The [frame-159 surface check](evidence/SURFACE_THREAD_PARITY.json) matched mesh, sphere CSV and phase bytes at two and four reconstruction threads. These are checks of those recorded inputs, not controlled speedup benchmarks. Timings depend on competing work; drain the existing runner before resuming with different scheduling.

## Source identity and scope

`engine/src/cinematic_choreography.hpp` contains the new controller. `gallery_scenes.hpp` registers the opt-in scene and `gallery_simulate.cpp` adds correct per-scene guide diagnostics. `fluid.hpp`, `surface.cpp` and `trace.cpp` remain byte-for-byte identical to accepted V6. The new header participates in Makefile dependencies for every affected target.

The first coarse pilot merged the whip streams; the V7 guide separates height and depth, uses an actual figure-eight projection, and slows the final return. Pilot frames are not passed off as the finished take. Local run evidence is distinct from the CI tests, which compile and check native operators but do not run a full expensive movie.

This remains externally guided fantasy fluid, with uniform 3 cm resolution, one-way whitewater and approximate preview indirect lighting. It does not add coupled air dynamics, resolve microscopic breakup, or guarantee thin-sheet physical accuracy. V6 has not been overwritten. No new license grant is introduced.
