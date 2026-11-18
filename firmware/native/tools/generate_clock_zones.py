#!/usr/bin/env python3
"""Use Espframe's timezone catalog for the native firmware and installer."""

import argparse
from html import escape
import importlib.util
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--header", type=Path)
    parser.add_argument("--options", action="store_true")
    args = parser.parse_args()
    if not args.header and not args.options:
        parser.error("choose --header or --options")

    source = Path(__file__).resolve().parents[3] / "components/espframe/timezones.py"
    spec = importlib.util.spec_from_file_location("espframe_timezones", source)
    zones = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(zones)
    labels = zones.generate_web_timezone_labels()
    if args.header:
        rows = (
            "  {" + ", ".join(map(json.dumps, (option, labels[option], posix)))
            + f", {lat:.4f}f, {lon:.4f}f" + "},"
            for option, (_, _, lat, lon, posix) in zip(labels, zones.TIMEZONES, strict=True)
        )
        args.header.write_text("\n".join(rows) + "\n", encoding="utf-8")
    if args.options:
        for option, label in labels.items():
            selected = " selected" if option == "Europe/London (GMT+0)" else ""
            print(f'<option value="{escape(option)}"{selected}>{escape(label.replace("_", " "))}</option>')


if __name__ == "__main__":
    main()
