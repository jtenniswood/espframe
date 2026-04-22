# Single source of truth for timezone data used by both C++ (sun_calc.h), YAML
# (time.yaml), and the web UI. Each entry is:
# (iana_key, gmt_label, lat, lon, posix_tz).
# To add or remove a timezone, edit this list only — both the C++ TZ_DATA[]
# array and the YAML select options are generated from it.

import re

TIMEZONES = [
    ("Pacific/Midway",                  "GMT-11",     28.21, -177.38, "SST11"),
    ("Pacific/Pago_Pago",               "GMT-11",    -14.27, -170.70, "SST11"),
    ("Pacific/Honolulu",                "GMT-10",     21.31, -157.86, "HST10"),
    ("America/Adak",                    "GMT-10",     51.88, -176.66, "HST10HDT,M3.2.0,M11.1.0"),
    ("America/Anchorage",               "GMT-9",      61.22, -149.90, "AKST9AKDT,M3.2.0,M11.1.0"),
    ("America/Juneau",                  "GMT-9",      58.30, -134.42, "AKST9AKDT,M3.2.0,M11.1.0"),
    ("America/Los_Angeles",             "GMT-8",      34.05, -118.24, "PST8PDT,M3.2.0,M11.1.0"),
    ("America/Vancouver",               "GMT-8",      49.28, -123.12, "PST8PDT,M3.2.0,M11.1.0"),
    ("America/Tijuana",                 "GMT-8",      32.51, -117.04, "PST8PDT,M3.2.0,M11.1.0"),
    ("America/Denver",                  "GMT-7",      39.74, -104.98, "MST7MDT,M3.2.0,M11.1.0"),
    ("America/Phoenix",                 "GMT-7",      33.45, -112.07, "MST7"),
    ("America/Edmonton",                "GMT-7",      53.55, -113.49, "MST7MDT,M3.2.0,M11.1.0"),
    ("America/Boise",                   "GMT-7",      43.62, -116.21, "MST7MDT,M3.2.0,M11.1.0"),
    ("America/Chicago",                 "GMT-6",      41.88,  -87.63, "CST6CDT,M3.2.0,M11.1.0"),
    ("America/Mexico_City",             "GMT-6",      19.43,  -99.13, "CST6"),
    ("America/Winnipeg",                "GMT-6",      49.90,  -97.14, "CST6CDT,M3.2.0,M11.1.0"),
    ("America/Guatemala",               "GMT-6",      14.63,  -90.51, "CST6"),
    ("America/Costa_Rica",              "GMT-6",       9.93,  -84.08, "CST6"),
    ("America/New_York",                "GMT-5",      40.71,  -74.01, "EST5EDT,M3.2.0,M11.1.0"),
    ("America/Toronto",                 "GMT-5",      43.65,  -79.38, "EST5EDT,M3.2.0,M11.1.0"),
    ("America/Detroit",                 "GMT-5",      42.33,  -83.05, "EST5EDT,M3.2.0,M11.1.0"),
    ("America/Havana",                  "GMT-5",      23.11,  -82.37, "CST5CDT,M3.2.0/0,M11.1.0/1"),
    ("America/Bogota",                  "GMT-5",       4.71,  -74.07, "<-05>5"),
    ("America/Lima",                    "GMT-5",     -12.05,  -77.04, "<-05>5"),
    ("America/Jamaica",                 "GMT-5",      18.11,  -77.30, "EST5"),
    ("America/Panama",                  "GMT-5",       8.98,  -79.52, "EST5"),
    ("America/Halifax",                 "GMT-4",      44.65,  -63.57, "AST4ADT,M3.2.0,M11.1.0"),
    ("America/Caracas",                 "GMT-4",      10.49,  -66.88, "<-04>4"),
    ("America/Santiago",                "GMT-4",     -33.45,  -70.67, "<-04>4<-03>,M9.1.6/24,M4.1.6/24"),
    ("America/La_Paz",                  "GMT-4",     -16.50,  -68.15, "<-04>4"),
    ("America/Manaus",                  "GMT-4",      -3.12,  -60.02, "<-04>4"),
    ("America/Barbados",                "GMT-4",      13.10,  -59.61, "AST4"),
    ("America/Puerto_Rico",             "GMT-4",      18.47,  -66.11, "AST4"),
    ("America/Santo_Domingo",           "GMT-4",      18.49,  -69.93, "AST4"),
    ("America/St_Johns",                "GMT-3:30",   47.56,  -52.71, "NST3:30NDT,M3.2.0,M11.1.0"),
    ("America/Sao_Paulo",               "GMT-3",     -23.55,  -46.63, "<-03>3"),
    ("America/Argentina/Buenos_Aires",  "GMT-3",     -34.60,  -58.38, "<-03>3"),
    ("America/Montevideo",              "GMT-3",     -34.88,  -56.16, "<-03>3"),
    ("America/Paramaribo",              "GMT-3",       5.85,  -55.17, "<-03>3"),
    ("Atlantic/South_Georgia",          "GMT-2",     -54.28,  -36.51, "<-02>2"),
    ("Atlantic/Azores",                 "GMT-1",      38.72,  -27.22, "<-01>1<+00>,M3.5.0/0,M10.5.0/1"),
    ("Atlantic/Cape_Verde",             "GMT-1",      14.93,  -23.51, "<-01>1"),
    ("UTC",                             "GMT+0",      51.51,   -0.13, "UTC0"),
    ("Europe/London",                   "GMT+0",      51.51,   -0.13, "GMT0BST,M3.5.0/1,M10.5.0"),
    ("Europe/Dublin",                   "GMT+0",      53.35,   -6.26, "IST-1GMT0,M10.5.0,M3.5.0/1"),
    ("Europe/Lisbon",                   "GMT+0",      38.72,   -9.14, "WET0WEST,M3.5.0/1,M10.5.0"),
    # Morocco temporarily switches to GMT+0 during Ramadan. That yearly,
    # religion-calendar rule is not representable by this compact POSIX string.
    ("Africa/Casablanca",               "GMT+1",      33.57,   -7.59, "<+01>-1"),
    ("Africa/Accra",                    "GMT+0",       5.56,   -0.19, "GMT0"),
    ("Atlantic/Reykjavik",              "GMT+0",      64.15,  -21.94, "GMT0"),
    ("Europe/Paris",                    "GMT+1",      48.86,    2.35, "CET-1CEST,M3.5.0,M10.5.0/3"),
    ("Europe/Berlin",                   "GMT+1",      52.52,   13.40, "CET-1CEST,M3.5.0,M10.5.0/3"),
    ("Europe/Rome",                     "GMT+1",      41.90,   12.50, "CET-1CEST,M3.5.0,M10.5.0/3"),
    ("Europe/Madrid",                   "GMT+1",      40.42,   -3.70, "CET-1CEST,M3.5.0,M10.5.0/3"),
    ("Europe/Amsterdam",                "GMT+1",      52.37,    4.90, "CET-1CEST,M3.5.0,M10.5.0/3"),
    ("Europe/Brussels",                 "GMT+1",      50.85,    4.35, "CET-1CEST,M3.5.0,M10.5.0/3"),
    ("Europe/Vienna",                   "GMT+1",      48.21,   16.37, "CET-1CEST,M3.5.0,M10.5.0/3"),
    ("Europe/Zurich",                   "GMT+1",      47.38,    8.54, "CET-1CEST,M3.5.0,M10.5.0/3"),
    ("Europe/Stockholm",                "GMT+1",      59.33,   18.07, "CET-1CEST,M3.5.0,M10.5.0/3"),
    ("Europe/Oslo",                     "GMT+1",      59.91,   10.75, "CET-1CEST,M3.5.0,M10.5.0/3"),
    ("Europe/Copenhagen",               "GMT+1",      55.68,   12.57, "CET-1CEST,M3.5.0,M10.5.0/3"),
    ("Europe/Warsaw",                   "GMT+1",      52.23,   21.01, "CET-1CEST,M3.5.0,M10.5.0/3"),
    ("Europe/Prague",                   "GMT+1",      50.08,   14.44, "CET-1CEST,M3.5.0,M10.5.0/3"),
    ("Europe/Budapest",                 "GMT+1",      47.50,   19.04, "CET-1CEST,M3.5.0,M10.5.0/3"),
    ("Europe/Belgrade",                 "GMT+1",      44.79,   20.47, "CET-1CEST,M3.5.0,M10.5.0/3"),
    ("Africa/Lagos",                    "GMT+1",       6.45,    3.39, "WAT-1"),
    ("Africa/Tunis",                    "GMT+1",      36.81,   10.17, "CET-1"),
    ("Africa/Cairo",                    "GMT+2",      30.04,   31.24, "EET-2EEST,M4.5.5/0,M10.5.4/24"),
    ("Europe/Athens",                   "GMT+2",      37.98,   23.73, "EET-2EEST,M3.5.0/3,M10.5.0/4"),
    ("Europe/Bucharest",                "GMT+2",      44.43,   26.10, "EET-2EEST,M3.5.0/3,M10.5.0/4"),
    ("Europe/Helsinki",                 "GMT+2",      60.17,   24.94, "EET-2EEST,M3.5.0/3,M10.5.0/4"),
    ("Europe/Kyiv",                     "GMT+2",      50.45,   30.52, "EET-2EEST,M3.5.0/3,M10.5.0/4"),
    ("Europe/Istanbul",                 "GMT+3",      41.01,   28.98, "<+03>-3"),
    ("Africa/Johannesburg",             "GMT+2",     -26.20,   28.05, "SAST-2"),
    ("Africa/Nairobi",                  "GMT+3",      -1.29,   36.82, "EAT-3"),
    ("Asia/Jerusalem",                  "GMT+2",      31.77,   35.22, "IST-2IDT,M3.4.4/26,M10.5.0"),
    ("Asia/Amman",                      "GMT+3",      31.95,   35.93, "<+03>-3"),
    ("Asia/Beirut",                     "GMT+2",      33.89,   35.50, "EET-2EEST,M3.5.0/0,M10.5.0/0"),
    ("Europe/Moscow",                   "GMT+3",      55.76,   37.62, "MSK-3"),
    ("Asia/Baghdad",                    "GMT+3",      33.31,   44.37, "<+03>-3"),
    ("Asia/Riyadh",                     "GMT+3",      24.69,   46.72, "<+03>-3"),
    ("Asia/Kuwait",                     "GMT+3",      29.38,   47.98, "<+03>-3"),
    ("Asia/Qatar",                      "GMT+3",      25.29,   51.53, "<+03>-3"),
    ("Africa/Addis_Ababa",              "GMT+3",       9.01,   38.75, "EAT-3"),
    ("Asia/Tehran",                     "GMT+3:30",   35.69,   51.39, "<+0330>-3:30"),
    ("Asia/Dubai",                      "GMT+4",      25.20,   55.27, "<+04>-4"),
    ("Asia/Muscat",                     "GMT+4",      23.59,   58.54, "<+04>-4"),
    ("Asia/Baku",                       "GMT+4",      40.41,   49.87, "<+04>-4"),
    ("Asia/Tbilisi",                    "GMT+4",      41.72,   44.79, "<+04>-4"),
    ("Indian/Mauritius",                "GMT+4",     -20.16,   57.50, "<+04>-4"),
    ("Asia/Kabul",                      "GMT+4:30",   34.53,   69.17, "<+0430>-4:30"),
    ("Asia/Karachi",                    "GMT+5",      24.86,   67.01, "PKT-5"),
    ("Asia/Tashkent",                   "GMT+5",      41.30,   69.28, "<+05>-5"),
    ("Asia/Yekaterinburg",              "GMT+5",      56.84,   60.60, "<+05>-5"),
    ("Asia/Kolkata",                    "GMT+5:30",   28.61,   77.21, "IST-5:30"),
    ("Asia/Colombo",                    "GMT+5:30",    6.93,   79.84, "<+0530>-5:30"),
    ("Asia/Kathmandu",                  "GMT+5:45",   27.72,   85.32, "<+0545>-5:45"),
    ("Asia/Dhaka",                      "GMT+6",      23.81,   90.41, "<+06>-6"),
    ("Asia/Almaty",                     "GMT+5",      43.24,   76.95, "<+05>-5"),
    ("Asia/Rangoon",                    "GMT+6:30",   16.87,   96.20, "<+0630>-6:30"),
    ("Asia/Bangkok",                    "GMT+7",      13.76,  100.50, "<+07>-7"),
    ("Asia/Jakarta",                    "GMT+7",      -6.21,  106.85, "WIB-7"),
    ("Asia/Ho_Chi_Minh",                "GMT+7",      10.82,  106.63, "<+07>-7"),
    ("Asia/Singapore",                  "GMT+8",       1.35,  103.82, "<+08>-8"),
    ("Asia/Kuala_Lumpur",               "GMT+8",       3.14,  101.69, "<+08>-8"),
    ("Asia/Shanghai",                   "GMT+8",      31.23,  121.47, "CST-8"),
    ("Asia/Hong_Kong",                  "GMT+8",      22.32,  114.17, "HKT-8"),
    ("Asia/Taipei",                     "GMT+8",      25.03,  121.57, "CST-8"),
    ("Asia/Manila",                     "GMT+8",      14.60,  120.98, "PST-8"),
    ("Australia/Perth",                 "GMT+8",     -31.95,  115.86, "AWST-8"),
    ("Asia/Tokyo",                      "GMT+9",      35.68,  139.69, "JST-9"),
    ("Asia/Seoul",                      "GMT+9",      37.57,  126.98, "KST-9"),
    ("Asia/Pyongyang",                  "GMT+9",      39.02,  125.75, "KST-9"),
    ("Australia/Adelaide",              "GMT+9:30",  -34.93,  138.60, "ACST-9:30ACDT,M10.1.0,M4.1.0/3"),
    ("Australia/Darwin",                "GMT+9:30",  -12.46,  130.84, "ACST-9:30"),
    ("Australia/Sydney",                "GMT+10",    -33.87,  151.21, "AEST-10AEDT,M10.1.0,M4.1.0/3"),
    ("Australia/Melbourne",             "GMT+10",    -37.81,  144.96, "AEST-10AEDT,M10.1.0,M4.1.0/3"),
    ("Australia/Brisbane",              "GMT+10",    -27.47,  153.03, "AEST-10"),
    ("Australia/Hobart",                "GMT+10",    -42.88,  147.33, "AEST-10AEDT,M10.1.0,M4.1.0/3"),
    ("Pacific/Guam",                    "GMT+10",     13.44,  144.79, "ChST-10"),
    ("Pacific/Port_Moresby",            "GMT+10",     -6.31,  147.17, "<+10>-10"),
    ("Asia/Vladivostok",                "GMT+10",     43.12,  131.91, "<+10>-10"),
    ("Pacific/Noumea",                  "GMT+11",    -22.28,  166.46, "<+11>-11"),
    ("Pacific/Norfolk",                 "GMT+11",    -29.05,  167.96, "<+11>-11<+12>,M10.1.0,M4.1.0/3"),
    ("Asia/Magadan",                    "GMT+11",     59.56,  150.80, "<+11>-11"),
    ("Pacific/Auckland",                "GMT+12",    -36.85,  174.76, "NZST-12NZDT,M9.5.0,M4.1.0/3"),
    ("Pacific/Fiji",                    "GMT+12",    -18.14,  178.44, "<+12>-12"),
    ("Pacific/Chatham",                 "GMT+12:45", -43.88, -176.46, "<+1245>-12:45<+1345>,M9.5.0/2:45,M4.1.0/3:45"),
    ("Pacific/Tongatapu",               "GMT+13",    -21.21, -175.15, "<+13>-13"),
    ("Pacific/Apia",                    "GMT+13",    -13.83, -171.76, "<+13>-13"),
    ("Pacific/Kiritimati",              "GMT+14",      1.87, -157.47, "<+14>-14"),
]


def generate_yaml_options():
    """Generate the YAML select options list."""
    return [f'{tz} ({gmt})' for tz, gmt, *_ in TIMEZONES]


def parse_gmt_label(gmt: str) -> float:
    """Parse a GMT label like GMT+5:30 into an hour offset."""
    if not gmt.startswith("GMT"):
        raise ValueError(f"Invalid GMT label: {gmt}")
    value = gmt[3:]
    if value in ("", "0", "+0", "-0"):
        return 0.0
    sign = 1.0
    if value[0] == "+":
        value = value[1:]
    elif value[0] == "-":
        sign = -1.0
        value = value[1:]
    if ":" in value:
        hours, minutes = value.split(":", 1)
        return sign * (int(hours) + int(minutes) / 60.0)
    return sign * float(value)


def format_gmt_offset(offset: float) -> str:
    """Format an hour offset as a GMT label."""
    if abs(offset) < 0.0001:
        return "GMT+0"
    sign = "+" if offset >= 0 else "-"
    total_minutes = int(round(abs(offset) * 60))
    hours, minutes = divmod(total_minutes, 60)
    if minutes:
        return f"GMT{sign}{hours}:{minutes:02d}"
    return f"GMT{sign}{hours}"


def _skip_posix_tz_name(posix: str, index: int) -> int:
    if index >= len(posix):
        raise ValueError("Missing timezone name")
    if posix[index] == "<":
        end = posix.find(">", index + 1)
        if end == -1:
            raise ValueError(f"Unterminated POSIX timezone name: {posix}")
        return end + 1
    match = re.match(r"[A-Za-z]{3,}", posix[index:])
    if not match:
        raise ValueError(f"Invalid POSIX timezone name: {posix}")
    return index + match.end()


def _parse_posix_offset(posix: str, index: int) -> tuple[float, int]:
    match = re.match(r"([+-]?)(\d{1,2})(?::(\d{1,2}))?(?::(\d{1,2}))?", posix[index:])
    if not match:
        raise ValueError(f"Invalid POSIX timezone offset: {posix}")
    sign = -1.0 if match.group(1) == "-" else 1.0
    hours = int(match.group(2))
    minutes = int(match.group(3) or "0")
    seconds = int(match.group(4) or "0")
    posix_offset = sign * (hours + minutes / 60.0 + seconds / 3600.0)
    # POSIX offsets use the opposite sign to conventional UTC/GMT offsets.
    return -posix_offset, index + match.end()


def posix_conventional_offsets(posix: str) -> list[float]:
    """Return the conventional UTC offsets represented by a POSIX TZ string."""
    index = _skip_posix_tz_name(posix, 0)
    standard_offset, index = _parse_posix_offset(posix, index)
    offsets = [standard_offset]
    if index >= len(posix) or posix[index] == ",":
        return offsets
    index = _skip_posix_tz_name(posix, index)
    if index < len(posix) and posix[index] != ",":
        daylight_offset, _ = _parse_posix_offset(posix, index)
    else:
        daylight_offset = standard_offset + 1.0
    offsets.append(daylight_offset)
    return sorted(set(offsets))


def web_timezone_label(tz: str, gmt: str, posix: str) -> str:
    """Generate a clearer web-only display label without changing stored values."""
    option = f"{tz} ({gmt})"
    offsets = posix_conventional_offsets(posix)
    if len(offsets) < 2:
        return option
    base_offset = parse_gmt_label(gmt)
    daylight_offset = max(offsets)
    if abs(daylight_offset - base_offset) < 0.0001:
        return option
    return f"{tz} ({gmt}; daylight {format_gmt_offset(daylight_offset)})"


def generate_web_timezone_labels():
    """Generate option-value to display-label mapping for the web UI."""
    return {
        f"{tz} ({gmt})": web_timezone_label(tz, gmt, posix)
        for tz, gmt, _lat, _lon, posix in TIMEZONES
    }


def generate_cpp_tz_data():
    """Generate the C++ TZ_DATA[] array body."""
    lines = []
    for tz, _gmt, lat, lon, posix in TIMEZONES:
        lines.append(f'  {{"{tz}", {lat:>8.2f}f, {lon:>8.2f}f, "{posix}"}},')
    return "\n".join(lines)
