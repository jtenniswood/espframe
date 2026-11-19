#!/usr/bin/env python3
"""Create a private native_config partition image for a dedicated test frame."""

import argparse
import csv
from getpass import getpass
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="New settings image to create")
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Choose a new output path")
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        parser.error("Activate the ESP-IDF environment first")
    generator = Path(idf_path) / "components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"

    ssid = input("Wi-Fi name: ")
    password = getpass("Wi-Fi password: ")
    url = input("Immich base URL: ").strip()
    key = getpass("Immich API key: ")
    if not 1 <= len(ssid.encode()) <= 32 or len(password.encode()) > 64:
        parser.error("Wi-Fi name or password is too long or the name is empty")
    if not url.startswith(("https://", "http://")) or len(url.encode()) > 256:
        parser.error("Enter an absolute HTTP or HTTPS Immich URL")
    if not 1 <= len(key) <= 256 or any(not 33 <= ord(c) <= 126 for c in key):
        parser.error("Enter a valid Immich API key")

    with TemporaryDirectory() as work:
        partition = Path(work) / "native-settings.bin"
        values = Path(work) / "native-settings.csv"
        with values.open("w", newline="") as file:
            writer = csv.writer(file)
            writer.writerow(("key", "type", "encoding", "value"))
            writer.writerow(("player", "namespace", "", ""))
            for name, value in (("ssid", ssid), ("password", password), ("immich_url", url), ("immich_key", key)):
                writer.writerow((name, "data", "string", value))
        subprocess.run([sys.executable, str(generator), "generate", str(values), str(partition), "0x6000"], check=True)
        with os.fdopen(os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as output:
            output.write(partition.read_bytes())
    print(f"Created {args.output} for the native_config partition at 0x410000.")


if __name__ == "__main__":
    main()
