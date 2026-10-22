"""Portable build, checks and bounded native smoke for the preserved WATER engines."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REVISIONS = ("open-splash", "retained-trick", "baseline")


def runtime_environment(build_dir: Path) -> dict[str, str]:
    """Use an installed runtime for child processes; never copy DLLs or edit PATH globally."""
    env = os.environ.copy()
    metadata = build_dir / "water-build.json"
    if metadata.exists():
        info = json.loads(metadata.read_text(encoding="utf-8"))
        directory = info.get("runtime_dir", "")
        if directory:
            if not Path(directory).is_dir():
                raise RuntimeError("Configured OpenMP runtime is missing; reconfigure this build.")
            env["PATH"] = directory + os.pathsep + env.get("PATH", "")
    return env


def executable(build_dir: Path, name: str) -> Path:
    if Path(name).name != name or name in (".", ".."):
        raise ValueError("Native executable must be a simple target name.")
    suffix = ".exe" if os.name == "nt" else ""
    for directory in (build_dir / "bin", build_dir):
        for candidate in (name, name + "_native"):
            path = directory / (candidate + suffix)
            if path.is_file():
                return path.resolve()
    raise FileNotFoundError(f"Build target {name!r} not found in {build_dir}; run the build command first.")


def command(args, *, log: Path | None = None, env=None, timeout=None):
    args = [str(x) for x in args]
    print(subprocess.list2cmdline(args), flush=True)
    if log is None:
        subprocess.run(args, check=True, env=env, timeout=timeout)
    else:
        with log.open("w", encoding="utf-8") as stream:
            completed = subprocess.run(args, stdout=stream, stderr=subprocess.STDOUT,
                                       env=env, timeout=timeout)
        if completed.returncode:
            tail = log.read_text(encoding="utf-8", errors="replace")[-5000:]
            raise RuntimeError(f"Command failed ({completed.returncode}); {log}\n{tail}")


def build(build_dir: Path, revision="open-splash", *, jobs=2, openmp="AUTO",
          compiler=None, engine_dir=None, cmake_args=()):
    build_dir = build_dir.resolve()
    build_dir.mkdir(parents=True, exist_ok=True)
    arguments = ["cmake", "-S", ROOT, "-B", build_dir,
                 f"-DWATER_REVISION={revision}", f"-DWATER_OPENMP={openmp}",
                 f"-DPython3_EXECUTABLE={sys.executable}"]
    if compiler:
        arguments.append(f"-DCMAKE_CXX_COMPILER={compiler}")
    if engine_dir:
        arguments.append(f"-DWATER_ENGINE_DIR={Path(engine_dir).resolve()}")
    arguments.extend(cmake_args)
    command(arguments, log=build_dir / "configure.log")
    command(["cmake", "--build", build_dir, "--config", "Release", "--parallel", jobs],
            log=build_dir / "build.log")
    print((build_dir / "water-build.json").read_text(encoding="utf-8"), flush=True)


def test(build_dir: Path):
    command(["ctest", "--test-dir", build_dir, "-C", "Release", "--output-on-failure"],
            env=runtime_environment(build_dir))


def read_snapshot(path: Path):
    raw = path.read_bytes()
    if len(raw) < 32:
        raise AssertionError("Truncated solver snapshot")
    magic, version, count, frame, time, spacing, radius = struct.unpack("<8sIIIfff", raw[:32])
    if magic != b"CYBRWTR1" or version != 1 or not count or len(raw) != 32 + count * 32:
        raise AssertionError("Invalid solver snapshot layout/count")
    values = list(struct.iter_unpack("<8f", raw[32:]))
    if not all(math.isfinite(x) for particle in values for x in particle):
        raise AssertionError("Nonfinite native solver output")
    return {"count": count, "frame": frame, "time": time,
            "sha256": hashlib.sha256(raw).hexdigest(), "values": values}


def smoke(build_dir: Path, output: Path):
    """Advance native APIC, reconstruct its surface and render a small actual frame."""
    output = output.resolve()
    if output.exists():
        raise FileExistsError(f"Smoke output already exists; choose a new directory: {output}")
    output.mkdir(parents=True)
    env = runtime_environment(build_dir)
    cache = output / "cache"
    command([executable(build_dir, "gallery_sim"), "--scene", "drop", "--dx", ".01",
             "--duration", str(1 / 24), "--fps", "24", "--threads", "2", "--out", cache,
             "--apic", "--no-whitewater", "--particles-per-axis", "2",
             "--pressure-iterations", "120"], log=output / "simulate.log", env=env, timeout=180)
    before = read_snapshot(cache / "frame_000000.bin")
    after = read_snapshot(cache / "frame_000001.bin")
    complete = json.loads((cache / "complete.json").read_text())
    if after["frame"] != 1 or after["time"] <= before["time"] or complete["steps"] <= 0:
        raise AssertionError("Native integration did not advance")
    if before["sha256"] == after["sha256"] or before["values"] == after["values"]:
        raise AssertionError("Native particle state did not evolve; header changes alone are insufficient")
    scene = json.loads((cache / "gallery_scene.json").read_text())
    mesh = output / "water.mesh"
    solids = output / "solids.mesh"
    command([executable(build_dir, "gallery_export"), "--scene", "drop", "--quality", "2",
             "--out", solids], log=output / "export.log", env=env, timeout=180)
    command([executable(build_dir, "surface"), "--in", cache / "frame_000001.bin",
             "--out", mesh, "--threads", "2", "--surface-scale", "1",
             "--support", "2.4", "--smooth", "0"], log=output / "surface.log",
             env=env, timeout=180)
    stem = output / "preview"
    command([executable(build_dir, "trace"), "--water", mesh, "--solids", solids,
             "--out", stem, "--width", "64", "--height", "36", "--spp", "1",
             "--bounces", "2", "--threads", "2", "--aperture", "0",
             "--camera", ",".join(map(str, scene["camera"])),
             "--target", ",".join(map(str, scene["target"])),
             "--fov", str(scene["vertical_fov_degrees"]), "--environment", "neutral"],
             log=output / "trace.log", env=env, timeout=180)
    ppm = stem.with_suffix(".ppm")
    with ppm.open("rb") as stream:
        if stream.readline().strip() != b"P6":
            raise AssertionError("Expected a native P6 image")
        dimensions = stream.readline().split()
        if dimensions != [b"64", b"36"] or stream.readline().strip() != b"255":
            raise AssertionError("Native image dimensions/range mismatch")
        pixels = stream.read()
    if len(pixels) != 64 * 36 * 3 or len(set(pixels)) < 2:
        raise AssertionError("Native image is truncated or constant")
    report = {"scope": "One coarse native APIC frame interval, surface reconstruction and 64x36 1-spp render; not a full-film or physics acceptance test.",
              "build": json.loads((build_dir / "water-build.json").read_text()),
              "native_steps": complete["steps"],
              "particle_values_changed": True,
              "snapshots": [{k: v for k, v in state.items() if k != "values"} for state in (before, after)],
              "image_bytes": ppm.stat().st_size,
              "image_sha256": hashlib.sha256(ppm.read_bytes()).hexdigest()}
    (output / "smoke.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    p = commands.add_parser("build")
    p.add_argument("--revision", choices=REVISIONS, default="open-splash")
    p.add_argument("--build-dir", type=Path)
    p.add_argument("--engine-dir", type=Path)
    p.add_argument("--compiler")
    p.add_argument("--jobs", type=int, default=2)
    p.add_argument("--openmp", type=str.upper, choices=("AUTO", "ON", "OFF"), default="AUTO")
    p.add_argument("--cmake-arg", action="append", default=[])
    for action in ("test", "smoke", "run"):
        p = commands.add_parser(action)
        p.add_argument("--build-dir", type=Path, required=True)
        if action == "smoke":
            p.add_argument("--output", type=Path, required=True)
        if action == "run":
            p.add_argument("native_command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    try:
        if args.action == "build":
            if args.jobs < 1:
                parser.error("--jobs must be positive")
            build(args.build_dir or ROOT / ".local" / args.revision, args.revision,
                  jobs=args.jobs, openmp=args.openmp, compiler=args.compiler,
                  engine_dir=args.engine_dir, cmake_args=args.cmake_arg)
        else:
            directory = args.build_dir.resolve()
            if not (directory / "water-build.json").is_file():
                parser.error("No configured WATER build at --build-dir")
            if args.action == "test":
                test(directory)
            elif args.action == "smoke":
                smoke(directory, args.output)
            else:
                values = args.native_command
                if values and values[0] == "--":
                    values = values[1:]
                if not values:
                    parser.error("Provide a native target and its arguments after --")
                command([executable(directory, values[0]), *values[1:]],
                        env=runtime_environment(directory))
    except (OSError, RuntimeError, AssertionError, subprocess.SubprocessError) as exc:
        parser.exit(1, f"Native WATER command failed: {exc}\n")


if __name__ == "__main__":
    main()
