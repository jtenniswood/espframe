#!/usr/bin/env python3
"""Measure touch-to-visible motion from frame-by-frame camera observations."""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from hashlib import sha256
import json
from pathlib import Path
import subprocess


TARGET_MS = Decimal(50)
MAX_FRAME_GAP_MS = Decimal(9)
FIELDS = ["contact_frame", "motion_frame", "last_frame", "tear"]


@dataclass(frozen=True)
class Swipe:
    contact: int
    motion: int | None
    last: int
    tear: str


def video_times(video):
    if not video.is_file():
        raise ValueError(f"Video file does not exist: {video}")
    try:
        result = subprocess.run(
            ["ffprobe", "-v", "error", "-select_streams", "V:0", "-show_frames",
             "-show_entries", "frame=best_effort_timestamp_time", "-of", "json", str(video.resolve())],
            check=True, capture_output=True, text=True,
        )
    except FileNotFoundError as error:
        raise ValueError("Install FFmpeg and put ffprobe on PATH first.") from error
    except subprocess.CalledProcessError as error:
        raise ValueError(f"ffprobe could not read the video. {error.stderr.strip()}") from error
    try:
        times = [Decimal(frame["best_effort_timestamp_time"]) * 1000
                 for frame in json.loads(result.stdout)["frames"]]
    except (KeyError, TypeError, ValueError, InvalidOperation) as error:
        raise ValueError("The video has unreadable frame timestamps.") from error
    if len(times) < 2 or any(not time.is_finite() for time in times) or any(
        later <= earlier for earlier, later in zip(times, times[1:])
    ):
        raise ValueError("The video needs strictly increasing frame timestamps.")
    return times


def read_swipes(lines, frame_count):
    rows = csv.DictReader(lines, strict=True)
    if rows.fieldnames != FIELDS:
        raise ValueError(f"Expected CSV header: {','.join(FIELDS)}")
    swipes = []
    for row in rows:
        try:
            if None in row or None in row.values():
                raise ValueError("Expected four fields")
            contact = int(row["contact_frame"])
            motion = int(row["motion_frame"]) if row["motion_frame"] else None
            last = int(row["last_frame"])
            if not 1 <= contact <= last < frame_count or (motion is not None and not contact <= motion <= last):
                raise ValueError("Expected contact, motion (if seen) and last frames in order inside the video")
            if swipes and contact <= swipes[-1].last:
                raise ValueError("Swipe windows must be nonoverlapping and in video order")
            if row["tear"] not in ("none", "visible", "unclear"):
                raise ValueError("Tear must be none, visible, or unclear")
        except ValueError as error:
            raise ValueError(f"CSV line {rows.line_num}: {error}") from error
        swipes.append(Swipe(contact, motion, last, row["tear"]))
    if not swipes:
        raise ValueError("Mark at least one swipe.")
    return swipes


def latency_bounds(times, swipe):
    # Contact and visible motion both happened between an observation and
    # the preceding frame. Treat an unobserved response as right-censored.
    if any(times[frame] - times[frame - 1] > MAX_FRAME_GAP_MS
           for frame in range(swipe.contact, swipe.last + 1)):
        raise ValueError(f"Swipe at frame {swipe.contact} includes a camera gap over {MAX_FRAME_GAP_MS} ms. "
                         "Use the original high-speed recording.")
    if swipe.motion is None:
        return times[swipe.last] - times[swipe.contact], Decimal("Infinity")
    return (max(Decimal(0), times[swipe.motion - 1] - times[swipe.contact]),
            times[swipe.motion] - times[swipe.contact - 1])


def p95_bounds(bounds):
    index = (95 * len(bounds) + 99) // 100 - 1
    lower, upper = zip(*bounds)
    return sorted(lower)[index], sorted(upper)[index]


def show(swipes, bounds):
    def ms(value):
        return f"{value:.2f} ms" if value.is_finite() else "unbounded"

    print("Physical touch to first visible photo motion")
    for swipe, (lower, upper) in zip(swipes, bounds):
        print(f"  Contact at frame {swipe.contact}: {ms(lower)} to {ms(upper)}, tearing {swipe.tear}")
    lower, upper = p95_bounds(bounds)
    under = sum(upper < TARGET_MS for _, upper in bounds)
    over = sum(lower >= TARGET_MS for lower, _ in bounds)
    unclear = len(bounds) - under - over
    visible_tears = sum(swipe.tear == "visible" for swipe in swipes)
    unclear_tears = sum(swipe.tear == "unclear" for swipe in swipes)
    print(f"Measured P95 bounds (n={len(bounds)}): {ms(lower)} to {ms(upper)}")
    print(f"50 ms target: {under} below, {over} at or above, {unclear} unresolved")
    print(f"Tearing: {visible_tears} visible, {unclear_tears} unclear")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("video", type=Path, help="Original high-speed video with real-time frame timestamps")
    parser.add_argument("swipes", type=Path, help="CSV of reviewed swipe windows in the video")
    args = parser.parse_args()
    try:
        times = video_times(args.video)
        with args.swipes.open(encoding="utf-8", newline="") as lines:
            swipes = read_swipes(lines, len(times))
        bounds = [latency_bounds(times, swipe) for swipe in swipes]
        for source in (args.video, args.swipes):
            digest = sha256()
            with source.open("rb") as data:
                for chunk in iter(lambda: data.read(1024 * 1024), b""):
                    digest.update(chunk)
            print(f"{source.name} SHA-256 {digest.hexdigest()}")
    except (OSError, ValueError, csv.Error) as error:
        parser.error(str(error))
    show(swipes, bounds)


if __name__ == "__main__":
    main()
