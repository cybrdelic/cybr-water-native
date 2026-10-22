#!/usr/bin/env python3
"""Observe a selected one-frame run and independently audit its completed cache.

This monitor does not change solver state, remove outputs, or resume a process.
The .audited marker is published only after the independent reader and the
source/volume ledgers pass. Its output is safe for the renderer to consume.
"""
import argparse
import csv
import json
import math
import subprocess
import time
from pathlib import Path


def read_rows(path):
    with path.open() as f:
        return list(csv.DictReader(f))


def atomic_json(path, data):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(data, indent=2) + "\n")
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--auditor", type=Path, required=True)
    args = parser.parse_args()
    cache, evidence = args.cache.resolve(), args.evidence.resolve()
    auditor = args.auditor.resolve()
    started = time.monotonic()
    last_report = 0.0
    last_time = 0.0
    peak_rss_kib = 0
    while not (cache / "complete.json").exists():
        status_path = Path(f"/proc/{args.pid}/status")
        if not status_path.exists():
            raise RuntimeError("Solver process exited without publishing complete.json")
        try:
            for line in status_path.read_text().splitlines():
                if line.startswith("VmHWM:"):
                    peak_rss_kib = max(peak_rss_kib, int(line.split()[1]))
            rows = read_rows(cache / "diagnostics.csv")
            row = rows[-1]
            last_time = float(row["time"])
            now = time.monotonic()
            if now - last_report >= 30:
                report = {"time_s": last_time, "step": int(row["step"]),
                          "particles": int(row["particles"]),
                          "max_speed_m_s": float(row["max_speed_m_s"]),
                          "step_seconds": float(row["step_seconds"]),
                          "peak_solver_rss_mib": peak_rss_kib / 1024,
                          "monitor_elapsed_seconds": now - started}
                atomic_json(evidence / "production_progress.json", report)
                print(json.dumps(report), flush=True)
                last_report = now
        except (FileNotFoundError, IndexError, KeyError, TypeError, ValueError):
            # The CSV is flushed every step; an observation can overlap a write.
            pass
        time.sleep(2)

    complete = json.loads((cache / "complete.json").read_text())
    frame = int(complete["frame"])
    base = cache / f"frame_{frame:06d}"
    result = subprocess.run([str(auditor), str(base.with_suffix(".bin")), "64"],
                            text=True, capture_output=True, check=True)
    audit = json.loads(result.stdout)
    if not audit.get("passed"):
        raise RuntimeError("Independent state reader did not pass")
    rows = read_rows(cache / "diagnostics.csv")
    row = rows[-1]
    manifest = json.loads((cache / "manifest.json").read_text())
    nominal_volume = float(manifest["marker_spacing_m"]) ** 3
    ledger = float(row["volume_ledger_error_m3"])
    source_error = (float(row["emitted_volume_m3"])
                    - float(row["prescribed_open_grid_inlet_volume_m3"]))
    if not math.isfinite(ledger) or abs(ledger) > 1e-8:
        raise RuntimeError(f"Primary volume ledger exceeded audit gate: {ledger}")
    if not math.isfinite(source_error) or abs(source_error) > nominal_volume * 1.1 + 1e-8:
        raise RuntimeError(f"Prescribed-source budget exceeded marker quantization: {source_error}")
    if audit["primary_count"] != int(row["particles"]):
        raise RuntimeError("Final primary count disagrees with substep diagnostics")
    secondary = sum(int(row[k]) for k in ("white_spray", "white_foam", "white_bubble"))
    if audit["whitewater_count"] != secondary:
        raise RuntimeError("Final whitewater count disagrees with diagnostics")
    summary = {"passed": True, "complete": complete, "state_audit": audit,
               "volume_ledger_error_m3": ledger,
               "emission_minus_prescribed_volume_m3": source_error,
               "nominal_marker_volume_m3": nominal_volume,
               "peak_solver_rss_mib_observed": peak_rss_kib / 1024,
               "maximum_recorded_speed_m_s": max(float(r["max_speed_m_s"]) for r in rows),
               "maximum_pressure_relative_residual": max(float(r["pressure_relative_residual"]) for r in rows),
               "maximum_projected_divergence_l2_s_inv": max(float(r["divergence_after_l2_s_inv"]) for r in rows),
               "scope": "Numerical/cache audit; photorealism remains a rendered-image assessment."}
    velocity_path = cache / "velocity_diagnostics.csv"
    if velocity_path.exists():
        summary["final_velocity_diagnostics"] = read_rows(velocity_path)[-1]
    atomic_json(evidence / f"production_frame_{frame:06d}_audit.json", summary)
    atomic_json(base.with_suffix(".audited"), {"passed": True, "frame": frame,
                                               "audit_file": str(evidence / f"production_frame_{frame:06d}_audit.json")})
    print(json.dumps({"status": "audited", "frame": frame,
                      "time_s": complete["time"], "particles": audit["primary_count"]}), flush=True)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(json.dumps({"status": "failed", "error": str(error)}), flush=True)
        raise
