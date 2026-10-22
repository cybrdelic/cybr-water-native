# CYBR WATER — retained-volume waterbending repair

## Result and scope

This revision modifies the user's recovered native C++ APIC/MAC engine, not the
separate CUDA ocean project and not an image generator. The completed numerical
run contains 241 actual states from t=0 through t=10 at 24 Hz. The movie uses
states 0–239 once each: 240 frames, 24 fps, exactly 10 seconds.

The principal fix is velocity feedback with moving-guide feed-forward, applied
to MAC faces **before the existing incompressibility projection**. Previously,
`flow * 4.5` was continuously added as acceleration with no velocity feedback;
the open-ended guides did not return the driven liquid. The new paths are closed
and smoothly morphed through gather, split, crossing, outward sweep, ring, and
release. Target curves are force inputs only: they are never rendered as water.

## Actual numerical configuration

- Uniform 4.5 cm MAC grid; 2 primary markers per cell axis initially.
- 38,027 finite, volume-carrying primary markers, unchanged through the run.
- Reseeding disabled for this take: no splits, merges, or inflated marker count.
- No inlet, no marker teleportation, no per-frame geometry reset.
- Initial and final represented water volume: approximately 0.433151 m3.
- No recorded outflow or emitted water in this finite domain.
- Guidance identically zero at/after 8.55 s; subsequent motion follows the
  shared gravity, pressure, advection, and collision code.
- Lateral and bottom computational boundaries are closed. The top is open at
  3.3 m. During the controlled performance through 8.55 s, no markers approach
  within 1 cm of a computational face; after splashdown some reach the lateral
  boundaries. This is a contained numerical scene, not an infinite environment.
- Secondary visual whitewater is disabled. Visible detached drops are actual
  isolated liquid markers; they carry their original liquid volume.

`production_cache/retention.csv` measures retained and elevated **volume**, not
only the number of particles. `domain_audit.csv` makes computational-boundary
contacts visible. A small volume ledger by itself is not claimed as evidence
that the choreography works.

## Reconstruction and noise

Every delivered frame comes from native 3D anisotropic reconstruction of its
own solved state. The surface lattice is 1.8 cm, kernel support is 3.6 marker
spacings, with five local scalar smoothing passes followed by the existing
bounded volume-correction solve. The same real basin/rock fields are exported
for rendering and used for collision. No 2D particle circles are used as the
bulk surface, and no decorative waves or random spray are added.

The movie uses the **new deterministic direct-light/Whitted preview mode** in
`src/trace.cpp`, not the original Monte Carlo path-tracing integrator. It branches
reflection and transmission with native dielectric Fresnel/refraction/TIR and
Beer attenuation, using the original geometry, BVH, and medium handling. Four
fixed subpixel samples provide spatial antialiasing. No temporal blur, denoiser,
image generation, repeated frames, or synthetic frame interpolation is applied.

This integrator deliberately approximates diffuse environment illumination with
fixed quadrature and omits diffuse interreflection and caustics. Branches stop
at a finite bounce limit or low throughput. It is a clean engineering review,
not an unbiased converged VFX beauty render. The original Monte Carlo integrator
remains available by omitting `--deterministic-preview`.

## Important remaining limitations

The outward motion is a broad fluid sweep rather than a finely atomized whip.
The ring is a guided liquid band, not a physically derived thin soap-film shield.
There is no adaptive grid, resolved capillary breakup, or coupled air pressure.
Small-scale geometric sparkle may remain even though stochastic render grain
has been removed. Do not infer film-quality physics from the clean preview.

## Rebuild and render

On a machine with C++17/OpenMP, make, Python 3, and ffmpeg:

```sh
python3 run_all.py --threads 4
```

The script compiles and runs the shared checks, integrates 10 seconds, renders
240 states, and encodes the movie. It does not read GitHub, call an image model,
or need any prior conversation's filesystem. The source package contains a
small selection of native states for inspection; regenerate the entire cache
with this command. Native binary checkpoints contain a compiler/executable
fingerprint, so they must not be blindly resumed with a different build.

To render any already-produced range:

```sh
python3 render_sequence.py --start 0 --end 240 --threads 4
```

The optional `legacy_trick` preset preserves the previous unbounded acceleration
controller for comparisons. The original waterbending preset is also preserved.

## Verification and reproducibility

See `VERIFICATION.json`, all per-frame records in `frame_records`, the source
patch, the three numerical/optical/control test reports, and the retention CSV.
Native float positions/volumes and PNG hashes identify the inputs and rendered
outputs. The per-frame `method` record is authoritative for the integrator;
legacy Monte Carlo headers in early capture metadata are explicitly normalized
in the delivered records without changing image pixels.
