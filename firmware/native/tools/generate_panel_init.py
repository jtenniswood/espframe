"""Generate native JD9365 parameters from an existing Espframe board profile."""


from __future__ import annotations
import argparse
import ast
from pathlib import Path
import re


def display_section(profile: Path) -> str:
    match = re.search(r"^display:\n((?:^ +[^\n]*\n)+)", profile.read_text(), re.MULTILINE)
    if match is None:
        raise ValueError(f"No display configuration in {profile}")
    return match[1]


def read_custom_sequence(display: str):
    capturing = False
    for line in display.splitlines():
        if line == "    init_sequence:":
            capturing = True
            continue
        if not capturing:
            continue
        if not line.startswith("      - "):
            break
        entry = line.removeprefix("      - ")
        if match := re.fullmatch(r"delay (\d+)ms", entry):
            yield 0, None, int(match[1])
        else:
            command, parameter = ast.literal_eval(entry)
            yield command, parameter, 0


def read_original_sequence(source: Path):
    for statement in ast.parse(source.read_text()).body:
        if not isinstance(statement, ast.Expr) or not isinstance(statement.value, ast.Call):
            continue
        call = statement.value
        if not (call.args and isinstance(call.args[0], ast.Constant) and call.args[0].value == "JC8012P4A1"):
            continue
        for keyword in call.keywords:
            if keyword.arg == "initsequence":
                for command, parameter in ast.literal_eval(keyword.value):
                    yield command, parameter, 0
                return
    raise ValueError(f"No JC8012P4A1 init sequence in {source}")


def generate(profile: Path, original_model: Path | None) -> str:
    display = display_section(profile)
    fields = {
        "pixel_clock_mhz": ("pclk_frequency", "MHz"),
        "lane_rate_mbps": ("lane_bit_rate", "Mbps"),
        "hsync_pulse": ("hsync_pulse_width", ""),
        "hsync_back": ("hsync_back_porch", ""),
        "hsync_front": ("hsync_front_porch", ""),
        "vsync_pulse": ("vsync_pulse_width", ""),
        "vsync_back": ("vsync_back_porch", ""),
        "vsync_front": ("vsync_front_porch", ""),
    }
    lines = ["// Generated from the Espframe board profile at build time."]
    for name, (key, suffix) in fields.items():
        match = re.search(rf"^    {key}: (\d+){suffix}$", display, re.MULTILINE)
        if match is None:
            raise ValueError(f"No {key} in {profile}")
        lines.append(f"constexpr int {name} = {int(match[1])};")

    sequence = list(read_original_sequence(original_model) if original_model else read_custom_sequence(display))
    if not sequence:
        raise ValueError(f"No panel initialization commands in {profile}")

    # Match the DCS settings appended by ESPHome's MIPI model for these
    # profiles: RGB565, RGB element order, inversion off, awake, display on.
    sequence.extend(((0x3a, 0x55, 0), (0x36, 0, 0), (0x20, None, 0),
                     (0x11, None, 0), (0x29, None, 0), (0, None, 10)))
    lines.append("constexpr PanelCommand panel_init[]{")
    for command, parameter, delay in sequence:
        if not (0 <= command <= 255 and (parameter is None or 0 <= parameter <= 255) and 0 <= delay <= 255):
            raise ValueError(f"Invalid JD9365 command in {profile}: {(command, parameter, delay)}")
        lines.append(f"    {{0x{command:02x}, 0x{parameter or 0:02x}, {delay}, {int(parameter is not None)}}},")
    lines.extend(("};", ""))
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--original-model", type=Path)
    args = parser.parse_args()
    args.output.write_text(generate(args.profile, args.original_model), encoding="utf-8")


if __name__ == "__main__":
    main()
