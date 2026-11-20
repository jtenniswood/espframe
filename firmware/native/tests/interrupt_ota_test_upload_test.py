#!/usr/bin/env python3
"""Exercise the partial upload against a local HTTP server."""

import json
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
from threading import Thread
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from interrupt_ota_test_upload import PREFIX_SIZE, verify_interruption


class TestFrame(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        self.server.reads += 1
        running = {"offset": 0x10000, "elf_sha256": "before"}
        data = json.dumps({"running": running, "ota_test_build": self.server.test_build}).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        self.server.upload = (self.path, self.headers["Content-Length"],
                              self.headers["Authorization"], self.rfile.read(PREFIX_SIZE))
        self.send_response(408)
        self.end_headers()


class InterruptedUploadTest(unittest.TestCase):
    def test_interruption_and_production_guard(self):
        with TemporaryDirectory() as directory, HTTPServer(("127.0.0.1", 0), TestFrame) as server:
            prefix = bytearray(PREFIX_SIZE)
            project = b"espframe_ota_test_v1"
            prefix[80:80 + len(project)] = project
            image = Path(directory) / "app.bin"
            image.write_bytes(prefix + b"unsent")
            origin = f"http://127.0.0.1:{server.server_port}"
            server.reads = 0
            server.upload = None
            server.test_build = False
            worker = Thread(target=server.serve_forever)
            worker.start()
            try:
                with self.assertRaisesRegex(ValueError, "OTA test baseline"):
                    verify_interruption(origin, image, "test")
                self.assertIsNone(server.upload)

                server.test_build = True
                verify_interruption(origin, image, "test")
                self.assertEqual(server.reads, 3)
                self.assertEqual(server.upload, ("/api/firmware", str(image.stat().st_size),
                                                 "Basic ZXNwZnJhbWU6dGVzdA==", prefix))
            finally:
                server.shutdown()
                worker.join()


if __name__ == "__main__":
    unittest.main()
