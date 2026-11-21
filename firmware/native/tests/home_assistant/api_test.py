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
PHOTO_SOURCE_KEY = 3905368779
BACKLIGHT_KEY = 2211332998
ONLINE_KEY = 3727615758
WIFI_STRENGTH_KEY = 3943756064
IP_ADDRESS_KEY = 3394986237
REBOOT_KEY = 2194406813
RESET_REASON_KEY = 2962208445
LANGUAGE_KEY = 4083618279
TIMEOUT_KEY = 1991622891
HEAP_FREE_KEY = 3519121973
HEAP_LARGEST_KEY = 3632003571
PSRAM_FREE_KEY = 276662424
PSRAM_LARGEST_KEY = 1399957840
CONTRACT = Path(__file__).resolve().parents[4] / "product/contract/settings.json"
STOCK_FIELDS = {field["key"]: field for field in json.loads(CONTRACT.read_text())}
STOCK_INTERVALS = STOCK_FIELDS["interval"]["options"]
STOCK_SOURCES = STOCK_FIELDS["photo_source"]["options"]


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


def check_restart_entities(entities):
    by_key = {item.key: item for item in entities}
    reboot = by_key[REBOOT_KEY]
    assert isinstance(reboot, ButtonInfo) and reboot.name == "Device: Reboot Screen"
    assert reboot.icon == "mdi:restart" and reboot.device_class == "restart"
    assert reboot.entity_category == EntityCategory.CONFIG and not reboot.disabled_by_default
    assert build_device_unique_id(MAC, reboot) == f"{MAC}/0/button/Device: Reboot Screen"
    assert build_device_unique_id(MAC, reboot, version=1) == f"{MAC}-button-device__reboot_screen"
    reason = by_key[RESET_REASON_KEY]
    assert isinstance(reason, TextSensorInfo) and reason.name == "Reset Reason"
    assert reason.icon == "mdi:restart" and reason.entity_category == EntityCategory.DIAGNOSTIC
    assert not reason.disabled_by_default
    assert build_device_unique_id(MAC, reason) == f"{MAC}/0/text_sensor/Reset Reason"
    assert build_device_unique_id(MAC, reason, version=1) == f"{MAC}-text_sensor-reset_reason"


async def expect_reset_reason(states, value):
    reason = await states.get()
    assert isinstance(reason, TextSensorState) and reason.key == RESET_REASON_KEY
    assert reason.state == value and not reason.missing_state


def check_memory_entities(entities):
    by_key = {item.key: item for item in entities}
    for key, name, object_id, unit, decimals in (
        (HEAP_FREE_KEY, "Memory: Heap Free", "memory__heap_free", "%", 1),
        (HEAP_LARGEST_KEY, "Memory: Heap Largest Block", "memory__heap_largest_block", "B", 0),
        (PSRAM_FREE_KEY, "Memory: PSRAM Free", "memory__psram_free", "%", 1),
        (PSRAM_LARGEST_KEY, "Memory: PSRAM Largest Block", "memory__psram_largest_block", "B", 0),
    ):
        sensor = by_key[key]
        assert isinstance(sensor, SensorInfo) and not sensor.disabled_by_default
        assert sensor.name == name and sensor.icon == "mdi:memory" and sensor.entity_category == EntityCategory.DIAGNOSTIC
        assert sensor.unit_of_measurement == unit and sensor.accuracy_decimals == decimals
        assert not sensor.device_class and sensor.state_class == SensorStateClass.NONE
        assert build_device_unique_id(MAC, sensor) == f"{MAC}/0/sensor/{name}"
        assert build_device_unique_id(MAC, sensor, version=1) == f"{MAC}-sensor-{object_id}"


async def expect_memory_states(states, readings):
    for key, expected in readings:
        sensor = await states.get()
        assert isinstance(sensor, SensorState) and sensor.key == key
        assert sensor.missing_state == (expected is None)
        if expected is not None:
            assert sensor.state == expected


def check_display_entities(entities):
    by_key = {item.key: item for item in entities}
    for key, field, name, icon, category, object_id in (
        (LANGUAGE_KEY, "device_language", "Device: Language", "mdi:translate", EntityCategory.CONFIG, "device__language"),
        (TIMEOUT_KEY, "conn_timeout", "Screen: Connection Timeout", "mdi:connection", EntityCategory.NONE,
         "screen__connection_timeout"),
    ):
        entity = by_key[key]
        assert isinstance(entity, SelectInfo) and not entity.disabled_by_default
        assert entity.name == name and entity.icon == icon and entity.entity_category == category
        assert entity.options == STOCK_FIELDS[field]["options"]
        assert build_device_unique_id(MAC, entity) == f"{MAC}/0/select/{name}"
        assert build_device_unique_id(MAC, entity, version=1) == f"{MAC}-select-{object_id}"


async def expect_select(states, key, value):
    state = await states.get()
    assert isinstance(state, SelectState) and state.key == key and state.state == value


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

    disconnected = asyncio.Event()

    async def on_stop(_expected):
        disconnected.set()

    api = client(port)
    await api.connect(login=True, on_stop=on_stop)
    try:
        info, entities, services = await api.device_info_and_list_entities()
        assert info.name == NAME and info.friendly_name == "Espframe Test"
        assert info.mac_address.lower().replace(":", "") == MAC
        assert info.api_encryption_supported and not info.api_encryption_provisionable
        assert info.project_name == "jtenniswood.immich-frame"
        assert not services and len(entities) == 16
        check_network_entities(entities)
        check_restart_entities(entities)
        check_display_entities(entities)
        check_memory_entities(entities)
        buttons = [entity for entity in entities if isinstance(entity, ButtonInfo) and entity.key != REBOOT_KEY]
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
        source = next(entity for entity in entities if entity.key == PHOTO_SOURCE_KEY)
        assert isinstance(source, SelectInfo) and source.name == "Photos: Source"
        assert source.options == STOCK_SOURCES and source.icon == "mdi:image-filter-frames"
        assert build_device_unique_id(MAC, source) == f"{MAC}/0/select/Photos: Source"
        assert build_device_unique_id(MAC, source, version=1) == f"{MAC}-select-photos__source"
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
        initial_source = await states.get()
        assert isinstance(initial_source, SelectState) and initial_source.key == PHOTO_SOURCE_KEY
        assert initial_source.state == "All Photos"
        await expect_network_states(states, 70)
        await expect_reset_reason(states, "power-on event")
        await expect_select(states, LANGUAGE_KEY, "en")
        await expect_select(states, TIMEOUT_KEY, "10 minutes")
        await expect_memory_states(states, (
            (HEAP_FREE_KEY, 35.25), (HEAP_LARGEST_KEY, 32768), (PSRAM_FREE_KEY, 54.5), (PSRAM_LARGEST_KEY, 8_388_608),
        ))
        for command, readings in (
            (b"m", ((HEAP_LARGEST_KEY, 8192), (PSRAM_FREE_KEY, None), (PSRAM_LARGEST_KEY, 0))),
            (b"r", ((HEAP_FREE_KEY, 30), (PSRAM_FREE_KEY, 41.5), (PSRAM_LARGEST_KEY, 262144))),
        ):
            clock_input.write(command)
            await clock_input.drain()
            await expect_memory_states(states, readings)
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
        api.select_command(source.key, "favorites")
        api.select_command(source.key, "Person", device_id=1)
        api.select_command(LANGUAGE_KEY, "FR")
        api.select_command(LANGUAGE_KEY, "fr", device_id=1)
        api.select_command(TIMEOUT_KEY, "30 second")
        api.select_command(TIMEOUT_KEY, "17 seconds")
        api.select_command(TIMEOUT_KEY, "30 seconds", device_id=1)
        api.button_command(REBOOT_KEY, device_id=1)
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
        for requested, applied in (("Favorites", "Favorites"), ("Album", "Favorites"), ("Tag", "Tag"), ("Person", "Person")):
            api.select_command(source.key, requested)
            accepted = await states.get()
            assert accepted.key == PHOTO_SOURCE_KEY and accepted.state == applied
        for key, requested, applied in (
            (LANGUAGE_KEY, "pt-br", "pt-br"), (LANGUAGE_KEY, "de", "pt-br"),
            (TIMEOUT_KEY, "45 seconds", "45 seconds"), (TIMEOUT_KEY, "2 minutes", "45 seconds"),
        ):
            api.select_command(key, requested)
            await expect_select(states, key, applied)
        for command, key, value in ((b"p", LANGUAGE_KEY, "he"), (b"t", TIMEOUT_KEY, "15 minutes")):
            clock_input.write(command)
            await clock_input.drain()
            await expect_select(states, key, value)
        for command, key, value in ((b"i", INTERVAL_KEY, "23 seconds"), (b"f", PHOTO_SOURCE_KEY, "Memories")):
            clock_input.write(command)  # Change playback independently of this Home Assistant session.
            await clock_input.drain()
            await expect_select(states, key, value)
        _, refreshed_entities, _ = await api.device_info_and_list_entities()
        refreshed_interval = next(entity for entity in refreshed_entities if entity.key == INTERVAL_KEY)
        assert refreshed_interval.options == [*STOCK_INTERVALS, "23 seconds"]
        api.select_command(INTERVAL_KEY, "23 seconds")
        await expect_select(states, INTERVAL_KEY, "23 seconds")
        assert isinstance(await connection.send_message_await_response(PingRequest(), PingResponse), PingResponse)
        assert states.empty()
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
        api.button_command(REBOOT_KEY)
        await disconnected.wait()
    finally:
        await api.disconnect()

    await api.connect(login=True)
    try:
        assert (await api.device_info()).name == NAME
        _, entities, _ = await api.device_info_and_list_entities()
        interval = next(entity for entity in entities if isinstance(entity, SelectInfo))
        assert interval.options == [*STOCK_INTERVALS, "23 seconds"]
        states = asyncio.Queue()
        api.subscribe_states(states.put_nowait)
        assert (await states.get()).state == "23 seconds"
        restored = await states.get()
        assert isinstance(restored, LightState) and restored.state and round(restored.brightness * 100) == 20
        source = await states.get()
        assert isinstance(source, SelectState) and source.key == PHOTO_SOURCE_KEY and source.state == "Memories"
        await expect_network_states(states, 100)
        await expect_reset_reason(states, "software via esp_restart")
        await expect_select(states, LANGUAGE_KEY, "he")
        await expect_select(states, TIMEOUT_KEY, "15 minutes")
        await expect_memory_states(states, (
            (HEAP_FREE_KEY, 30), (HEAP_LARGEST_KEY, 8192), (PSRAM_FREE_KEY, 41.5), (PSRAM_LARGEST_KEY, 262144),
        ))
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
    print("The stock reboot button disconnected the simulated frame and Reset Reason updated on reconnect.")
    print("Device language and connection timeout used stock select identities, options and live state.")
    print("Heap and PSRAM sensors preserved stock identities and reported changed, missing and zero readings.")
    print("Silent clients, wrong keys, plaintext, oversized frames and inconsistent lengths were rejected. The client reconnected.")


if __name__ == "__main__":
    asyncio.run(main(sys.argv[1]))
