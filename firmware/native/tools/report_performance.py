#!/usr/bin/env python3
"""Summarize one or more native Espframe boot traces from a USB console log."""

import argparse
from bisect import bisect_left
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
import re


ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
PHASE_NAMES = {"d": "Drag", "s": "Settle or glide", "f": "Crossfade"}


@dataclass
class Gesture:
    latched: int
    sampled: int
    contact: int
    composed: int
    cache: str
    slide: int
    preparing: bool


@dataclass
class Refill:
    completed: int
    started: int
    slide: int
    result: str


@dataclass
class Capture:
    board: str
    period_us: int
    free_psram: int
    largest_psram: int
    sequence: int = 0
    missing: int = 0
    declared_lost: int = 0
    gap_samples: dict[tuple[str, str], list[int]] = field(default_factory=lambda: defaultdict(list))
    last_frame: tuple[str, str, int, int] | None = None
    gestures: list[Gesture] = field(default_factory=list)
    refills: list[Refill] = field(default_factory=list)

    def add(self, kind, fields):
        if kind == "lost":
            self.declared_lost += int(fields[0])
            self.last_frame = None
            return

        sequence = int(fields[0])
        if sequence <= self.sequence:
            raise ValueError("Trace records are out of order")
        if sequence != self.sequence + 1:
            self.last_frame = None
            self.missing += sequence - self.sequence - 1
        self.sequence = sequence

        if kind == "f":
            epoch, phase, latched, preparing = fields[1:]
            latched, preparing = int(latched), int(preparing)
            if self.last_frame:
                old_epoch, old_phase, old_latch, old_preparing = self.last_frame
                if (epoch, phase) == (old_epoch, old_phase) and latched > old_latch:
                    gap = latched - old_latch
                    self.gap_samples[phase, "all"].append(gap)
                    if old_preparing == preparing:
                        self.gap_samples[phase, "preparing" if preparing else "quiet"].append(gap)
            self.last_frame = (epoch, phase, latched, preparing)
        elif kind == "g":
            latched, sampled, contact, composed, cache, slide, preparing = fields[1:]
            self.gestures.append(Gesture(int(latched), int(sampled), int(contact), int(composed),
                                         cache, int(slide), bool(int(preparing))))
        elif kind == "r":
            completed, started, slide, result = fields[1:]
            self.refills.append(Refill(int(completed), int(started), int(slide), result))
        else:
            raise ValueError(f"Unknown record type {kind}")


def read_captures(lines):
    captures = []
    for number, line in enumerate(lines, 1):
        _, marker, text = ANSI.sub("", line).partition("swipe-trace: ")
        if not marker or not text.strip():
            continue
        kind, *fields = text.split()
        if kind not in {"start", "f", "g", "r", "lost"}:
            continue
        try:
            if kind == "start":
                version, board, period, free, largest = fields
                if version != "1":
                    raise ValueError(f"Unsupported trace version {version}")
                captures.append(Capture(board, int(period), int(free), int(largest)))
            elif captures:
                captures[-1].add(kind, fields)
        except (IndexError, ValueError) as error:
            raise ValueError(f"Invalid trace at log line {number}. {error}") from error
    if not captures:
        raise ValueError("No playback trace found. Capture from boot with SWIPE_TRACE enabled.")
    return captures


def distribution(samples):
    if not samples:
        return "no samples"
    ordered = sorted(samples)
    count = len(ordered)
    median = ordered[(count + 1) // 2 - 1] / 1000
    p95 = ordered[(count * 95 + 99) // 100 - 1] / 1000
    return f"n={count} p50={median:.2f} ms p95={p95:.2f} ms max={ordered[-1] / 1000:.2f} ms"


def cache_waits(capture):
    ready_at = defaultdict(list)
    for refill in sorted(capture.refills, key=lambda row: row.completed):
        if refill.result == "r":
            ready_at[refill.slide].append(refill.completed)
    completed_waits = []
    unresolved = 0
    for gesture in capture.gestures:
        if gesture.cache != "m":
            continue
        times = ready_at[gesture.slide]
        index = bisect_left(times, gesture.sampled)
        if index < len(times):
            completed_waits.append(times[index] - gesture.sampled)
        else:
            unresolved += 1
    return completed_waits, unresolved


def show(capture):
    print(f"{capture.board.upper()} · {capture.period_us / 1000:.2f} ms nominal refresh period")
    print(f"PSRAM at startup: {capture.free_psram:,} bytes free, {capture.largest_psram:,} bytes in largest free block")
    lost = max(capture.missing, capture.declared_lost)
    if lost:
        print(f"Trace incomplete: at least {lost} record(s) lost. Treat all measurements as partial.")

    cache = Counter(gesture.cache for gesture in capture.gestures)
    attempts = cache["h"] + cache["m"]
    rate = f"{100 * cache['h'] / attempts:.1f}%" if attempts else "no eligible gestures"
    print(f"Ready at first motion: {rate} ({cache['h']} hit, {cache['m']} miss, {cache['e']} library edge excluded)")
    print("First movement sample to LCD handoff:")
    for label, subset in (
        ("All", capture.gestures),
        ("Cache hit, quiet", [row for row in capture.gestures if row.cache == "h" and not row.preparing]),
        ("Cache hit, preparing", [row for row in capture.gestures if row.cache == "h" and row.preparing]),
        ("Cache miss", [row for row in capture.gestures if row.cache == "m"]),
    ):
        print(f"  {label}: {distribution([row.latched - row.sampled for row in subset])}")
    print(f"First contact report to LCD handoff: {distribution([row.latched - row.contact for row in capture.gestures])}")
    print(f"First frame compose and submit: {distribution([row.composed for row in capture.gestures])}")

    print("Gaps between changed frames in the same animation:")
    for phase, label in PHASE_NAMES.items():
        for workload in ("all", "quiet", "preparing"):
            samples = capture.gap_samples.get((phase, workload))
            if samples:
                late = sum(gap * 2 > capture.period_us * 3 for gap in samples)
                print(f"  {label}, {workload}: {distribution(samples)}, {late} over 1.5 refresh periods")

    results = Counter(refill.result for refill in capture.refills)
    ready = [row.completed - row.started for row in capture.refills if row.result == "r"]
    print(f"Slide preparation to ready: {distribution(ready)} ({results['e']} retries, {results['c']} cancelled)")
    waits, unresolved = cache_waits(capture)
    print(f"Cache miss to requested slide ready: {distribution(waits)} ({unresolved} unresolved)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="USB console log captured from frame boot")
    args = parser.parse_args()
    try:
        with args.log.open(encoding="utf-8", errors="replace") as log:
            captures = read_captures(log)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    for number, capture in enumerate(captures, 1):
        if number > 1:
            print()
        show(capture)


if __name__ == "__main__":
    main()
