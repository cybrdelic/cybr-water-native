# CYBR WATER Native

A native C++ APIC/MAC liquid solver, anisotropic surface reconstruction, and deterministic dielectric / path-traced rendering. The visible water comes from the solved liquid state. Choreography guides apply forces before pressure projection; they are never rendered as water geometry.

## Current source

**[Accepted V6 baseline](native-v6/README.md)** preserves the exact source behind the accepted ten-second, 960 x 540 native take. Its solver, reconstruction, coherent optical normals, oriented foam, checkpoint recovery and twelve native test groups are retained.

**[V7 waterbending duet](choreography-v7/README.md)** adds a separately selectable performance: twin rising forms, a figure-eight braid, coiling anticipation, staggered outward strikes, opposed orbital catches, a shield hold, and a spiral return. The original `waterbending_trick` preset remains unchanged; select `waterbending_duet` for the new sequence. The V6 fluid solver, mesher and optical tracer are byte-identical between the two source trees.

The controller uses closed periodic curves, smooth time transitions, moving-target velocity/acceleration compensation and bounded pre-projection velocity feedback. It does not teleport particles, manufacture replacement water meshes or add a permanent inlet.

## Build, test and reproduce

Linux, a C++17/OpenMP compiler, GNU Make, Python 3.10+, NumPy, Pillow and FFmpeg/ffprobe are required. The complete readable sources are checked in. No external model, texture download, generated image or pretrained denoiser is required.

```sh
cd choreography-v7
python3 -m pip install -r requirements-render.txt
python3 reproduce.py --tests-only
python3 reproduce.py --out runs/duet --width 960 --height 540
```

A review at `--width 640 --height 360` changes only the native image resolution, not the fluid simulation. It is not upscaled or represented as 960 x 540. Full runs are CPU-intensive and write several gigabytes of cache; there is no real-time performance claim.

The reproduction workflow exports static collision/render geometry, evolves 241 states at 24 Hz over ten seconds, reconstructs and renders 240 consecutive native frames, and rejects missing/repeated frames, mismatched source identities and incorrect timestamps before publishing the MP4. There is no frame interpolation or video denoising. `audit_take.py --take runs/duet` additionally checks every saved particle state, identity, volume ledger and lateral clearance.

For the original accepted choreography, use [V6's reproduction instructions](native-v6/README.md). Do not mix binaries, caches or scripts across revisions.

## Validation and scope

The native-source migration was checked by input/output SHA-256 and then compiled and tested in a clean GitHub Actions runner. The regular native-regression workflow checks the readable source trees on subsequent changes. Local historical results and CI results are kept distinct. Controller tests establish continuity, force bounds and exact release; they are not a substitute for reviewing the actual fluid motion.

The physical model still uses a uniform 3 cm fluid grid, 1 cm primary marker spacing, and one-way secondary whitewater. Microscopic breakup, coupled-air dynamics and physical convergence are not established. The deterministic preview approximates indirect illumination; it is not converged full global illumination. Wide liquid sweeps should not be confused with a fully resolved filamentary whip.

Large MP4s and simulation caches are not placed in Git. Their source identities and verification records accompany the delivered native exports; the repository contains the implementation and complete reproduction tools.

## Preserved history

[The previous repository documentation](README_HISTORY.md) retains the earlier baseline, open-splash and retained-trick source/media links and their original limitations. These directories have not been overwritten. The exact V6 migration and initial V7 transport bundles remain under their `migration/` directories for provenance; normal development uses the readable source files, not those bundles.

No license grant was supplied with the source deliveries, and this publication adds no license grant.
