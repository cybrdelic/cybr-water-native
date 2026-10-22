#!/usr/bin/env python3
"""Verify/reassemble the six original snapshots stored as lossless byte parts.

No downloads or simulation are performed. Existing matching snapshots are kept;
different existing files are never overwritten. Run with --check-only to verify
all part and whole-file hashes without creating reconstructed snapshots.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parent


def contained(name: str) -> Path:
    relative = Path(name)
    if relative.is_absolute() or '..' in relative.parts:
        raise ValueError(f'Unsafe relative path: {name}')
    path = ROOT / relative
    if path.is_symlink() or ROOT not in path.resolve().parents:
        raise ValueError(f'Path is not contained in this package: {name}')
    return path


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check-only', action='store_true')
    args = parser.parse_args()
    manifest = json.loads((ROOT / 'transport-parts.json').read_text())
    if manifest['format'] != 'lossless-file-parts-v1':
        raise ValueError('Unknown parts format')
    for entry in manifest['files']:
        target = contained(entry['path'])
        whole = hashlib.sha256()
        total = 0
        for part in entry['parts']:
            path = contained(part['path'])
            data = path.read_bytes()
            if len(data) != part['bytes'] or hashlib.sha256(data).hexdigest() != part['sha256']:
                raise ValueError(f'Damaged part: {part["path"]}')
            whole.update(data)
            total += len(data)
        if total != entry['bytes'] or whole.hexdigest() != entry['sha256']:
            raise ValueError(f'Combined snapshot mismatch: {entry["path"]}')
        if args.check_only:
            print(f'Verified {entry["path"]}: {total} bytes, {entry["sha256"]}')
            continue
        if target.exists():
            if target.stat().st_size != total or digest(target) != entry['sha256']:
                raise FileExistsError(f'Refusing to replace different file: {target}')
            print(f'Already restored: {entry["path"]}')
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        fd, temporary_name = tempfile.mkstemp(prefix=target.name + '.', suffix='.tmp', dir=target.parent)
        temporary = Path(temporary_name)
        try:
            with os.fdopen(fd, 'wb') as output:
                for part in entry['parts']:
                    with contained(part['path']).open('rb') as source:
                        for block in iter(lambda: source.read(1024 * 1024), b''):
                            output.write(block)
            if temporary.stat().st_size != total or digest(temporary) != entry['sha256']:
                raise ValueError(f'Read changed during restore: {entry["path"]}')
            # Atomic creation without overwriting an existing destination.
            try:
                os.link(temporary, target)
            except OSError as exc:
                if os.name != 'nt' or getattr(exc, 'winerror', None) not in (1, 50):
                    raise
                # Windows rename refuses an existing destination even in a race.
                os.rename(temporary, target)
            print(f'Restored {entry["path"]}: {total} bytes')
        finally:
            temporary.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
