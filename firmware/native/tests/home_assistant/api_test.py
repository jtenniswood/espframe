"""Exercise the native responder over loopback with Home Assistant's ESPHome client."""

import asyncio
from base64 import b64encode
import json
from pathlib import Path
import sys

from aioesphomeapi import (
    APIClient, BinarySensorInfo, BinarySensorState, ButtonInfo, ColorMode, EntityCategory,
    InvalidEncryptionKeyAPIError, LightInfo, LightState, SelectInfo, SelectState, SensorInfo,
    SensorState, SensorStateClass, TextSensorInfo, TextSensorState, build_device_unique_id,
)
from aioesphomeapi.api_pb2 import PingRequest, PingResponse
from aioesphomeapi.noise import NoiseHandshake


HOST = "127.0.0.1"
NAME = "espframe-ha-test"
MAC = "02aabbccddee"
KEY = b64encode(bytes(range(1, 33))).decode()
INTERVAL_KEY = 3029912432
BACKLIGHT_KEY = 2211332998
ONLINE_KEY = 3727615758
WIFI_STRENGTH_KEY = 3943756064
IP_ADDRESS_KEY = 3394986237
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


def check_network_entities(entities):
    by_key = {item.key: item for item in entities}
    for key, entity_type, name, domain, object_id, icon in (
        (ONLINE_KEY, BinarySensorInfo, "Network: Online", "binary_sensor", "network__online", "mdi:check-network"),
        (WIFI_STRENGTH_KEY, SensorInfo, "Network: WiFi Strength", "sensor", "network__wifi_strength", "mdi:wifi"),
        (IP_ADDRESS_KEY, TextSensorInfo, "Network: IP Address", "text_sensor", "network__ip_address", "mdi:ip-network"),
    ):
        entity = by_key[key]
        assert isinstance(entity, entity_type)
        assert entity.name == name and entity.icon == icon and entity.entity_category == EntityCategory.DIAGNOSTIC
        assert build_device_unique_id(MAC, entity) == f"{MAC}/0/{domain}/{name}"
        assert build_device_unique_id(MAC, entity, version=1) == f"{MAC}-{domain}-{object_id}"
    online = by_key[ONLINE_KEY]
    assert online.device_class == "connectivity" and online.is_status_binary_sensor
    strength = by_key[WIFI_STRENGTH_KEY]
    assert strength.unit_of_measurement == "%" and strength.device_class == "signal_strength"
    assert strength.state_class == SensorStateClass.MEASUREMENT and strength.accuracy_decimals == 0


async def expect_wifi_strength(states, expected):
    strength = await states.get()
    assert isinstance(strength, SensorState) and strength.key == WIFI_STRENGTH_KEY
    assert strength.missing_state == (expected is None)
    if expected is not None:
        assert strength.state == expected


async def expect_network_states(states, strength):
    online = await states.get()
    assert isinstance(online, BinarySensorState) and online.key == ONLINE_KEY and online.state
    address = await states.get()
    assert isinstance(address, TextSensorState) and address.key == IP_ADDRESS_KEY and address.state == HOST
    await expect_wifi_strength(states, strength)


async def verify(port, clock_input):
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
        assert not services and len(entities) == 7
        check_network_entities(entities)
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
        backlight = next(entity for entity in entities if isinstance(entity, LightInfo))
        assert backlight.name == "Screen: Backlight" and backlight.key == BACKLIGHT_KEY
        assert backlight.supported_color_modes == [ColorMode.BRIGHTNESS]
        assert build_device_unique_id(MAC, backlight) == f"{MAC}/0/light/Screen: Backlight"
        assert build_device_unique_id(MAC, backlight, version=1) == f"{MAC}-light-screen_backlight"
        await api.device_capabilities()
        states = asyncio.Queue()
        api.subscribe_states(states.put_nowait)
        first = await states.get()
        assert isinstance(first, SelectState) and first.key == INTERVAL_KEY and first.state == "17 seconds"

        async def expect_backlight(level):
            current = await states.get()
            assert isinstance(current, LightState) and current.key == BACKLIGHT_KEY
            assert current.color_mode == ColorMode.BRIGHTNESS and current.state == (level > 0)
            assert round(current.brightness * 100) == level

        await expect_backlight(64)
        await expect_network_states(states, 70)
        for command, strength in ((b"x", None), (b"w", 0), (b"s", 100)):
            clock_input.write(command)
            await clock_input.drain()
            await expect_wifi_strength(states, strength)

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
        await expect_backlight(0)
        api.button_command(buttons[1].key)
        await expect_backlight(64)
        api.light_command(BACKLIGHT_KEY, brightness=0.37, color_mode=ColorMode.BRIGHTNESS)
        await expect_backlight(37)
        api.light_command(BACKLIGHT_KEY, brightness=1 / 255)
        await expect_backlight(1)
        api.light_command(BACKLIGHT_KEY, state=False)
        await expect_backlight(0)
        api.light_command(BACKLIGHT_KEY, state=True)
        await expect_backlight(64)
        api.light_command(BACKLIGHT_KEY, state=False)
        await expect_backlight(0)
        api.light_command(BACKLIGHT_KEY, state=True, brightness=0.28)
        await expect_backlight(28)
        api.light_command(BACKLIGHT_KEY, brightness=1.25)
        api.light_command(BACKLIGHT_KEY, brightness=float("nan"))
        api.light_command(BACKLIGHT_KEY, state=False, device_id=1)
        api.light_command(1234, state=False)
        await asyncio.sleep(0.25)
        assert states.empty()

        clock_input.write(b"n")  # The player enters scheduled off-hours while the API session is idle.
        await clock_input.drain()
        await expect_backlight(0)
        api.light_command(BACKLIGHT_KEY, state=True, brightness=0.20)
        await asyncio.sleep(0.25)
        assert states.empty()
        api.button_command(buttons[1].key)
        await expect_backlight(64)
        api.light_command(BACKLIGHT_KEY, brightness=0.20)
        await expect_backlight(20)
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
        restored = await states.get()
        assert isinstance(restored, LightState) and restored.state and round(restored.brightness * 100) == 20
        await expect_network_states(states, 100)
    finally:
        await api.disconnect()


async def main(executable):
    process = await asyncio.create_subprocess_exec(
        executable, stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
    )
    try:
        async with asyncio.timeout(15):
            port = int(await process.stdout.readline())
            await verify(port, process.stdin)
    finally:
        if process.returncode is None:
            process.terminate()
        await process.wait()
    print("ESPHome client authenticated and controlled the backlight, sleep, wake and slideshow interval with stock entity IDs.")
    print("Network status, IP address and Wi-Fi strength used the stock diagnostic identities and reported live state.")
    print("Silent clients, wrong keys, plaintext, oversized frames and inconsistent lengths were rejected. The client reconnected.")


if __name__ == "__main__":
    asyncio.run(main(sys.argv[1]))
