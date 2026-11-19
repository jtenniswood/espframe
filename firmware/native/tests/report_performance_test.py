#!/usr/bin/env python3

from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from report_performance import cache_waits, distribution, read_captures, show


class ReportTest(unittest.TestCase):
    def test_capture_distinguishes_motion_and_refill(self):
        lines = """
I (300) swipe-probe: Player ready
\x1b[0;32mI (300) swipe-trace: start 1 v1 16000 11000000 10000000\x1b[0m
I (318) swipe-trace: r 1 318000 305000 3 r
I (333) swipe-trace: g 2 332000 316000 311000 2500 h -1 0
I (333) swipe-trace: f 3 1 d 332000 0
I (349) swipe-trace: f 4 1 d 348000 0
I (380) swipe-trace: f 5 1 d 380000 1
I (412) swipe-trace: f 6 1 d 412000 1
I (428) swipe-trace: f 7 2 s 428000 1
I (450) swipe-trace: g 8 450000 434000 429000 2200 m 4 1
I (500) swipe-trace: r 9 500000 490000 4 c
I (580) swipe-trace: g 10 580000 560000 548000 3200 e -1 1
I (610) swipe-trace: r 11 610000 500000 4 r
I (650) swipe-trace: g 12 650000 630000 620000 3200 m 5 1
"""
        capture, = read_captures(lines.splitlines())
        self.assertEqual(capture.gap_samples["d", "all"], [16000, 32000, 32000])
        self.assertEqual(capture.gap_samples["d", "quiet"], [16000])
        self.assertEqual(capture.gap_samples["d", "preparing"], [32000])
        self.assertEqual(cache_waits(capture), ([176000], 1))
        report = StringIO()
        with redirect_stdout(report):
            show(capture)
        self.assertIn("33.3% (1 hit, 2 miss, 1 library edge excluded)", report.getvalue())
        self.assertIn("2 over 1.5 refresh periods", report.getvalue())
        self.assertIn("1 cancelled", report.getvalue())
        self.assertIn("176.00 ms (1 unresolved)", report.getvalue())

    def test_missing_frames_and_reboots_do_not_add_fake_gaps(self):
        captures = read_captures([
            "I (0) swipe-trace: f 52 3 d 1000 0",
            "I (0) swipe-trace: start 1 v1 16000 500 250",
            "I (0) swipe-trace: f 1 1 d 1000 0",
            "I (0) swipe-trace: lost 1",
            "I (0) swipe-trace: f 3 1 d 33000 0",
            "I (0) swipe-trace: f 4 1 d 49000 0",
            "I (0) swipe-trace: start 1 v3 16500 600 300",
            "I (0) swipe-trace: f 1 1 d 1000 0",
        ])
        self.assertEqual(len(captures), 2)
        self.assertEqual(captures[0].missing, 1)
        self.assertEqual(captures[0].declared_lost, 1)
        self.assertEqual(captures[0].gap_samples["d", "all"], [16000])
        self.assertFalse(captures[1].gap_samples)

    def test_report_uses_nearest_rank_p95_and_requires_versioned_boot(self):
        self.assertIn("p95=19.00 ms", distribution(list(range(1000, 21000, 1000))))
        with self.assertRaisesRegex(ValueError, "No playback trace found"):
            read_captures(["I (0) swipe-probe: startup"])
        with self.assertRaisesRegex(ValueError, "Unsupported trace version 2"):
            read_captures(["I (0) swipe-trace: start 2 v3 16000 500 250"])


if __name__ == "__main__":
    unittest.main()
