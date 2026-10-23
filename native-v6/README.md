# Accepted V6 native baseline

This is the source revision that produced the accepted `CYBR_WATERBENDING_V6_COMPLETE_10S.mp4`: 960 x 540, 240 distinct native frames, 24 fps, ten seconds. `evidence/ACCEPTED_V6_VERIFICATION.json` is the saved verification from that delivery, not evidence of a new run performed by repository publication.

The full readable engine is materialized and committed by the native-source workflow. On a bootstrap checkout, run `python3 scripts/materialize_native.py` from the repository root. The migration checks both the pre-existing open-splash files and every restored file by SHA-256; it never downloads source or silently overwrites edits. All previous repository revisions stay unchanged.

## Reproduce

Linux, C++17/OpenMP, GNU Make, Python 3.10+, NumPy, Pillow, FFmpeg/ffprobe. From this directory:

```sh
python3 -m pip install -r requirements-render.txt
python3 reproduce_continuation.py --tests-only
python3 reproduce_continuation.py --native
```

A complete run is CPU-intensive and writes multi-gigabyte fluid/geometry caches. Use a clean working directory. `--skip-simulation` requires a complete matching cache. The scene exporter recreates the basin, rocks and collision field from code. No downloaded textures, generated images, external models, frame interpolation or learned denoisers are used.

The native method is quadratic APIC/MAC, 3 cm grid, 1 cm marker spacing, coherent surface reconstruction, and deterministic dielectric/Whitted preview with four to sixteen adaptive camera samples. The original `waterbending_trick` preset remains unchanged in the new duet engine.

## Bounds of the result

Uniform resolution, one-way secondary whitewater, unresolved microscopic atomization and approximate indirect light remain. It is not a coupled water/air solver or converged full global illumination. Hash equality establishes preservation of source and recorded output identities, not physics convergence or cross-platform bitwise reproducibility.

The large accepted MP4, full simulation states and geometry caches are not placed in Git. Their identities are recorded in the saved verification; the exact native source and reproduction workflow are included here. No additional license grant is introduced.
