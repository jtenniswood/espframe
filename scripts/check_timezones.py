#!/usr/bin/env python3
"""Validate ESPFrame timezone data against Python's IANA zoneinfo database."""

from __future__ import annotations

import datetime as dt
import importlib.util
import os
from pathlib import Path
import time
from zoneinfo import ZoneInfo


ROOT = Path(__file__).resolve().parents[1]
TIMEZONES_PATH = ROOT / "components" / "espframe" / "timezones.py"
CASABLANCA = "Africa/Casablanca"
CASABLANCA_LIMITATION = "Ramadan UTC+0 transition is not representable by the compact POSIX string"


def load_timezones():
    spec = importlib.util.spec_from_file_location("espframe_timezones", TIMEZONES_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {TIMEZONES_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def posix_offset_hours(posix: str, instant: dt.datetime) -> float:
    old_tz = os.environ.get("TZ")
    os.environ["TZ"] = posix
    time.tzset()
    try:
        local = time.localtime(instant.timestamp())
        if hasattr(local, "tm_gmtoff"):
            return local.tm_gmtoff / 3600.0
        return (time.mktime(local) - time.mktime(time.gmtime(instant.timestamp()))) / 3600.0
    finally:
        if old_tz is None:
            os.environ.pop("TZ", None)
        else:
            os.environ["TZ"] = old_tz
        time.tzset()


def iana_offset_hours(tz: str, instant: dt.datetime) -> float:
    return instant.astimezone(ZoneInfo(tz)).utcoffset().total_seconds() / 3600.0


def iana_local_noon_offset_hours(tz: str, year: int, month: int, day: int) -> float:
    local_noon = dt.datetime(year, month, day, 12, 0, 0, tzinfo=ZoneInfo(tz))
    return local_noon.utcoffset().total_seconds() / 3600.0


def assert_equal(actual, expected, label: str) -> None:
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


def assert_close(actual: float, expected: float, label: str) -> None:
    if abs(actual - expected) > 0.0001:
        raise AssertionError(f"{label}: expected {expected}, got {actual}")


def main() -> int:
    module = load_timezones()
    rows = module.TIMEZONES
    row_by_tz = {tz: (gmt, posix) for tz, gmt, _lat, _lon, posix in rows}

    assert_equal(row_by_tz["Asia/Almaty"][0], "GMT+5", "Asia/Almaty label")
    assert CASABLANCA in row_by_tz, CASABLANCA_LIMITATION

    sample_instants = [
        dt.datetime(2026, 1, 15, 12, tzinfo=dt.timezone.utc),
        dt.datetime(2026, 2, 20, 12, tzinfo=dt.timezone.utc),
        dt.datetime(2026, 4, 22, 12, tzinfo=dt.timezone.utc),
        dt.datetime(2026, 7, 15, 12, tzinfo=dt.timezone.utc),
        dt.datetime(2026, 11, 15, 12, tzinfo=dt.timezone.utc),
    ]

    for tz, gmt, _lat, _lon, posix in rows:
        label_offset = module.parse_gmt_label(gmt)
        iana_offsets = {iana_offset_hours(tz, instant) for instant in sample_instants}
        if label_offset not in iana_offsets:
            raise AssertionError(f"{tz} label {gmt} is not one of the sampled IANA offsets {sorted(iana_offsets)}")

        if tz == CASABLANCA:
            continue

        for instant in sample_instants:
            assert_close(
                posix_offset_hours(posix, instant),
                iana_offset_hours(tz, instant),
                f"{tz} POSIX offset on {instant.date()}",
            )

    expected_noon_offsets = [
        ("Europe/London", 2026, 1, 15, 0.0),
        ("Europe/London", 2026, 3, 29, 1.0),
        ("Europe/London", 2026, 4, 22, 1.0),
        ("America/New_York", 2026, 7, 15, -4.0),
        ("Australia/Sydney", 2026, 1, 15, 11.0),
        ("Australia/Sydney", 2026, 7, 15, 10.0),
        ("UTC", 2026, 4, 22, 0.0),
        ("America/Phoenix", 2026, 7, 15, -7.0),
        ("Asia/Kolkata", 2026, 7, 15, 5.5),
        ("America/St_Johns", 2026, 7, 15, -2.5),
        ("Australia/Adelaide", 2026, 1, 15, 10.5),
    ]
    for tz, year, month, day, expected in expected_noon_offsets:
        assert_close(
            iana_local_noon_offset_hours(tz, year, month, day),
            expected,
            f"{tz} local noon offset on {year:04d}-{month:02d}-{day:02d}",
        )

    labels = module.generate_web_timezone_labels()
    assert "daylight GMT+1" in labels["Europe/London (GMT+0)"]
    assert "daylight GMT-4" in labels["America/New_York (GMT-5)"]
    assert labels["Asia/Almaty (GMT+5)"] == "Asia/Almaty (GMT+5)"

    print("timezone validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
