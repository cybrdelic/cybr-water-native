#!/usr/bin/env python3
"""Changing hidden fluid topology must not reseed visible solid materials."""
from __future__ import annotations

import hashlib
import json
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def write_mesh(path: Path, triangles: list[tuple[list[tuple[float, ...]], int]]) -> None:
    with path.open("wb") as stream:
        stream.write(struct.pack("<8sIIQ", b"CYBRMSH1", 1, 0, len(triangles)))
        for vertices, material in triangles:
            stream.write(struct.pack("<18fI", *(value for v in vertices for value in v), material))


def run() -> dict:
    with tempfile.TemporaryDirectory(prefix="cybr-trace-material-identity-") as temporary:
        directory = Path(temporary)
        rock = [(-2.0, -2.0, 0.0, 0.0, 0.0, 1.0),
                (2.0, -2.0, 0.0, 0.0, 0.0, 1.0),
                (0.0, 2.0, 0.0, 0.0, 0.0, 1.0)]
        write_mesh(directory / "rock.mesh", [(rock, 1)])
        hashes: list[str] = []
        stats: list[dict] = []
        for count in (1, 17):
            # The liquid is far behind the opaque receiver. Neither the camera
            # ray nor any valid reflected path can see it, so only input topology
            # and loading order differ. Its material must not affect the rock.
            water = []
            for index in range(count):
                x = float(index * 4)
                vertices = [(x - 1, -1.0, -1000.0, 0.0, 0.0, 1.0),
                            (x + 1, -1.0, -1000.0, 0.0, 0.0, 1.0),
                            (x, 1.0, -1000.0, 0.0, 0.0, 1.0)]
                water.append((vertices, 0))
            write_mesh(directory / "water.mesh", water)
            output = directory / f"case_{count}"
            command = [str(ROOT / "build/trace"), "--water", str(directory / "water.mesh"),
                       "--solids", str(directory / "rock.mesh"), "--out", str(output),
                       "--width", "64", "--height", "64", "--spp", "16",
                       "--bounces", "12", "--threads", "1", "--seed", "29",
                       "--camera", "0,0,3", "--target", "0,0,0", "--fov", "35",
                       "--aperture", "0", "--environment", "forest"]
            subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            hashes.append(hashlib.sha256(output.with_suffix(".pfm").read_bytes()).hexdigest())
            stats.append(json.loads(output.with_suffix(".json").read_text()))
        assert hashes[0] == hashes[1], "Hidden fluid topology changed the visible solid shading"
        assert all(s["dielectric_events"] == 0 for s in stats), "Hidden liquid was unexpectedly visible"
        result = {
            "passed": True,
            "water_triangle_counts": [1, 17],
            "visible_rock_triangles": 1,
            "raw_pfm_sha256": hashes,
            "identical_raw_pixels": True,
            "dielectric_events": [s["dielectric_events"] for s in stats],
            "source_sha256": hashlib.sha256((ROOT / "src/trace.cpp").read_bytes()).hexdigest(),
            "executable_sha256": hashlib.sha256((ROOT / "build/trace").read_bytes()).hexdigest(),
        }
        (ROOT / "evidence/trace_temporal_identity.json").write_text(json.dumps(result, indent=2) + "\n")
        return result


if __name__ == "__main__":
    print(json.dumps(run(), indent=2))
