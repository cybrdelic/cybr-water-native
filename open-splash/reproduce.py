#!/usr/bin/env python3
"""Build, simulate, reconstruct and render the complete code-only splash revision.

This is a synchronous, two-stage reproducibility driver. It does not download
assets, generate images using a learned model, interpolate frames, or denoise.
A failed command stops the pipeline. Existing run directories are refused so a
new run cannot quietly mix old cached states with new source.

Requirements: Linux, C++17/OpenMP compiler, ffmpeg/ffprobe, Python 3.10+,
NumPy, pandas, Pillow. The large second domain needs several GB of RAM. The
complete state sequence and temporary reconstruction require several GB of disk.
"""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path
from typing import Sequence

ROOT = Path(__file__).resolve().parent
LOWER = [-6.0, -.12, -5.1]
UPPER = [4.02, 2.7, 7.5]


def run(command: Sequence[str], log: Path, cwd: Path | None = None) -> None:
    """Execute without a shell; retain full stdout/stderr for provenance."""
    print(' '.join(str(x) for x in command), flush=True)
    with log.open('w', encoding='utf-8') as output:
        result = subprocess.run(list(map(str, command)), stdout=output,
                                stderr=subprocess.STDOUT, cwd=cwd, check=False)
    if result.returncode:
        tail = log.read_text(encoding='utf-8', errors='replace')[-5000:]
        raise RuntimeError(f'Command failed ({result.returncode}); {log}\n{tail}')


def vector(values: Sequence[float]) -> str:
    return ','.join(str(x) for x in values)


def keep_prefix(path: Path, field: str, boundary: float) -> None:
    """Keep recorded prefix; the exact handoff state is written once anew."""
    with path.open(newline='') as source:
        reader = csv.DictReader(source)
        names = reader.fieldnames
        rows = [row for row in reader if float(row[field]) < boundary]
    if not names:
        raise ValueError(f'Missing CSV header in {path}')
    with path.open('w', newline='') as destination:
        writer = csv.DictWriter(destination, fieldnames=names)
        writer.writeheader()
        writer.writerows(rows)


def check_handoff(before: Path, after: Path) -> dict:
    import numpy as np
    a = np.fromfile(before, dtype='<f4', offset=32).reshape(-1, 8)
    b = np.fromfile(after, dtype='<f4', offset=32).reshape(-1, 8)
    result = {
        'marker_count': len(a),
        'positions_exact': np.array_equal(a[:, :3], b[:, :3]),
        'velocities_exact': np.array_equal(a[:, 3:6], b[:, 3:6]),
        'volume_bytes_exact': before.with_suffix('.volumes').read_bytes() == after.with_suffix('.volumes').read_bytes(),
        'id_bytes_exact': before.with_suffix('.ids').read_bytes() == after.with_suffix('.ids').read_bytes(),
        'max_position_change_m': float(np.max(np.abs(a[:, :3] - b[:, :3]))),
        'max_velocity_change_m_s': float(np.max(np.abs(a[:, 3:6] - b[:, 3:6]))),
    }
    for key in ('positions_exact', 'velocities_exact', 'volume_bytes_exact', 'id_bytes_exact'):
        if not result[key]:
            raise AssertionError(f'Handoff changed {key}')
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True,
                        help='New, empty destination outside the delivered source tree.')
    parser.add_argument('--sim-threads', type=int, default=3)
    parser.add_argument('--render-threads', type=int, default=4)
    parser.add_argument('--compiler', default='g++')
    parser.add_argument('--previous-video', type=Path,
                        help='Optional old MP4 for the labelled before/after image.')
    args = parser.parse_args()
    if min(args.sim_threads, args.render_threads) < 1:
        parser.error('Thread counts must be positive.')
    out = args.output.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error('Output must be a new or empty directory.')
    if out == ROOT or ROOT in out.parents:
        parser.error('Place output outside the source package.')
    for tool in (args.compiler, 'ffmpeg', 'ffprobe'):
        if shutil.which(tool) is None:
            parser.error(f'Required executable not found: {tool}')
    out.mkdir(parents=True, exist_ok=True)
    shutil.copytree(ROOT / 'engine', out / 'engine',
                    ignore=shutil.ignore_patterns('build', '__pycache__'))
    for name in ('render_sequence.py', 'verify_delivery.py'):
        shutil.copy2(ROOT / name, out / name)
    for directory in ('cache', 'scene', 'handoff_state', 'delivery', 'engine/build'):
        (out / directory).mkdir(parents=True, exist_ok=True)
    engine = out / 'engine'
    binaries = engine / 'build'
    flags = ['-O3', '-std=c++17', '-fopenmp']
    targets = {
        'gallery_sim': 'src/gallery_simulate.cpp',
        'gallery_export': 'src/gallery_export.cpp',
        'surface_native': 'src/surface.cpp',
        'trace_native': 'src/trace.cpp',
        'solver_checks': 'tests/solver_checks.cpp',
        'splash_boundary_checks': 'tests/splash_boundary_checks.cpp',
        'fragment_phase_checks': 'tests/fragment_phase_checks.cpp',
        'retained_guidance_checks': 'tests/retained_guidance_checks.cpp',
    }
    for binary, source in targets.items():
        run([args.compiler, *flags, str(engine / source), '-o', str(binaries / binary)],
            out / f'build_{binary}.log')
    test_names = {'solver_checks': 'solver_tests.log',
                  'splash_boundary_checks': 'splash_boundary_checks.json',
                  'fragment_phase_checks': 'fragment_phase_checks.json',
                  'retained_guidance_checks': 'retained_guidance_checks.json'}
    for binary, filename in test_names.items():
        run([str(binaries / binary)], out / filename)
        if not json.loads((out / filename).read_text())['passed']:
            raise AssertionError(filename)
    run([str(binaries / 'trace_native'), '--self-test'], out / 'optics_native_tests.json')

    export = [str(binaries / 'gallery_export'), '--scene', 'waterbending_trick',
              '--quality', '1.5', '--sdf-dx', '.02']
    run([*export, '--out', str(out / 'scene/solids.mesh'),
         '--sdf-out', str(out / 'scene/solid.sdf')], out / 'solids.log')
    run([*export, '--out', str(out / 'scene/solids_expanded_copy.mesh'),
         '--sdf-out', str(out / 'scene/solid_large.sdf'),
         '--sdf-lower', vector(LOWER), '--sdf-upper', vector(UPPER)], out / 'solid_large.log')

    base = [str(binaries / 'gallery_sim'), '--scene', 'waterbending_trick',
            '--dx', '.03', '--duration', '10', '--fps', '24',
            '--threads', str(args.sim_threads), '--out', str(out / 'cache'),
            '--apic', '--particles-per-axis', '3', '--reseed-interval', '0',
            '--whitewater-rate', '18000', '--max-whitewater', '180000']
    run([*base, '--stop-after-frame', '216'], out / 'simulation.log')
    complete = json.loads((out / 'cache/complete.json').read_text())
    if complete['frame'] != 216 or abs(complete['time'] - 9) > 1e-8:
        raise AssertionError('Stage one did not stop at the intended handoff.')
    step_base = int(complete['steps'])
    for suffix in ('.bin', '.ids', '.volumes', '.white'):
        shutil.copy2(out / 'cache' / ('frame_000216' + suffix),
                     out / 'handoff_state' / ('frame_000216' + suffix))
    shutil.copy2(out / 'cache/manifest.json', out / 'stage1_manifest.json')
    (out / 'cache/complete.json').unlink()
    for filename in ('retention.csv', 'domain_audit.csv'):
        keep_prefix(out / 'cache' / filename, 'frame', 216)
    keep_prefix(out / 'cache/diagnostics.csv', 'time', 9)
    handoff = {
        'at_frame': 216, 'time_s': 9, 'step_base': step_base,
        'reason': 'Keep the unforced splash away from numerical open exits.',
        'preserved': 'Primary positions, velocities, volumes, IDs; saved secondary kinematics.',
        'reconstructed': 'APIC affine velocity gradients; pressure warm start reset. Not a bitwise checkpoint continuation.',
        'stage1': {'lower': [-3.3, -.12, -2.52], 'upper': [3.3, 3.3, 2.52]},
        'stage2': {'lower': LOWER, 'upper': UPPER},
    }
    (out / 'DOMAIN_HANDOFF.json').write_text(json.dumps(handoff, indent=2))
    run([*base, '--import-frame', str(out / 'handoff_state/frame_000216.bin'),
         '--start-step', str(step_base), '--domain-lower', vector(LOWER),
         '--domain-upper', vector(UPPER)], out / 'simulation_expanded.log')
    result = check_handoff(out / 'handoff_state/frame_000216.bin', out / 'cache/frame_000216.bin')
    (out / 'handoff_conservation_test.json').write_text(json.dumps(result, indent=2))
    run([sys.executable, str(out / 'render_sequence.py'), '--threads', str(args.render_threads)],
        out / 'render.log')
    run(['ffmpeg', '-v', 'error', '-y', '-framerate', '24', '-i',
         str(out / 'rendered/frame_%06d.png'), '-frames:v', '240', '-c:v', 'libx264',
         '-preset', 'slow', '-crf', '17', '-pix_fmt', 'yuv420p', '-movflags', '+faststart',
         str(out / 'delivery/CYBR_WATERBENDING_OPEN_SPLASH_10S.mp4')], out / 'encode.log')
    verification = [sys.executable, str(out / 'verify_delivery.py')]
    if args.previous_video:
        verification += ['--previous-video', str(args.previous_video.resolve())]
    run(verification, out / 'verification.log')
    print(f'Completed and verified: {out / "delivery"}', flush=True)


if __name__ == '__main__':
    main()
