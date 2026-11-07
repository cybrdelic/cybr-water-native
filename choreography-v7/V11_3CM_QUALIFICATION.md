# CYBR WATER Native: bounded V11 visual qualification

**Result: narrower flow, but visual acceptance remains withheld.** Genuine APIC/MAC states and native optical renders establish a limited improvement. The corrected flow still forms broad arching curtains; it does not yet read as convincing thin martial whips or production-quality water.

Baseline source: `32c97c3ecd5f2a73116a9783efa99b313b35bd39` (draft PR #2). Local implementation: `1a508f9d91e402e8756f84a1731439a9b0a8edf7`. The accompanying source branch and draft PR carry the final code; the render receipts retain exact source and binary hashes.

## What actually ran

- Original and corrected V11: 3 cm pressure grid, 1 cm marker spacing, APIC, 27 markers per cell, no reseeding, surface tension 0.072 N/m, seed 37, native moving open domain, no inlet.
- **85 saved states each, t=0 through 3.5 s, were audited** for finite primary/secondary data, unique and unchanged primary IDs, and unchanged per-marker volumes. Extra unreviewed suffix states produced during stopping are excluded from qualification.
- Both retain **439,624 markers and 0.439623998890 m³**. The maximum change in saved reference volume is zero. This measures marker ownership, not local surface thickness or a complete physical convergence study.
- A separate original-controller 12 cm reproduction used the same bounded sequence. It has 6,473 markers and 0.414271998954 m³. Its initial sampling volume is about 5.8% lower, so the comparison is not an equal-mass convergence experiment.
- The motion comparison contains 17 unique decoded pairs from actual states 0,3,…,48. Simulation is 24 Hz; the review movie is **8 fps, 640×360 per view**, showing source times 0–2 s. Container duration is 2.125 s because the last sample occupies one normal frame interval. No frame interpolation, repeated padding, generated imagery, or guide meshes.
- Native stills are **960×540** per view. Selected path references use **32 samples/pixel, 64 bounces**, and the existing Monte Carlo transport. The preview uses deterministic Whitted transport, 4-to-16 adaptive samples and 20 bounces. These are distinct integrators and are labelled separately.
- The machine exposes eight CPU cores with an 8 GiB cgroup memory limit and no detected GPU. Later preview tracing reached 119.5 s for one 960×540 frame. No controlled throughput benchmark is claimed.

## Focused correction

The original 3 cm lift still entrained broad columns. Source inspection showed a 26 cm scoop capture radius and an 18.5 cm whip radius. The candidate changes **force support**, not rendered geometry: scoop core/capture 6/26 cm → 3/14 cm; whip core/capture 5.5/18.5 cm → 2.5/10 cm. The curve AABB pruning follows the new support radius. No particles are moved by the controller, and the solver, mesher, optics and earlier named presets remain unchanged.

At 1 s, using **six solver threads for both original and confirmation run**, primary volume above 80 cm changes from **87.656 L to 18.393 L**, a **79.02% reduction**. The total primary volume remains 439.624 L. This is less over-entrainment; it is not proof of correct thin-sheet physics.

The main candidate exploration used four solver threads versus six for the original. A separate one-second six-thread candidate confirmation differs from the four-thread candidate by one marker in the above-80-cm count. Its maximum position difference is 1.54 mm, RMS coordinate difference 8.04 µm. This check does not establish thread equivalence for the whole sequence or justify performance comparisons.

## Acceptance assessment

| Criterion | Actual evidence | Judgment |
|---|---|---|
| Whip/sheet thickness | Same-camera original/candidate native frames at 1, 1.5 and 2 s; marker cross-section proxies | Smaller bulk, but broad curtains remain. At 1 s the 80 cm-height equivalent circular diameters fall from roughly 37–38 cm to about 20.5 cm. These area-derived proxies are **not measured sheet thickness**. Thin-sheet acceptance fails. |
| Continuity | Saved states at 24 Hz and 8 fps early native motion | Gross lift-to-whip motion can be inspected; selected reconstruction resets fragment history. Full 24 fps meshing/hysteresis continuity is unqualified. |
| Breakup | Native primary droplet classification and secondary parcels in receipts | Visible perforation and fragments are present, but no physical droplet-size spectrum or grid convergence is established. |
| Detached droplets | Volume-preserving isolated primary spheres, reported separately from massless secondary parcels | Representation is present; realistic atomization remains unqualified. |
| Mass conservation | Every audited primary ID and reference volume unchanged through 3.5 s | Saved primary ownership passes. This does not prove local incompressibility, geometric volume distribution or coupled-air behavior. |
| Reconstruction | Anisotropic native surface, bounded global volume correction, per-frame mesh reports | Selected renders meet the existing dense-volume tolerance. Global volume matching can hide local dilation; original 2 s uncorrected mesh volume was about 0.210 m³ before correction to about 0.439 m³. Local thickness is not thereby validated. |
| Optical response | Native dielectric reflections/refractions and two 32-spp path comparisons | No nonfinite paths in inspected references; visible Monte Carlo noise and rare medium errors remain. No sample/depth convergence or caustic validation. |
| Attack timing | Scoop, whip and redirect portion only, through 3.5 s | Less elevated material reaches the hand field; the narrower capture changes amplitude. Stronger martial readability has not been accepted. |
| Slice, wave, vortex, counterrotation | No qualified corrected high-resolution states/renders in their later time intervals | Pending. Earlier coarse footage is not substituted. |
| Final release/splash | Existing exact-release controller tests still pass at 8.90 s; no corrected 3 cm splash sequence reviewed | **Physically and visually unqualified.** Controller release is not a rendered splash result. |

## Confirmed redirect failure

At **3.5 s, both versions have zero primary volume above 80 cm**. Maximum marker height is 0.658 m before and 0.588 m after; the native path references show a low basin splash instead of a sustained redirect/slice. The candidate therefore fails to improve the complete choreography and must remain experimental.

Source inspection suggests a handoff mismatch: redirect radial support is centered at radius 0.46 m and ends at radius 0.74 m, while the whip hands reach roughly 1.2–1.3 m laterally. At 2.5 s, only 0.025312 m³ of the original's 0.067794 m³ elevated water lies within that radial support; the candidate has 0.004302 m³ of 0.012483 m³. This is an overlap diagnostic, not a causal proof or exact post-projection force audit. A moving handoff/catch field should be tested as the next isolated experiment; no unvalidated late-force correction is claimed here.

## Numerical coverage and limitations

The baseline's currently retained substep CSV covers only t=0–2.337962963 s even though later saved states and frame-end native logs exist. That is an evidence gap, and the audit explicitly marks `ledger_coverage_complete: false`; it does not certify baseline substep pressure through 3.5 s. The corrected CSV covers the entire reviewed interval. Pressure maxima from the two CSVs therefore have different time coverage and must not be treated as a full matched comparison. Per-frame endpoint pressures remain in the untouched baseline simulation logs.

For the corrected 0–3.5 s interval, maximum recorded relative pressure residual is 2.07808e-05, maximum post-projection L2 divergence is 0.000180717 s⁻¹, and the volume-ledger error is zero. Exact saved-state hashes and coverage are in the audit JSONs. The original 12 cm review repository does not supply its original solver cache; the included coarse cache was freshly reproduced, not assumed identical to the published movie.

## Changes and validation

- V11 capture support narrowed; an added regression retains centerline transport while rejecting entrainment 15 cm off the whip. That new assertion fails against the untouched controller and passes against the candidate.
- Every target that transitively includes the V11 scene now depends on its header, preventing stale gallery/test binaries.
- `reproduce.py --stop-after-frame` supports explicitly bounded simulation-only prefixes; it refuses to certify them as full movies.
- `inspect_native_states.py` audits arbitrary prefixes and labels incomplete substep ledgers explicitly. Default behavior rejects a truncated ledger.
- `render_take.py` exposes native preview/path integrators, bounce depth, branch cutoff and environment, and attaches simulation identity separately from render source identity. Defaults retain the existing preview settings.
- `checks`, `finish-checks`, `continuation-checks`, and `choreography-checks` pass. **28 Python tests pass.** A real 3 cm one-frame bounded reproduction completed.
- Additional `combat-checks` fails on the untouched source: `combat guide is not closed`. It remains a known baseline failure, not a new passing claim.
- `native-v6`, `fluid.hpp`, `surface.cpp`, `trace.cpp`, and the earlier scene registration source are byte-for-byte unchanged from the supplied revision.

## Reproduction

Start from the repository and fetch the accompanying qualification branch. Build the original in a separate worktree so its executable is preserved:

```sh
git worktree add ../water-v11-original 32c97c3ecd5f2a73116a9783efa99b313b35bd39
make -C ../water-v11-original/choreography-v7/engine -j2 all build/trace_native
```

Run the original native solver from that worktree, with a fresh output directory:

```sh
choreography-v7/engine/build/gallery_sim --scene waterbending_martial_v11 \
  --dx .03 --duration 10 --fps 24 --threads 6 --out runs/original/cache \
  --apic --particles-per-axis 3 --reseed-interval 0 \
  --whitewater-rate 24000 --max-whitewater 180000 --surface-tension .072 \
  --auto-domain --domain-padding .48 --domain-lower -1.98,-.12,-1.50 \
  --domain-upper 1.98,2.82,1.50 --stop-after-frame 84
```

Use `.12` in a separate fresh output directory for a coarse comparison. On the corrected branch, the bounded runner supplies the same physical options:

```sh
cd choreography-v7
OMP_WAIT_POLICY=PASSIVE python3 reproduce.py --scene waterbending_martial_v11 \
  --out runs/v11-qualified-prefix --dx .03 --threads 6 \
  --simulation-only --stop-after-frame 84
python3 inspect_native_states.py --take runs/v11-qualified-prefix --dx .03 \
  --end-frame 84 --out runs/v11-qualified-prefix/STATE_AUDIT.json
python3 render_take.py --take runs/v11-qualified-prefix \
  --scene-assets runs/v11-qualified-prefix/scene --select 24,36,48,84 \
  --width 960 --height 540 --workers 1 --surface-threads 2 --trace-threads 2 \
  --integrator path --samples 32 --adaptive-aa 0 --bounces 64
```

Use a fresh take directory when changing integrator/settings. Render contracts deliberately reject mixed settings. For early preview motion use `--select 0,3,6,9,12,15,18,21,24,27,30,33,36,39,42,45,48`, `--width 640 --height 360`, preview integrator, 4 samples and adaptive AA 16; encode the 17 **actual** chronological frames at 8 fps. The delivered movie composes two such views with labels only. Receipts contain exact executed commands, source hashes, binary hashes, state hashes and reconstruction results.

The bundle contains selected native states at 1, 1.5, 2 and 3.5 s, scene metadata, audits and render receipts. It does not contain the full simulation cache, reconstruction phase chain or a resumable high-resolution ten-second take.

## Remaining work

1. Fix and visually test the failed whip-to-redirect handoff, then review the complete corrected 3 cm choreography through release and splash, with full retained substep diagnostics and 24 fps surface history.
2. Replace broad mass entrainment with a better measured flow objective; validate actual local sheet/filament thickness against the grid before further narrowing force radii.
3. Compare at least one finer grid and multiple meshing scales, preserving initial physical mass, to distinguish solver breakup from reconstruction dilation.
4. Localize rare medium-tracking errors; perform sample/depth convergence and inspect temporal shimmer, foam binding and caustics.
5. Only then decide whether the experimental V11 changes should be accepted. Neither controller tests nor this bounded review establish finished optical or production-film quality.
