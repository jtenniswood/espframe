#!/usr/bin/env python3
"""Summarize native Espframe serial captures from a paired memory test."""

import argparse
from collections import defaultdict
from pathlib import Path
import re

KEYS = ("internal_used", "internal_free", "internal_min", "internal_largest",
        "psram_used", "psram_free", "psram_min", "psram_largest")
SAMPLE = re.compile(r"native-memory: ([a-z_-]+) " + " ".join(rf"{key}=(\d+)" for key in KEYS))
GROUPS = {
    "display-ready": "Display initialized",
    "tailscale-before-start": "Before tunnel start",
    "tailscale-started": "Tunnel started",
    "tailscale-needs_login": "Waiting for browser sign-in",
    "tailscale-connected": "Tunnel connected",
    "tailscale-stopped": "Tunnel stopped",
}


def read_capture(path):
    groups = defaultdict(list)
    succeeded = set()
    connected = False
    for match in SAMPLE.finditer(path.read_text(errors="replace")):
        stage, *numbers = match.groups()
        stats = dict(zip(KEYS, map(int, numbers)))
        if stage == "tailscale-connected":
            connected = True
        elif stage in {"tailscale-stopped", "tailscale-waiting_wifi", "tailscale-connecting",
                       "tailscale-auth_failed", "tailscale-error", "wifi-disconnected"}:
            connected = False
        if stage.endswith("-http-success"):
            succeeded.add(stage)
        if stage in GROUPS:
            groups[GROUPS[stage]].append(stats)
        elif stage.startswith("photo-http-"):
            groups["LAN HTTP"].append(stats)
        elif stage.startswith("tailnet-http-"):
            groups["Tailnet HTTP"].append(stats)
        elif stage == "running":
            phase = "with tunnel" if connected else "without tunnel"
            groups[f"Periodic sample {phase}"].append(stats)
    return groups, succeeded


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path, help="serial log from the baseline LAN run")
    parser.add_argument("tailscale", type=Path, help="serial log from the Tailscale test run")
    args = parser.parse_args()
    results = [read_capture(path) for path in (args.baseline, args.tailscale)]
    for (groups, succeeded), path, expected in zip(
        results, (args.baseline, args.tailscale), ("photo-http-success", "tailnet-http-success")
    ):
        if not groups or expected not in succeeded:
            parser.error(f"{path} must include a completed HTTP exchange ({expected})")

    print("Values are bytes. Used is the maximum sampled value. Other metrics are sampled minima.")
    print("Low-water marks cover the boot up to each sample, including earlier stages.")
    print()
    print("| Build / phase | Samples | Internal used | Internal free | Internal low water | Largest internal block "
          "| PSRAM used | PSRAM free | PSRAM low water | Largest PSRAM block |")
    print("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for (groups, _), name in zip(results, ("Baseline", "Tailscale")):
        for phase, samples in groups.items():
            summary = [
                (max if key.endswith("_used") else min)(sample[key] for sample in samples)
                for key in KEYS
            ]
            print(f"| {name} / {phase} | {len(samples)} | " + " | ".join(f"{value:,}" for value in summary) + " |")


if __name__ == "__main__":
    main()
