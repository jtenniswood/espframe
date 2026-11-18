"""Extract the V3 JD9365 init sequence from the maintained device profile."""

import argparse
import ast
from pathlib import Path
import re


def read_sequence(profile: Path):
    capturing = False
    for line in profile.read_text(encoding="utf-8").splitlines():
        if line == "    init_sequence:":
            capturing = True
            continue
        if not capturing:
            continue
        if not line.startswith("      - "):
            break
        entry = line.removeprefix("      - ")
        if match := re.fullmatch(r"delay (\d+)ms", entry):
            yield 0, 0, int(match[1])
            continue
        command, parameter = ast.literal_eval(entry)
        if not (0 <= command <= 255 and 0 <= parameter <= 255):
            raise ValueError(f"Expected byte arguments in {profile}: {entry}")
        yield command, parameter, 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    sequence = list(read_sequence(args.profile))
    if not sequence or not any(command == 0x29 for command, _, _ in sequence):
        parser.error(f"Expected a complete JD9365 init sequence in {args.profile}")
    entries = "".join(
        f"    {{0x{command:02x}, 0x{parameter:02x}, {delay}}},\n"
        for command, parameter, delay in sequence
    )
    args.output.write_text(
        "// Generated from the Espframe V3 display profile at build time.\n"
        "constexpr PanelCommand panel_init[]{\n"
        f"{entries}}};\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
