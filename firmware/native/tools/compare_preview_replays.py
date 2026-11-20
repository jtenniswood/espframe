#!/usr/bin/env python3
"""Capture and compare the two direct synthetic runs on a Guition V1."""

import argparse
import json
from pathlib import Path
from time import monotonic, sleep
from urllib.error import URLError
from urllib.request import urlopen


def read_json(origin, path):
    with urlopen(origin + path, timeout=2) as response:
        return json.load(response)


def capture(frame, replay, timeout):
    profile = read_json(replay, "/replay-profile")
    deadline = monotonic() + timeout
    while monotonic() < deadline:
        try:
            benchmark = read_json(frame, "/api/trial/benchmark")
        except (URLError, TimeoutError):
            sleep(0.5)
            continue
        if benchmark.get("replay_origin") != replay or type(benchmark.get("direct_replay_lookahead")) is not bool:
            raise ValueError("The frame is not running a direct synthetic trial for this replay server")
        if benchmark.get("settled") == benchmark.get("target") == 48:
            break
        sleep(0.5)
    else:
        raise ValueError("The frame did not finish 48 rapid swipes before the capture timeout")

    preparation = read_json(frame, "/api/trial/prepare")
    status = read_json(frame, "/api/trial")
    if benchmark != read_json(frame, "/api/trial/benchmark"):
        raise ValueError("The benchmark changed during capture")
    if profile != read_json(replay, "/replay-profile"):
        raise ValueError("The replay server restarted during capture")
    return {"frame": frame, "profile": profile, "benchmark": benchmark, "preparation": preparation, "status": status}


def validate(run, *, lookahead):
    benchmark = run["benchmark"]
    if (benchmark.get("direct_replay_lookahead") is not lookahead or benchmark["scripted"] is not True
            or benchmark["source"] != "immich" or benchmark["scenario"] != "rapid"):
        raise ValueError("Expected the control capture first and the lookahead capture second")
    if not (benchmark["target"] == benchmark["started"] == benchmark["settled"] == benchmark["first_motion"]["count"] == 48):
        raise ValueError("Each capture must contain all 48 settled swipes and first-motion samples")
    if benchmark["wrong_slide"]:
        raise ValueError("A swipe settled on the wrong slide")
    prep = run["preparation"]
    if prep["prepared_transfer"]["count"]:
        raise ValueError("The capture includes requests to the preparation service")
    if not lookahead and (prep["prefetch_transfer"]["count"] or prep["prefetch_wait"]["count"]):
        raise ValueError("The control capture includes preview lookahead")


def sample(timing, limit):
    count = timing["count"]
    if not count:
        return "no samples"
    over = timing[limit]
    return f"{over}/{count} ({over / count:.1%}), worst {timing['worst_us'] / 1000:.1f} ms"


def prepared_pairs(run):
    slides = run["preparation"].get("new_slides")
    if not isinstance(slides, dict):
        raise ValueError("Both companion prefetch captures must report newly prepared slides")
    paired, single = slides.get("paired"), slides.get("single")
    if any(type(value) is not int or value < 0 for value in (paired, single)) or paired + single == 0:
        raise ValueError("Both companion prefetch captures must report newly prepared slides")
    return f"{paired}/{paired + single}"


def prefetch_summary(run):
    stages = run["preparation"]
    transfers, waits = stages["prefetch_transfer"], stages["prefetch_wait"]
    used = f"{waits['count']} of {transfers['count']} completed attempts"
    mean = f"{waits['total_us'] / waits['count'] / 1000:.1f} ms" if waits["count"] else "no samples"
    return used, mean


def compare(control, enabled, *, companion_prefetch=False):
    validate(control, lookahead=companion_prefetch)
    validate(enabled, lookahead=True)
    before, after = control["benchmark"], enabled["benchmark"]
    if companion_prefetch and (before.get("direct_replay_companion_prefetch") is not False
                               or after.get("direct_replay_companion_prefetch") is not True):
        raise ValueError("Expected companion prefetch disabled in the control capture and enabled in the candidate")
    status_before, status_after = control["status"], enabled["status"]
    if (control["profile"]["sha256"] != enabled["profile"]["sha256"]
            or before["replay_origin"] != after["replay_origin"]):
        raise ValueError("Both captures must use the same replay origin and synthetic preview dataset")
    if control["profile"]["run"] == enabled["profile"]["run"]:
        raise ValueError("Restart the replay server between captures to start at the same photo")
    if control["frame"] != enabled["frame"]:
        raise ValueError("Measure both variants on the same frame")
    if any(before[key] != after[key] for key in ("frame_period_us", "drag_gap_limit_us", "refill_limit_us")):
        raise ValueError("The captures use different display or timing thresholds")
    if any(status_before[key] != status_after[key] for key in ("cache_slots", "wifi_primary_channel", "wifi_power_save")):
        raise ValueError("The captures use different cache sizes, Wi-Fi channels or Wi-Fi power settings")

    rows = [
        ("Photos ready at swipe start", f"{48 - before['cold_starts']}/48", f"{48 - after['cold_starts']}/48"),
        ("Cold swipes that returned", before["cold_returns"], after["cold_returns"]),
        ("First motion over 50 ms", sample(before["first_motion"], "over_50_ms"), sample(after["first_motion"], "over_50_ms")),
        (f"Drag gaps over {before['drag_gap_limit_us'] / 1000:.1f} ms",
         sample(before["drag_gaps"], "over_limit"), sample(after["drag_gaps"], "over_limit")),
        (f"Refills over {before['refill_limit_us'] / 1000:.0f} ms",
         sample(before["refill"], "over_limit"), sample(after["refill"], "over_limit")),
        ("Wi-Fi RSSI", f"{status_before['wifi_rssi_dbm']} dBm", f"{status_after['wifi_rssi_dbm']} dBm"),
    ]
    gap_ms = before["drag_gap_limit_us"] / 1000
    for key, label, limit in (
        ("quiet_cache_hit_motion", "Cached first motion over 50 ms, idle worker", "over_50_ms"),
        ("busy_cache_hit_motion", "Cached first motion over 50 ms, preparing", "over_50_ms"),
        ("quiet_drag_gaps", f"Drag gaps over {gap_ms:.1f} ms, idle worker", "over_limit"),
        ("busy_drag_gaps", f"Drag gaps over {gap_ms:.1f} ms, preparing", "over_limit"),
    ):
        if key in before and key in after:
            rows.append((label, sample(before[key], limit), sample(after[key], limit)))
    before_used, before_wait = prefetch_summary(control) if companion_prefetch else ("disabled", "disabled")
    after_used, after_wait = prefetch_summary(enabled)
    if companion_prefetch:
        rows.insert(2, ("New slides rendered as portrait pairs", prepared_pairs(control), prepared_pairs(enabled)))
    rows.append(("Lookahead previews used", before_used, after_used))
    if after_wait != "no samples" or (companion_prefetch and before_wait != "no samples"):
        rows.append(("Mean wait for staged previews", before_wait, after_wait))
    candidate = "Companion prefetch" if companion_prefetch else "Lookahead"
    return "\n".join([
        f"| Metric | Control | {candidate} |",
        "| --- | --- | --- |",
        *(f"| {label} | {baseline} | {candidate} |" for label, baseline, candidate in rows),
    ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    collect = actions.add_parser("capture", help="Wait for the trial and save its reports")
    collect.add_argument("--frame", required=True, help="HTTP origin of the V1 trial")
    collect.add_argument("--replay", required=True, help="HTTP origin of the synthetic replay server")
    collect.add_argument("--timeout", type=int, default=120, help="Seconds to wait for the scripted run")
    collect.add_argument("output", type=Path)
    summarize = actions.add_parser("compare", help="Compare two direct preview captures")
    summarize.add_argument("control", type=Path)
    summarize.add_argument("candidate", type=Path)
    summarize.add_argument("--companion-prefetch", action="store_true",
                           help="Compare companion prefetch with primary lookahead enabled in both runs")
    args = parser.parse_args()
    try:
        if args.action == "capture":
            result = capture(args.frame.rstrip("/"), args.replay.rstrip("/"), args.timeout)
            with args.output.open("x") as output:
                json.dump(result, output, indent=2)
                output.write("\n")
        else:
            print(compare(json.loads(args.control.read_text()), json.loads(args.candidate.read_text()),
                          companion_prefetch=args.companion_prefetch))
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
