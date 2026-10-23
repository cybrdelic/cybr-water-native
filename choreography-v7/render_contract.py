"""Validate native render provenance and publish complete PNG files.

Worker counts and absolute working directories are not visual settings. Source,
scene geometry, camera, reconstruction, sampling and optics are.
"""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import tempfile
from typing import Any

VALUE_FLAGS = (
    '--width', '--height', '--spp', '--bounces', '--branch-cutoff', '--camera',
    '--target', '--fov', '--aperture', '--environment', '--sky-power', '--sun-power',
    '--absorption', '--wet-level', '--wet-reach', '--adaptive-aa', '--aa-threshold',
    '--diffuse-samples',
)
BOOL_FLAGS = ('--deterministic-preview', '--coherent-normals', '--oriented-foam')
SURFACE_SETTINGS = {
    'normal_filter_passes': 0,
    'coherent_fragment_hysteresis': True,
    'topology_normal_filter_passes': 4,
    'tangential_center_relaxation': 0.25,
    'support_markers': 6,
    'scalar_smoothing_passes': 4,
}

def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()

def save_png_atomic(image: Any, path: Path) -> None:
    """Publish a flushed PNG without exposing partial bytes at the final path."""
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='wb', dir=path.parent,
                                         prefix=path.name + '.', suffix='.tmp',
                                         delete=False) as stream:
            temporary = Path(stream.name)
            image.save(stream, format='PNG')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)

def visual_options(command: list[str]) -> dict[str, Any]:
    answer: dict[str, Any] = {}
    for flag in VALUE_FLAGS:
        if command.count(flag) != 1:
            raise RuntimeError(f'Expected exactly one {flag} in native render command')
        index = command.index(flag)
        if index + 1 == len(command):
            raise RuntimeError(f'Missing value for {flag}')
        answer[flag] = command[index + 1]
    for flag in BOOL_FLAGS:
        answer[flag] = flag in command
    return answer

def make_contract(source: dict[str, str], command: list[str], assets: Path) -> dict[str, Any]:
    return {
        'format': 'CYBR native duet render contract 1',
        'source_code_sha256': source,
        'visual_options': visual_options(command),
        'surface_settings': SURFACE_SETTINGS,
        'scene_assets_sha256': {
            name: sha(assets / name) for name in ('solids.mesh', 'solid.sdf')
        },
    }

def check_receipt(receipt: dict[str, Any], contract: dict[str, Any]) -> None:
    if receipt['source_code_sha256'] != contract['source_code_sha256']:
        raise RuntimeError('Cannot resume a different native source revision')
    if visual_options(receipt['command']) != contract['visual_options']:
        raise RuntimeError('Cannot mix camera, sampling, resolution or optical settings')
    for key, expected in contract['surface_settings'].items():
        if receipt['surface'].get(key) != expected:
            raise RuntimeError(f'Cannot mix reconstruction setting {key}')

def freeze_contract(out: Path, contract: dict[str, Any]) -> None:
    """Write once, or compare exactly. Legacy receipts are checked, not trusted.

    Adoption verifies all recorded render/reconstruction settings. Old receipts
    do not contain geometry hashes, so adoption is refused unless there are no
    frames. Existing pre-contract runs should be completed by their original
    pinned runner, not relabelled as contract-verified.
    """
    path = out / 'render_contract.json'
    if path.exists():
        if json.loads(path.read_text()) != contract:
            raise RuntimeError('Native render contract changed; use a fresh take directory')
        return
    receipts = list(out.glob('frame_*.json'))
    if receipts:
        raise RuntimeError('Pre-contract frames require the original pinned runner; refusing unverifiable adoption')
    temporary = path.with_name(path.name + '.tmp')
    with temporary.open('x') as stream:
        json.dump(contract, stream, indent=2, sort_keys=True)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)
