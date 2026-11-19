"""Exercise the native responder over loopback with Home Assistant's ESPHome client."""

import asyncio
from base64 import b64encode
import json
from pathlib import Path
import sys

from aioesphomeapi import APIClient, ButtonInfo, InvalidEncryptionKeyAPIError, SelectInfo, SelectState, build_device_unique_id
from aioesphomeapi.api_pb2 import PingRequest, PingResponse
from aioesphomeapi.noise import NoiseHandshake


HOST = "127.0.0.1"
NAME = "espframe-ha-test"
MAC = "02aabbccddee"
KEY = b64encode(bytes(range(1, 33))).decode()
INTERVAL_KEY = 3029912432
CONTRACT = Path(__file__).resolve().parents[4] / "product/contract/settings.json"
STOCK_INTERVALS = next(field["options"] for field in json.loads(CONTRACT.read_text()) if field["key"] == "interval")


def client(port, key=KEY):
    return APIClient(HOST, port, None, noise_psk=key, expected_name=NAME, expected_mac=MAC)


async def assert_rejected(port, frame):
    reader, writer = await asyncio.open_connection(HOST, port)
    try:
        writer.write(frame)
        await writer.drain()
        assert await reader.read(1) == b""
    finally:
        writer.close()
        await writer.wait_closed()


async def assert_invalid_record(port, oversized):
    reader, writer = await asyncio.open_connection(HOST, port)

    async def send_frame(body):
        writer.write(b"\x01" + len(body).to_bytes(2, "big") + body)
        await writer.drain()

    async def read_frame():
        header = await reader.readexactly(3)
        assert header[0] == 1
        return await reader.readexactly(int.from_bytes(header[1:], "big"))

    try:
        handshake = NoiseHandshake(KEY, b"NoiseAPIInit\0\0")
        await send_frame(b"")
        assert await read_frame() == b"\x01espframe-ha-test\0" + MAC.encode() + b"\0"
        await send_frame(b"\0" + handshake.write_message())
        response = await read_frame()
        assert response[0] == 0
        handshake.read_message(response[1:])
        if oversized:
            writer.write(b"\x01\x08\x00")
            await writer.drain()
        else:
            encrypt, _ = handshake.get_ciphers()
            # The enclosed message claims a body byte it does not contain.
            await send_frame(encrypt.encrypt(b"\x00\x01\x00\x01"))
        assert await reader.read(1) == b""
    finally:
        writer.close()
        await writer.wait_closed()


async def verify(port, output):
    reader, writer = await asyncio.open_connection(HOST, port)
    try:
        async with asyncio.timeout(6):
            await asyncio.sleep(2.3)
            writer.write(b"\x01\x00\x00")
            await writer.drain()
            greeting = b"\x01" + NAME.encode() + b"\0" + MAC.encode() + b"\0"
            assert await reader.readexactly(3 + len(greeting)) == b"\x01\x00" + bytes([len(greeting)]) + greeting
            assert await reader.read(1) == b""  # Unfinished handshake.
    finally:
        writer.close()
        await writer.wait_closed()
    await assert_rejected(port, b"\x00\x00\x00")  # Plaintext.
    await assert_rejected(port, b"\x01\x00\x41")  # Oversized handshake.
    await assert_invalid_record(port, oversized=True)
    await assert_invalid_record(port, oversized=False)
    try:
        await client(port, b64encode(b"x" * 32).decode()).connect(login=True)
    except InvalidEncryptionKeyAPIError:
        pass
    else:
        raise AssertionError("The frame accepted the wrong encryption key")

    api = client(port)
    await api.connect(login=True)
    try:
        info, entities, services = await api.device_info_and_list_entities()
        assert info.name == NAME and info.friendly_name == "Espframe Test"
        assert info.mac_address.lower().replace(":", "") == MAC
        assert info.api_encryption_supported and not info.api_encryption_provisionable
        assert info.project_name == "jtenniswood.immich-frame"
        assert not services and len(entities) == 3
        buttons = [entity for entity in entities if isinstance(entity, ButtonInfo)]
        for button, name, key, object_id in zip(
            buttons, ("Screen: Sleep", "Screen: Wake"), (2655868716, 1917547667),
            ("screen_sleep", "screen_wake"), strict=True,
        ):
            assert isinstance(button, ButtonInfo)
            assert button.name == name and button.key == key and button.object_id == object_id
            assert build_device_unique_id(MAC, button) == f"{MAC}/0/button/{name}"
            assert build_device_unique_id(MAC, button, version=1) == f"{MAC}-button-{object_id}"
        interval = next(entity for entity in entities if isinstance(entity, SelectInfo))
        assert interval.name == "Photos: Slideshow Interval" and interval.key == INTERVAL_KEY
        assert interval.options == [*STOCK_INTERVALS, "17 seconds"]
        assert build_device_unique_id(MAC, interval) == f"{MAC}/0/select/Photos: Slideshow Interval"
        assert build_device_unique_id(MAC, interval, version=1) == f"{MAC}-select-photos_slideshow_interval"
        await api.device_capabilities()
        states = asyncio.Queue()
        api.subscribe_states(states.put_nowait)
        first = await states.get()
        assert isinstance(first, SelectState) and first.key == INTERVAL_KEY and first.state == "17 seconds"
        connection = api._get_connection()
        assert isinstance(await connection.send_message_await_response(PingRequest(), PingResponse), PingResponse)
        api.select_command(1234, "45 seconds")
        api.select_command(interval.key, "45 seconds", device_id=1)
        api.select_command(interval.key, "3 seconds")
        api.select_command(interval.key, "1 minutes")
        assert isinstance(await connection.send_message_await_response(PingRequest(), PingResponse), PingResponse)
        assert states.empty()
        for requested, applied in (
            ("45 seconds", "45 seconds"),
            ("1 minute", "45 seconds"),  # Persistence failure preserves the current setting.
            ("17 seconds", "17 seconds"),  # The native custom option stays usable within this session.
            ("2 hours", "2 hours"),
        ):
            api.select_command(interval.key, requested)
            accepted = await states.get()
            assert accepted.key == INTERVAL_KEY and accepted.state == applied
        api.button_command(1234)
        api.button_command(buttons[1].key, device_id=1)
        api.button_command(buttons[0].key)
        assert await output.readline() == b"asleep\n"
        api.button_command(buttons[1].key)
        assert await output.readline() == b"awake\n"
    finally:
        await api.disconnect()

    await api.connect(login=True)
    try:
        assert (await api.device_info()).name == NAME
        _, entities, _ = await api.device_info_and_list_entities()
        interval = next(entity for entity in entities if isinstance(entity, SelectInfo))
        assert interval.options == STOCK_INTERVALS
        states = asyncio.Queue()
        api.subscribe_states(states.put_nowait)
        assert (await states.get()).state == "2 hours"
    finally:
        await api.disconnect()


async def main(executable):
    process = await asyncio.create_subprocess_exec(executable, stdout=asyncio.subprocess.PIPE)
    try:
        async with asyncio.timeout(15):
            port = int(await process.stdout.readline())
            await verify(port, process.stdout)
    finally:
        if process.returncode is None:
            process.terminate()
        await process.wait()
    print("ESPHome client authenticated and controlled sleep, wake and the slideshow interval with stock entity IDs.")
    print("Silent clients, wrong keys, plaintext, oversized frames and inconsistent lengths were rejected. The client reconnected.")


if __name__ == "__main__":
    asyncio.run(main(sys.argv[1]))
