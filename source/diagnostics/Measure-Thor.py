"""Sample Thor upper-surface presentation, PID and thermal state without changing the device.

SurfaceFlinger returns a finite rolling history. Intervals are deduplicated only
within observed windows; gaps between non-overlapping windows are NOT counted
as frame stalls. At the default five-second interval, coverage is incomplete.
This measures surface presentation, never 60 Hz guest simulation or lower UI FPS.
"""

import argparse
from collections import Counter
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import re
import shlex
import shutil
import statistics
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parent.parent
MAX_FENCE = (1 << 63) - 1
SYSFS_DISCOVERY_SCRIPT = r'''
for z in /sys/class/thermal/thermal_zone*; do
  [ -r "$z/type" ] || continue
  n=$(cat "$z/type" 2>/dev/null) || continue
  printf 'thermal\t%s/temp\t%s\n' "$z" "$n"
done
for p in /sys/devices/system/cpu/cpufreq/policy*; do
  [ -d "$p" ] && printf 'cpu_frequency\t%s/scaling_cur_freq\n' "$p"
done
exit 0
'''
GPU_SYSFS_FIELDS = (
    {"kind": "gpu_frequency", "name": "kgsl_gpuclk", "path": "/sys/class/kgsl/kgsl-3d0/gpuclk", "raw_unit": "Hz"},
    {"kind": "gpu_busy", "name": "kgsl_gpubusy", "path": "/sys/class/kgsl/kgsl-3d0/gpubusy", "raw_unit": "driver_counter_pair"},
)


def parse_sysfs_discovery(result):
    """Discover once; only these exact sensor families are sampled (at most eight)."""
    temperatures, frequencies = [], []
    if result["returncode"] == 0:
        for line in result["stdout"].splitlines():
            fields = line.split("\t")
            if (len(fields) == 3 and fields[0] == "thermal"
                    and re.fullmatch(r"/sys/class/thermal/thermal_zone\d+/temp", fields[1])
                    and re.fullmatch(r"(?:cpuss-\d+|gpuss-\d+|cpu-\d+-\d+)", fields[2])):
                temperatures.append({"kind": "temperature", "name": fields[2],
                                     "path": fields[1], "raw_unit": "millidegree_C"})
            elif (len(fields) == 2 and fields[0] == "cpu_frequency"
                  and re.fullmatch(r"/sys/devices/system/cpu/cpufreq/policy\d+/scaling_cur_freq", fields[1])):
                frequencies.append({"kind": "cpu_frequency", "name": fields[1].split("/")[-2],
                                    "path": fields[1], "raw_unit": "kHz"})
    # Keep cpuss and gpuss represented before the per-core families. Never infer
    # package temperature from battery or attach a cause to a thermal reading.
    rank = {"cpuss": 0, "gpuss": 1, "cpu": 2}
    temperatures.sort(key=lambda s: (rank[s["name"].split("-")[0]],
                                    tuple(map(int, re.findall(r"\d+", s["name"]))), s["path"]))
    frequencies.sort(key=lambda s: int(s["name"][6:]))
    unique = lambda rows: list({row["path"]: row for row in rows}.values())
    selected = unique(temperatures)[:8] + unique(frequencies)[:8] + [dict(item) for item in GPU_SYSFS_FIELDS]
    return {"status": "discovered" if result["returncode"] == 0 else "unavailable",
            "temperature_limit": 8, "cpu_policy_limit": 8, "fields": selected,
            "note": "Names discovered once; kernel sysfs units retained. GPU counters do not establish GPU utilization or throttling."}


def sysfs_sample_script(fields):
    # Paths from discovery are restricted above; quote again for the POSIX shell.
    rows = []
    for index, field in enumerate(fields):
        path = shlex.quote(field["path"])
        rows.append(f"if v=$(cat {path} 2>/dev/null); then printf '%s\\t%s\\t%s\\n' {index} ok \"$v\"; "
                    f"else printf '%s\\t%s\\n' {index} unavailable; fi")
    return "\n".join(rows)


def parse_sysfs_sample(result, fields):
    rows = {}
    if result["returncode"] == 0:
        for line in result["stdout"].splitlines():
            parts = line.split("\t", 2)
            if len(parts) >= 2 and parts[0].isdigit():
                rows[int(parts[0])] = parts[1:]
    values = []
    for index, field in enumerate(fields):
        row = rows.get(index, ["unavailable"])
        value = {**field, "status": "unavailable", "raw_text": row[1] if len(row) > 1 else None}
        if row[0] == "ok" and len(row) > 1:
            raw = row[1].split()
            expected = 2 if field["kind"] == "gpu_busy" else 1
            if len(raw) == expected and all(re.fullmatch(r"-?\d+", n) for n in raw):
                integers = list(map(int, raw))
                value.update(status="available", raw_value=integers if expected == 2 else integers[0])
                if field["kind"] == "temperature":
                    value["temperature_c"] = integers[0] / 1000.0
            else:
                value["status"] = "invalid"
        values.append(value)
    return {"status": "sampled" if result["returncode"] == 0 else "unavailable", "readings": values}


def utc():
    return datetime.now(timezone.utc).isoformat()


def positive(value):
    try:
        number = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be a positive finite number") from error
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be a positive finite number")
    return number


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, help="Explicit USB serial or paired Wi-Fi endpoint")
    parser.add_argument("--package", required=True, help="Exact foreground game package")
    parser.add_argument("--output", required=True, type=Path, help="New JSON output; existing files are rejected")
    parser.add_argument("--duration", type=positive, default=300.0, help="Sampling duration in seconds (default: 300)")
    parser.add_argument("--interval", type=positive, default=5.0, help="Seconds between samples (default: 5; use <=0.5 for more overlap)")
    parser.add_argument("--layer", help="Exact upper SDL SurfaceFlinger layer; otherwise require one matching MainActivity SurfaceView")
    parser.add_argument("--adb", help="ADB executable; defaults to workspace platform-tools or PATH")
    parser.add_argument("--command-timeout", type=positive, default=15.0)
    parser.add_argument("--long-gap-ms", type=positive, default=25.0, help="Observed interval threshold, not a refresh-rate target (default: 25)")
    args = parser.parse_args()
    if not args.serial.strip() or re.search(r"\s", args.serial):
        parser.error("--serial must be nonempty and contain no whitespace")
    if not re.fullmatch(r"[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)+", args.package, re.ASCII):
        parser.error("--package must be an Android package name")
    if args.layer is not None and (not args.layer.strip() or "\n" in args.layer or "\r" in args.layer):
        parser.error("--layer must be a nonempty single exact layer name")
    args.output = args.output.resolve()
    args.raw = args.output.with_name(args.output.stem + ".samples")
    if args.output.suffix.lower() != ".json":
        parser.error("--output must have a .json suffix")
    if args.output.exists() or args.raw.exists():
        parser.error("output or its .samples directory already exists; choose a fresh output name")
    bundled = ROOT / ".tools/platform-tools/adb.exe"
    requested = args.adb or (str(bundled) if bundled.is_file() else "adb")
    args.adb = shutil.which(requested)
    if args.adb is None:
        parser.error("ADB executable not found; specify --adb")
    return args


def command(args, remote):
    # ADB shell joins its arguments through a remote POSIX shell. Quote layer
    # parentheses/brackets and every other remote argument explicitly.
    argv = [args.adb, "-s", args.serial, "shell", shlex.join(remote)]
    started = time.monotonic()
    try:
        result = subprocess.run(argv, capture_output=True, text=True, encoding="utf-8",
                                errors="replace", timeout=args.command_timeout)
        return {"remote_argv": remote, "returncode": result.returncode,
                "stdout": result.stdout, "stderr": result.stderr,
                "elapsed_seconds": time.monotonic() - started}
    except (subprocess.TimeoutExpired, OSError) as error:
        def decoded(value):
            return value.decode("utf-8", "replace") if isinstance(value, bytes) else (value or "")
        return {"remote_argv": remote, "returncode": None, "error": str(error),
                "stdout": decoded(getattr(error, "stdout", "")),
                "stderr": decoded(getattr(error, "stderr", "")),
                "elapsed_seconds": time.monotonic() - started}


def parse_latency(result):
    if result["returncode"] != 0:
        return None, []
    lines = result["stdout"].splitlines()
    period = int(lines[0].strip()) if lines and lines[0].strip().isdigit() else None
    timestamps = set()
    for line in lines[1:]:
        fields = line.split()
        if len(fields) == 3 and all(field.isdigit() for field in fields):
            # desiredPresentTime, actualPresentTime, frameReadyTime.
            actual = int(fields[1])
            if 0 < actual < MAX_FENCE:
                timestamps.add(actual)
    return period, sorted(timestamps)


def parse_thermal(result):
    raw = result["stdout"]
    status = re.search(r"Thermal\s+Status:\s*(\d+)", raw, re.IGNORECASE)
    sensors = []
    for entry in re.findall(r"Temperature\{([^}]+)\}", raw):
        fields = dict(re.findall(r"(m\w+)\s*=\s*([^,}]+)", entry))
        try:
            value = float(fields.get("mValue", "nan"))
            if math.isfinite(value):
                sensors.append({"name": fields.get("mName", "").strip(),
                                "type": fields.get("mType", "").strip(),
                                "status": fields.get("mStatus", "").strip(), "value": value})
        except ValueError:
            pass
    return {"status": int(status[1]) if status else None, "reported_sensors": sensors}


def distribution(values):
    if not values:
        return {"count": 0}
    values = sorted(values)
    def percentile(p):
        position = (len(values) - 1) * p
        low = math.floor(position)
        return values[low] + (values[math.ceil(position)] - values[low]) * (position - low)
    mean = statistics.fmean(values)
    return {"count": len(values), "min_ms": values[0], "mean_ms": mean,
            "p50_ms": percentile(0.50), "p95_ms": percentile(0.95),
            "p99_ms": percentile(0.99), "max_ms": values[-1],
            "observed_interval_rate_fps": 1000.0 / mean if mean else None}


class Timeline:
    def __init__(self, layer, pids, first, elapsed):
        self.layer, self.pids, self.baseline = layer, pids, max(first)
        self.timestamps = {self.baseline}
        self.pairs = {}
        self.windows = []
        self.periods = Counter()
        self.last_new_elapsed = elapsed
        self.no_new = []

    def add(self, timestamps, period, sample, elapsed):
        self.periods[str(period)] += 1
        current = [stamp for stamp in timestamps if stamp >= self.baseline]
        new = set(current) - self.timestamps
        if new:
            self.last_new_elapsed = elapsed
        else:
            self.no_new.append({"sample": sample, "seconds_since_last_observed_new_frame":
                                max(0.0, elapsed - self.last_new_elapsed)})
        self.timestamps.update(current)
        if current:
            self.windows.append((current[0], current[-1]))
        for previous, present in zip(current, current[1:]):
            self.pairs.setdefault((previous, present), {"period_ns": period, "first_sample": sample})

    def summary(self, threshold):
        intervals = [(end - start) / 1e6 for start, end in self.pairs]
        by_period = {}
        long_gaps = []
        for (start, end), info in self.pairs.items():
            milliseconds = (end - start) / 1e6
            by_period.setdefault(str(info["period_ns"]), []).append(milliseconds)
            if milliseconds > threshold:
                long_gaps.append({"from_actual_present_ns": start, "to_actual_present_ns": end,
                                  "interval_ms": milliseconds, **info})
        merged = []
        for start, end in sorted(self.windows):
            if merged and start <= merged[-1][1]:
                merged[-1][1] = max(merged[-1][1], end)
            else:
                merged.append([start, end])
        coverage_gaps = [{"from_ns": a[1], "to_ns": b[0], "unobserved_seconds": (b[0] - a[1]) / 1e9}
                         for a, b in zip(merged, merged[1:])]
        return {"layer": self.layer, "pids": self.pids,
                "first_snapshot_baseline_actual_present_ns": self.baseline,
                "unique_actual_present_timestamps_after_baseline": len(self.timestamps) - 1,
                "interval_distribution": distribution(intervals),
                "distribution_by_snapshot_reported_period_ns": {key: distribution(value) for key, value in by_period.items()},
                "reported_refresh_period_ns_samples": dict(self.periods),
                "long_gap_threshold_ms": threshold, "observed_long_gaps": long_gaps,
                "observed_window_coverage_seconds": sum(end - start for start, end in merged) / 1e9,
                "sampling_coverage_gaps_not_frame_stalls": coverage_gaps,
                "no_new_frame_observations": self.no_new}


def main():
    args = arguments()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive reservation and exclusive raw writes protect historical receipts.
    with args.output.open("x", encoding="utf-8") as output:
        json.dump({"status": "collecting", "started_utc": utc()}, output)
    args.raw.mkdir()
    started_utc, started = utc(), time.monotonic()
    timelines, samples, pid_observations = {}, [], []
    status, failure = "completed", None
    sysfs_discovery = None
    try:
        discovery_raw = command(args, ["sh", "-c", SYSFS_DISCOVERY_SCRIPT])
        sysfs_discovery = parse_sysfs_discovery(discovery_raw)
        with (args.raw / "sysfs-discovery.json").open("x", encoding="utf-8") as output:
            json.dump({"utc": utc(), "command": discovery_raw, "selection": sysfs_discovery}, output, indent=2)
        sysfs_script = sysfs_sample_script(sysfs_discovery["fields"])
        while not samples or time.monotonic() - started < args.duration:
            index = len(samples)
            raw = {"index": index, "utc": utc(), "elapsed_seconds": time.monotonic() - started}
            raw["pidof"] = command(args, ["pidof", args.package])
            pid_result = raw["pidof"]
            valid_pid_query = (pid_result["returncode"] == 0 or
                               (pid_result["returncode"] == 1 and not pid_result["stdout"].strip()
                                and not pid_result["stderr"].strip()))
            pids = sorted(int(pid) for pid in pid_result["stdout"].split() if pid.isdigit()) if valid_pid_query else None
            pid_observations.append({"sample": index, "elapsed_seconds": raw["elapsed_seconds"], "pids": pids})
            raw["layers"] = command(args, ["dumpsys", "SurfaceFlinger", "--list"])
            available = raw["layers"]["stdout"].splitlines() if raw["layers"]["returncode"] == 0 else []
            candidates = [name for name in available if
                          (name.startswith("SurfaceView[") or name.startswith("SurfaceView - "))
                          and re.search(r"(?<![\w.])" + re.escape(args.package) + r"/", name)
                          and "MainActivity" in name]
            raw["matching_surface_layers"] = candidates
            # Thor Android 13 exposes the SDL buffer as the BLAST child; a
            # similarly named SurfaceView container has no useful frame fences.
            buffered = [name for name in candidates if "(BLAST)" in name]
            if buffered:
                candidates = buffered
            raw["candidate_layers"] = candidates
            layer = args.layer if args.layer and args.layer in available else None
            if not args.layer and len(candidates) == 1:
                layer = candidates[0]
            if not args.layer and len(candidates) > 1:
                raw["selection_error"] = "Ambiguous upper SDL layers; rerun with an exact --layer"
            raw["selected_layer"] = layer
            sample = {"index": index, "utc": raw["utc"], "elapsed_seconds": raw["elapsed_seconds"],
                      "pids": pids, "pid_query_valid": valid_pid_query,
                      "selected_layer": layer, "raw_file": f"{index:06d}.json"}
            if not layer:
                sample["layer_unavailable"] = ("Requested exact layer is not currently listed" if args.layer
                                               else "No unique MainActivity SurfaceView currently listed")
            if "selection_error" in raw:
                sample["selection_error"] = raw["selection_error"]
            if layer:
                raw["latency"] = command(args, ["dumpsys", "SurfaceFlinger", "--latency", layer])
                period, timestamps = parse_latency(raw["latency"])
                sample.update(reported_refresh_period_ns=period, valid_ring_timestamps=len(timestamps))
                if timestamps and pids:
                    key = (tuple(pids), layer)
                    if key not in timelines:
                        # First snapshot establishes a watermark; don't report pre-run history.
                        timelines[key] = Timeline(layer, pids, timestamps, raw["elapsed_seconds"])
                        sample["first_snapshot_baseline_only"] = True
                    else:
                        timelines[key].add(timestamps, period, index, raw["elapsed_seconds"])
            raw["battery"] = command(args, ["dumpsys", "battery"])
            battery = re.search(r"^\s*temperature:\s*(-?\d+)\s*$", raw["battery"]["stdout"], re.MULTILINE)
            sample["battery_temperature_c"] = int(battery[1]) / 10.0 if battery and raw["battery"]["returncode"] == 0 else None
            raw["thermalservice"] = command(args, ["dumpsys", "thermalservice"])
            sample["thermal"] = parse_thermal(raw["thermalservice"])
            raw["sysfs"] = command(args, ["sh", "-c", sysfs_script])
            sample["sysfs"] = parse_sysfs_sample(raw["sysfs"], sysfs_discovery["fields"])
            sample["command_failures"] = [key for key in ("layers", "latency", "battery", "thermalservice", "sysfs")
                                          if key in raw and raw[key]["returncode"] != 0]
            sample["collection_seconds"] = time.monotonic() - started - raw["elapsed_seconds"]
            with (args.raw / sample["raw_file"]).open("x", encoding="utf-8") as output:
                json.dump(raw, output, indent=2)
            samples.append(sample)
            if "selection_error" in raw:
                raise RuntimeError(raw["selection_error"])
            target = min(started + args.duration, started + (index + 1) * args.interval)
            time.sleep(max(0.0, target - time.monotonic()))
    except KeyboardInterrupt:
        status = "interrupted"
    except Exception as error:
        status, failure = "failed", str(error)
    elapsed = time.monotonic() - started
    summaries = [timeline.summary(args.long_gap_ms) for timeline in timelines.values()]
    if status == "completed" and not any(item["interval_distribution"]["count"] for item in summaries):
        status = "completed_without_usable_intervals"
    known_pids = {tuple(item["pids"]) for item in pid_observations if item["pids"]}
    pid_changes = [{"sample": after["sample"], "from": before["pids"], "to": after["pids"]}
                   for before, after in zip(pid_observations, pid_observations[1:])
                   if before["pids"] != after["pids"]]
    temperatures = [sample["battery_temperature_c"] for sample in samples if sample["battery_temperature_c"] is not None]
    report = {"schema": 1, "status": status, "error": failure,
              "measurement": "upper_SurfaceFlinger_actual_present_timestamps_only",
              "started_utc": started_utc, "finished_utc": utc(), "elapsed_seconds": elapsed,
              "serial": args.serial, "package": args.package, "adb": args.adb,
              "requested_duration_seconds": args.duration, "requested_interval_seconds": args.interval,
              "raw_samples_directory": str(args.raw), "sample_count": len(samples),
              "pid_continuity": {"observations": pid_observations,
                  "observed_unchanged_nonempty_pids": len(known_pids) == 1 and all(item["pids"] for item in pid_observations),
                  "changes_or_query_availability_transitions": pid_changes,
                  "absent_samples": sum(item["pids"] == [] for item in pid_observations),
                  "query_failure_samples": sum(item["pids"] is None for item in pid_observations),
                  "note": "Sampled continuity only; no claim about events between PID queries."},
              "battery_temperature_c": {"min": min(temperatures), "max": max(temperatures)} if temperatures else None,
              "sysfs_discovery": sysfs_discovery,
              "timelines": summaries, "samples": samples,
              "limits": ["Presentation timestamps are not guest simulation or lower UI FPS.",
                         "Each series discards its first rolling snapshot as historical baseline.",
                         "Pairs are deduplicated within overlapping observations; non-overlap gaps are coverage holes, not frame stalls.",
                         "Default five-second polling usually leaves holes in SurfaceFlinger's short rolling history; percentiles describe sampled intervals only.",
                         "Reported period belongs to the snapshot; a mode-switch window can contain intervals from both modes.",
                         "No-new-frame observations can include intentional pause/background/layer inactivity; they are not diagnosed crashes."]}
    # This path was exclusively reserved by this run; historical outputs are never replaced.
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{status}: {len(samples)} samples; {args.output}")
    return 0 if status == "completed" else 1


if __name__ == "__main__":
    sys.exit(main())
