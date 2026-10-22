# CYBR WATER Native

Native C++ waterbending studies: an APIC/MAC liquid solver, anisotropic surface reconstruction, and native CPU path-traced / deterministic-preview rendering. The choreography is driven by body acceleration fields, not animated water meshes or direct particle placement.

This repository preserves the **open-boundary splash repair** alongside the earlier retained-trick take, a coherent coarse baseline, and failed intermediate experiments. The latest delivery repairs the earlier invisible lateral walls and reduces coarse-marker bead scale, while explicitly documenting the remaining numerical and rendering limits.

## Latest: open-boundary splash repair

![Latest open-boundary native preview](media/open-splash.gif)

The [original ten-second movie](open-splash/delivery/CYBR_WATERBENDING_OPEN_SPLASH_10S.mp4) is 960×540, 240 distinct decoded frames at 24 fps. It is a native deterministic direct-light/Whitted preview, **not converged global illumination**, image generation, or a particle diagnostic.

![Repaired native sequence](open-splash/delivery/SEQUENCE.jpg)

The source uses a uniform 3 cm grid, 1 cm primary markers, open lateral boundaries, coherent fragment classification, and flow-derived secondary visual parcels. The supplied 241-state records retain 439,737 primary markers and 0.43973699889 m³, with zero recorded primary outflow and zero markers within two grid cells of a lateral exit. Guidance ends at 8.55 s. Finer equal-volume primary spheres still represent about 12.4 mm numerical droplets, not physically validated atomization.

**At 9 s the domain is explicitly enlarged.** The packaged snapshots preserve primary positions, velocities, volumes and IDs exactly, but reconstruct APIC affine gradients and reset pressure warm start. This is not a bitwise continuation or a full energy-conservation claim.

![Earlier closed-wall result and repaired open-boundary result](open-splash/delivery/SPLASH_BEFORE_AFTER.jpg)

See [complete source, dependencies, reproduction commands, sample restoration and limitations](open-splash/README.md). The complete latest engine and test set is isolated in `open-splash/`; earlier revisions are not silently overwritten. Its Python reproduction driver creates a new output tree, compiles the supplied C++17/OpenMP source, simulates the two stages, reconstructs 240 frames and verifies the encoded movie. Several GB of RAM and disk are needed. The original publication review did not execute project code. The later portability work is described below and does not claim a new full-film run.

## Earlier retained-trick take: historical wall audit

![Earlier retained-trick preview](media/retained-trick.gif)

The [earlier full movie](media/retained-trick-10s.mp4) is also 960×540, ten seconds at 24 fps. It restored the gather/split/cross/sweep/ring sequence after the failed transport experiment, but used a 4.5 cm grid and 38,027 coarse markers. Its [splash audit](retained-trick/SPLASH_AUDIT.md) identified invisible closed side walls and repeated approximately 2.79 cm primary spheres. The original simulation behavior and audit are preserved as historical evidence; later portability edits are limited to builds, standard-library includes and renderer numerical predicates. Their defects are not attributed to the newer open-boundary take, and the newer take is not claimed to solve all splash physics.

See [earlier source and caveats](retained-trick/README.md).

## Baseline preview

![Prior 32-spp baseline waterbending preview](media/baseline-32spp.gif)

Ten-second preview from the prior saved 32-spp native export, reduced to 12 fps for the GIF. It is not the latest transport result. The full [32-spp movie](media/baseline-32spp.mp4) is 320×180, 24 fps, 240 frames. The sample count is the saved export's label, not something inferred from the MP4.

![Prior 32-spp baseline contact sheet](media/baseline-32spp-contact.png)

The source delivery itself includes the noisier [4-spp movie](media/baseline-4spp.mp4) and [4-spp contact sheet](media/baseline-4spp-contact.png). That delivery records 241 solver states, including t=0, and 240 rendered frames. The rendering helper retains its original 4-spp defaults. These renders show the current limitations rather than a finished VFX result.

## What's here

- [open-splash](open-splash): latest complete source/test set, reproduction driver, losslessly packaged sample states, handoff evidence, frame records and native media.
- [retained-trick](retained-trick): earlier complete source/test set, reproduction scripts, seven sample states, frame records, and historical splash audit.
- [engine/src](engine/src): baseline solver, procedural scene definitions, native mesher and tracer, and static-scene exporter.
- [scripts/render_batch.py](scripts/render_batch.py): baseline frame-to-video helper, with its machine-specific path made repository-relative.
- [experiments/geometry](experiments/geometry): preceding piecewise-guide experiment, three native diagnostic frames, and failure record.
- [experiments/transport](experiments/transport): earlier failed controller patch, particle-state diagnostic renderer, and failed native-surface status.
- [experiments/reconstruction](experiments/reconstruction): earlier thin-sheet/reference meshing-profile checkpoint, preserved as an optional source variant rather than silently merged into the baseline.
- [evidence](evidence): saved simulation metadata and logs. These are historical run evidence, not test results from this repository's publication review.
- [media](media): real saved renders and reduced-frame-rate GIFs derived from them. No generated illustration stands in for a simulation result.

No previous repository history, compiled executables, or full binary simulation caches are included. The retained-trick revision contains seven sample states and static review inputs. The open-splash revision adds five representative states and the exact pre-transfer state; six large snapshots use lossless parts. Regenerate the full take to rerender every frame.

## Portable native setup

The current build supports C++17 toolchains on Windows, Linux and macOS through CMake 3.30+, with Python 3.10+ as the standard-library-only build/run frontend. No GPU, model download, external C++ library or texture pack is needed. Use a separate build directory for each preserved revision.

| Platform | Toolchain |
| --- | --- |
| Windows | Visual Studio 2022 Build Tools with Desktop development with C++, or a compatible CMake generator/compiler. MSVC parallel builds require the installed LLVM OpenMP runtime for `/openmp:llvm`. |
| Linux | GCC or Clang with its matching OpenMP runtime; GCC distributions commonly supply `libgomp` with the compiler. |
| macOS | Apple Clang plus optional Homebrew `libomp`. Set `OpenMP_ROOT` to the installed libomp prefix when requiring parallel execution. |

Install CMake in your own Python environment if it is not on PATH: `python -m pip install "cmake>=3.30,<5"`. On Windows, `py -3` may be used instead of `python`; on Linux/macOS, `python3` may be the interpreter name. No global environment or system configuration is changed by the project helpers.

Run these shell-neutral, one-line commands from the repository root:

```text
python tools/native.py build --revision open-splash --build-dir .local/open-splash --openmp AUTO --jobs 2
python tools/native.py test --build-dir .local/open-splash
python tools/native.py smoke --build-dir .local/open-splash --output .local/smoke-open-splash
python -m unittest discover -s tests -v
```

The smoke command refuses an existing output directory. It advances the real native drop solver, checks finite distinct state snapshots, reconstructs a surface, renders a 64x36 one-sample frame, and records hashes in `smoke.json`. This checks execution across the pipeline; it is not a full film, image-quality benchmark or fluid-physics qualification.

`AUTO` enables OpenMP only if the compiler and required runtime are found; it prints and records whether the build is parallel or serial. Use `--openmp ON` to require it, or `--openmp OFF` for the explicit serial path. On Windows CMake locates the selected Visual Studio installation's existing LLVM OpenMP DLL and the Python runner adds its directory only to child-process PATH. It never copies or redistributes Microsoft DLLs. A custom existing DLL can be selected with `--cmake-arg=-DWATER_OPENMP_RUNTIME_DLL=C:/path/to/libomp140.x86_64.dll`. On macOS, use `--cmake-arg=-DOpenMP_ROOT=/path/from/brew-prefix-libomp` if discovery needs help.

See [native CI](.github/workflows/native.yml): each of the three preserved revisions is built and actually checked/rendered on Windows, Linux and macOS; an additional Linux job exercises serial execution. Workflow definitions are coverage intentions until a run for the relevant commit has completed. Native outputs can differ between compilers and thread schedules. The renderer keeps the original extended-precision triangle-plane path where `long double` is wider than `double`; other platforms use a compensated double-double fallback tested against 100-digit Decimal reference calculations without weakening the existing optics assertions.

For the other revisions, substitute `retained-trick` or `baseline` and use a matching build directory. Baseline contains four native programs and optics/precision checks; the later revisions also contain their solver test sets. Do not mix revision binaries. The original GNU Makefiles remain historical alternatives.

Local verification on 22 October 2026 used MSVC 19.44 on Windows: open-splash passed 6 native checks with OpenMP, retained-trick passed 4 with OpenMP, and baseline passed 2 in serial mode. All three advanced 2,152 markers through three native substeps, reconstructed the solved surface and wrote a 64x36 one-sample image. The 58-case compensated-plane oracle matched its Decimal100 reference to the reported Float64 values. Seven restoration regressions passed, and all six original sample snapshots restored with matching SHA-256 hashes. Both revision runners passed their bounded `--checks-only` paths. Those local results alone do not establish full-film reproduction, Linux/macOS execution or physical accuracy.

[Hosted run 39865946874](https://github.com/cybrdelic/cybr-water-native/actions/runs/39865946874) subsequently passed all ten jobs at source `0524f4f1ba0d61c5b2c3b662b5d0cf758768a0e9`: all three preserved engines on Windows, Linux and macOS with OpenMP, plus the latest preserved engine in Linux serial mode. Each job compiled, ran its native checks, advanced actual particle state, reconstructed a surface and rendered a bounded 64x36 image; restoration regressions passed. The earlier hosted fixture failure was corrected by resolving temporary roots to the same canonical form as production, without changing containment rules or solver equations. The [macOS open-splash artifact](https://github.com/cybrdelic/cybr-water-native/actions/runs/39865946874/artifacts/12463788179) records Apple Clang 21, three native substeps, 2,152 markers and changed particle payloads. Its coarse noisy image was inspected. These bounded results do not qualify newer WATER V6 revisions, production films, convergence or physical accuracy.

Full-film helpers additionally need FFmpeg/ffprobe and their documented Python packages. [requirements-render.txt](requirements-render.txt) lists baseline video imports; [open-splash/requirements.txt](open-splash/requirements.txt) lists the latest full-run dependencies. The native bounded smoke does not need these optional packages.

## Build and simulate the baseline

Run from the repository root:

```sh
python tools/native.py build --revision baseline --build-dir .local/baseline
python tools/native.py run --build-dir .local/baseline -- gallery_sim --scene waterbending_trick --dx .14 --particles-per-axis 2 --apic --duration 10 --fps 24 --threads 4 --pressure-iterations 65 --no-whitewater --out cache
python tools/native.py run --build-dir .local/baseline -- gallery_export --scene waterbending_trick --out solids.mesh
```

The first command builds `gallery_sim`, `surface`, `trace`, and `gallery_export`. `cache` is regenerated locally, including states and a compatible checkpoint. The archive contained only cache metadata, not the frame binaries needed for rendering. The simulator writes 241 states for t=0 through t=10 at 24 Hz; the movie uses frames 0 through 239.

The upstream subset's Makefile advertised unrelated targets whose sources were absent. The earlier `engine/Makefile` intentionally includes only those four complete targets and does not offer its missing solver test suite. The separately packaged `retained-trick/CYBR_WATER_VFX/` and latest `open-splash/engine/` contain complete source/test sets; use their own documented runners rather than mixing revision binaries.

## Render one frame

After simulation and static-scene export:

```sh
python tools/native.py run --build-dir .local/baseline -- surface --in cache/frame_000072.bin --out current.mesh --threads 4 --surface-scale 1 --support 2.4 --smooth 0
python tools/native.py run --build-dir .local/baseline -- trace --water current.mesh --solids solids.mesh --out current_render --width 320 --height 180 --spp 4 --bounces 8 --threads 4 --camera 1.45,1.85,3.55 --target 0,.83,0 --fov 29 --aperture 0 --environment neutral
ffmpeg -i current_render.ppm -frames:v 1 current_render.png
```

These flags are present in the inspected source and reproduce the settings used by the delivered baseline helper. Increase `--spp` to reduce sampling noise; that does not repair the underlying fluid choreography or thin-sheet resolution.

## Render the ten-second baseline

Create a Python environment with `python -m venv .venv`. For the following commands use `.venv/bin/python` on Linux/macOS or `.venv/Scripts/python.exe` on Windows in place of `python`:

```text
python -m pip install -r requirements-render.txt
python scripts/render_batch.py 0 240 --build-dir .local/baseline
ffmpeg -i batch_000_240.mp4 -c:v libx264 -pix_fmt yuv420p baseline.mp4
```

The range end is exclusive. The helper reconstructs each saved fluid state and invokes the native tracer; it does not interpolate missing frames. It overwrites `current.mesh` and `current_render.*`, so do not run batches concurrently in the same working tree. A full render can be much slower than the coarse simulation.

The tracer's built-in check is included in `python tools/native.py test --build-dir .local/baseline`. Historical solver/tracer check output belongs to the earlier reconstruction checkpoint and is clearly labeled there.

## Development status

### Baseline: pipeline complete, visual target unmet

The guide phases are gather, split, helix, whip, ring, and force fade/collapse. The dx=0.14 m grid is coarse, whitewater is disabled, and the persistent inlets continue into the intended collapse. Thin separated ribbons and a stable ring are not reliably maintained. The baseline is a solver/rendering prototype, not photorealistic or physically validated waterbending.

### Geometry experiment: failed visual acceptance

The guide uses closest-point projections on line segments, stronger/wider attraction and flow, and removes permanent inlets. Its saved validation records 241 states at dx=0.08 m, with selected 16-spp renders at 2, 5, and 7 seconds. The elevated ribbon/ring shape is lost near the midpoint. See [experiment notes](experiments/geometry/README.md).

![Geometry experiment at 5 seconds: failed shape retention](experiments/geometry/frames/render_120.png)

### Earlier transport experiment: solver completes, native surface fails

![Transport experiment particle-state diagnostic](media/transport-diagnostic.gif)

The animation is a reduced 12-fps view of the [full 960×540 particle diagnostic](media/transport-diagnostic.mp4), not reconstructed or refracted water. The saved run reached ten seconds at dx=0.10 m. Its verification record reports 240 diagnostic states, 1,026–8,835 markers, and **native surface volume-solver failure at t=5 seconds**. There is no completed native beauty movie for this earlier revision. It is superseded by the separately documented retained-trick revision, not silently repaired in place. See [source, reproduction steps, and caveats](experiments/transport/README.md).

### Reconstruction checkpoint: experimental profile, not thin-sheet physics

The earlier mesher offers `--profile thin-sheet` and `--profile reference`. These are geometric parameter presets, not adaptive resolution or a physical film-preservation model. Its low-rate [20-frame movie](media/reconstruction-2fps.mp4) samples ten simulated seconds at 2 fps. The separate [64-spp detail still](media/reconstruction-detail.png) is not representative of that 4-spp movie. See [checkpoint notes](experiments/reconstruction/README.md).

## Verification and limitations

For this publication, source was statically inspected, local documentation links checked, JSON/Python syntax checked, historical and latest audit claims checked against source/data, and all six source videos strictly decoded. Media pixels were visually reviewed. The native project was **not compiled, simulated, or rerendered during that original publication review**. Later portability checks must be assessed separately from those historical delivery records. Existing run logs and assertions describe the delivered checkpoints only.

The source snapshot has no release version or pinned toolchain. Bitwise reproduction across compilers, platforms, OpenMP schedules, or video codecs is not promised. Original native checkpoints are ABI/configuration sensitive. Full multiphase water/air behavior, physically resolved thin films, reference-quality choreography, and a complete production gallery are outside the demonstrated scope.

## License

No project license was supplied with the reviewed source bundles. This publication adds no license grant.
