#!/usr/bin/env python3

from contextlib import redirect_stdout
from decimal import Decimal
from io import StringIO
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from report_optical_swipes import Swipe, latency_bounds, p95_bounds, read_swipes, show, video_times


class OpticalSwipeTest(unittest.TestCase):
    times = [Decimal(index * 4) for index in range(60)]
    header = "contact_frame,motion_frame,last_frame,tear"

    def test_brackets_contact_and_motion_with_adjacent_frames(self):
        self.assertEqual(latency_bounds(self.times, Swipe(10, 18, 29, "none")), (28, 36))
        self.assertEqual(latency_bounds(self.times, Swipe(10, 10, 29, "none")), (0, 4))
        self.assertEqual(latency_bounds(self.times, Swipe(10, None, 29, "unclear")), (76, Decimal("Infinity")))

    def test_uses_local_timestamps_and_detects_slow_motion_exports(self):
        uneven = list(map(Decimal, [5, 10, 14, 20, 23, 28]))
        self.assertEqual(latency_bounds(uneven, Swipe(2, 4, 5, "none")), (6, 13))
        with self.assertRaisesRegex(ValueError, "original high-speed recording"):
            latency_bounds([Decimal(value) for value in [0, 4, 8, 12, 25]], Swipe(1, 2, 4, "none"))

    def test_annotations_validate_frame_order_overlap_and_tearing(self):
        self.assertEqual(read_swipes([self.header, "1,5,10,none", "11,,59,unclear"], 60),
                         [Swipe(1, 5, 10, "none"), Swipe(11, None, 59, "unclear")])
        for invalid, error in (
            (["0,5,10,none"], "inside the video"),
            (["5,4,10,none"], "inside the video"),
            (["5,8,60,none"], "inside the video"),
            (["5,8,9,none", "9,12,15,none"], "nonoverlapping"),
            (["5,8,9,maybe"], "Tear must be"),
            (["5,8,9,none,extra"], "four fields"),
            (["5,8,9"], "four fields"),
            ([], "at least one swipe"),
        ):
            with self.subTest(invalid=invalid), self.assertRaisesRegex(ValueError, error):
                read_swipes([self.header, *invalid], 60)

    def test_reports_missed_swipes_and_uncertainty_at_the_target(self):
        swipes = [Swipe(1, 10, 19, "none"), Swipe(20, 32, 36, "visible"), Swipe(37, None, 59, "unclear")]
        bounds = [latency_bounds(self.times, swipe) for swipe in swipes]
        report = StringIO()
        with redirect_stdout(report):
            show(swipes, bounds)
        self.assertIn("Measured P95 bounds (n=3): 88.00 ms to unbounded", report.getvalue())
        self.assertIn("50 ms target: 1 below, 1 at or above, 1 unresolved", report.getvalue())
        self.assertIn("Tearing: 1 visible, 1 unclear", report.getvalue())
        self.assertEqual(p95_bounds([(Decimal(20), Decimal(30))] * 19 + [bounds[-1]]), (20, 30))

    def test_reads_ffprobe_frame_timestamps_without_reordering(self):
        with tempfile.TemporaryDirectory() as scratch:
            video = Path(scratch) / "High Speed.mov"
            video.touch()
            with patch("report_optical_swipes.subprocess.run") as probe:
                payload = {"frames": [{"best_effort_timestamp_time": "0.031250"},
                                      {"best_effort_timestamp_time": "0.035417"}]}
                probe.return_value.stdout = json.dumps(payload)
                self.assertEqual(video_times(video), [Decimal("31.250"), Decimal("35.417")])
                self.assertEqual(probe.call_args.args[0][-1], str(video.resolve()))
                payload["frames"].reverse()
                probe.return_value.stdout = json.dumps(payload)
                with self.assertRaisesRegex(ValueError, "increasing frame timestamps"):
                    video_times(video)
                probe.side_effect = subprocess.CalledProcessError(1, "ffprobe", stderr="Invalid data")
                with self.assertRaisesRegex(ValueError, "Invalid data"):
                    video_times(video)


if __name__ == "__main__":
    unittest.main()
