"""Exercise the native responder over loopback with Home Assistant's ESPHome client."""

import asyncio
from base64 import b64encode
import sys

from aioesphomeapi import APIClient, ButtonInfo, InvalidEncryptionKeyAPIError, build_device_unique_id
from aioesphomeapi.api_pb2 import PingRequest, PingResponse
from aioesphomeapi.noise import NoiseHandshake


HOST = "127.0.0.1"
NAME = "espframe-ha-test"
MAC = "02aabbccddee"
KEY = b64encode(bytes(range(1, 33))).decode()


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
        info, buttons, services = await api.device_info_and_list_entities()
        assert info.name == NAME and info.friendly_name == "Espframe Test"
        assert info.mac_address.lower().replace(":", "") == MAC
        assert info.api_encryption_supported and not info.api_encryption_provisionable
        assert info.project_name == "jtenniswood.immich-frame"
        assert not services and len(buttons) == 2
        for button, name, key, object_id in zip(
            buttons, ("Screen: Sleep", "Screen: Wake"), (2655868716, 1917547667),
            ("screen_sleep", "screen_wake"), strict=True,
        ):
            assert isinstance(button, ButtonInfo)
            assert button.name == name and button.key == key and button.object_id == object_id
            assert build_device_unique_id(MAC, button) == f"{MAC}/0/button/{name}"
            assert build_device_unique_id(MAC, button, version=1) == f"{MAC}-button-{object_id}"
        await api.device_capabilities()
        api.subscribe_states(lambda *_: None)
        connection = api._get_connection()
        assert isinstance(await connection.send_message_await_response(PingRequest(), PingResponse), PingResponse)
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
    print("ESPHome client authenticated, read identity and entities, and drove the native sleep/wake schedule")
    print("Silent clients, wrong keys, plaintext, oversized frames and inconsistent lengths were rejected. The client reconnected.")


if __name__ == "__main__":
    asyncio.run(main(sys.argv[1]))
