#!/usr/bin/env python3
"""Exercise the upload timeout on a frame running the native OTA test build."""

import argparse
import base64
from contextlib import closing
from getpass import getpass
from http.client import HTTPConnection, HTTPException
import json
from pathlib import Path
from urllib.parse import urlsplit


PREFIX_SIZE = 64 * 1024


def verify_interruption(origin, image, password):
    origin = urlsplit(origin)
    if (origin.scheme != "http" or not origin.hostname or origin.username is not None or origin.password is not None
            or origin.path not in ("", "/") or origin.query or origin.fragment):
        raise ValueError("Enter the HTTP origin of the OTA test frame")
    auth = "Basic " + base64.b64encode(f"espframe:{password}".encode()).decode("ascii")

    def connection():
        return HTTPConnection(origin.hostname, origin.port, timeout=10)

    def status():
        with closing(connection()) as client:
            client.request("GET", "/api/firmware", headers={"Authorization": auth})
            reply = client.getresponse()
            if reply.status != 200:
                raise ValueError(f"Firmware status returned HTTP {reply.status}")
            result = json.load(reply)
            if result.get("ota_test_build") is not True or not result.get("running"):
                raise ValueError("Connect to a frame running the native OTA test baseline")
            return result["running"]

    with image.open("rb") as file:
        length = file.seek(0, 2)
        file.seek(0)
        prefix = file.read(PREFIX_SIZE)
    project = prefix[80:112].split(b"\0", 1)[0]
    if length <= PREFIX_SIZE or project not in {f"espframe_ota_test_v{board}".encode() for board in (1, 2, 3)}:
        raise ValueError("Choose a complete app image from the native OTA test build")

    before = status()
    with closing(connection()) as client:
        client.putrequest("POST", "/api/firmware")
        client.putheader("Authorization", auth)
        client.putheader("Content-Type", "application/octet-stream")
        client.putheader("Content-Length", str(length))
        # Send enough to start staging, then let the server time out the upload.
        client.endheaders(prefix)
        reply = client.getresponse()
        if reply.status != 408:
            raise ValueError(f"Expected the interrupted upload response (HTTP 408), got HTTP {reply.status}")
        reply.read()
    if status() != before:
        raise ValueError("The running firmware changed during the interrupted upload")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frame", help="HTTP origin of the native OTA test frame")
    parser.add_argument("image", type=Path, help="Matching native OTA test app image")
    args = parser.parse_args()
    try:
        verify_interruption(args.frame, args.image, getpass("Frame settings password: "))
    except (HTTPException, OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
    print("The frame rejected the interrupted upload and is serving settings from the original image.")


if __name__ == "__main__":
    main()
