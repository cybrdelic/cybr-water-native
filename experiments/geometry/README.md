# Geometry guide experiment

Status: **failed visual acceptance**. This header replaces the baseline `gallery_scenes.hpp`; it does not replace the shared solver. It uses segment projections for guide lookup, increases attraction/flow, and disables permanent inlets.

[VALIDATION.json](evidence/VALIDATION.json) records a completed ten-second, 24-Hz simulation at dx=0.08 m, 241 states, and a volume-ledger error of approximately 2.95e-12 m³. This numerical ledger does not establish visual success. The native 16-spp frames at [2 s](frames/render_048.png), [5 s](frames/render_120.png), and [7 s](frames/render_168.png) show loss of the desired raised ribbons/ring. No full-length successful repair movie is claimed.

To isolate this source variant, copy `engine/` to a new local build directory, replace only `src/gallery_scenes.hpp` with this header, and rebuild. The full exact original geometry-run command was not included, so no bitwise reproduction command is asserted. Do not apply this patch over the transport variant and call it the latest revision.

This is historical evidence; the experiment was not rerun for publication.
