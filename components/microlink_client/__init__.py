"""ESPHome wrapper for the vendored MicroLink ESP32 Tailscale client."""

from pathlib import Path

import esphome.codegen as cg
from esphome.components import text_sensor
from esphome.components.esp32 import add_extra_build_file, add_idf_sdkconfig_option
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from esphome.core import CORE

CODEOWNERS = ["@jtenniswood"]
DEPENDENCIES = ["esp32", "network"]
AUTO_LOAD = ["text_sensor"]

CONF_AUTH_KEY = "auth_key"
CONF_DEVICE_NAME = "device_name"
CONF_H2_BUFFER_SIZE_KB = "h2_buffer_size_kb"
CONF_JSON_BUFFER_SIZE_KB = "json_buffer_size_kb"
CONF_MAX_PEERS = "max_peers"
CONF_STATE = "state"
CONF_IP_ADDRESS = "ip_address"
CONF_WIFI_TX_POWER_DBM = "wifi_tx_power_dbm"

microlink_client_ns = cg.esphome_ns.namespace("microlink_client")
MicrolinkClient = microlink_client_ns.class_("MicrolinkClient", cg.Component)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MicrolinkClient),
        cv.Required(CONF_AUTH_KEY): cv.string_strict,
        cv.Optional(CONF_DEVICE_NAME): cv.string_strict,
        cv.Optional(CONF_MAX_PEERS, default=8): cv.int_range(min=1, max=64),
        cv.Optional(CONF_H2_BUFFER_SIZE_KB, default=256): cv.int_range(min=64, max=2048),
        cv.Optional(CONF_JSON_BUFFER_SIZE_KB, default=256): cv.int_range(min=64, max=2048),
        cv.Optional(CONF_WIFI_TX_POWER_DBM, default=0): cv.int_range(min=-1, max=20),
        cv.Optional(CONF_STATE): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:tailwind",
        ),
        cv.Optional(CONF_IP_ADDRESS): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:vpn",
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


def _vendor_paths() -> tuple[Path, Path]:
    repo_root = Path(__file__).resolve().parents[2]
    vendor_root = repo_root / "third_party" / "microlink"
    microlink_src = vendor_root / "components" / "microlink"
    wireguard_src = microlink_src / "components" / "wireguard_lwip"

    if not microlink_src.exists():
        raise cv.Invalid(f"Vendored MicroLink component missing: {microlink_src}")
    if not wireguard_src.exists():
        raise cv.Invalid(f"Vendored wireguard_lwip component missing: {wireguard_src}")
    return microlink_src, wireguard_src


def _register_component_files(src: Path, dest_component: str, skip_nested: bool = False) -> None:
    for path in src.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(src)
        if skip_nested and rel.parts and rel.parts[0] == "components":
            continue
        add_extra_build_file(
            str(Path("components") / dest_component / rel),
            str(path),
        )


def _register_microlink_components() -> Path:
    microlink_src, wireguard_src = _vendor_paths()

    # ESPHome clears the IDF components directory after code generation.
    # Registering these files lets ESPHome copy them at the later build-file stage.
    _register_component_files(microlink_src, "microlink", skip_nested=True)
    _register_component_files(wireguard_src, "wireguard_lwip")

    return Path(CORE.relative_build_path("components", "microlink")).resolve()


async def to_code(config):
    microlink_build_dir = _register_microlink_components()

    cg.add_define("USE_MICROLINK_CLIENT")
    cg.add_build_flag(f"-I{microlink_build_dir / 'include'}")
    cg.add_build_flag(f"-I{microlink_build_dir / 'src'}")

    add_idf_sdkconfig_option("CONFIG_ML_MAX_PEERS", config[CONF_MAX_PEERS])
    add_idf_sdkconfig_option("CONFIG_ML_NVS_MAX_PEERS", 64)
    add_idf_sdkconfig_option("CONFIG_ML_H2_BUFFER_SIZE_KB", config[CONF_H2_BUFFER_SIZE_KB])
    add_idf_sdkconfig_option("CONFIG_ML_JSON_BUFFER_SIZE_KB", config[CONF_JSON_BUFFER_SIZE_KB])
    add_idf_sdkconfig_option("CONFIG_ML_ENABLE_CONFIG_HTTPD", False)
    add_idf_sdkconfig_option("CONFIG_ML_ENABLE_CELLULAR", False)
    add_idf_sdkconfig_option("CONFIG_ML_ENABLE_NET_SWITCH", False)
    add_idf_sdkconfig_option("CONFIG_ML_ZERO_COPY_WG", False)

    # MicroLink's WireGuard and Noise paths need these ESP-IDF features enabled.
    add_idf_sdkconfig_option("CONFIG_MBEDTLS_CHACHA20_C", True)
    add_idf_sdkconfig_option("CONFIG_MBEDTLS_POLY1305_C", True)
    add_idf_sdkconfig_option("CONFIG_MBEDTLS_CHACHAPOLY_C", True)
    add_idf_sdkconfig_option("CONFIG_MBEDTLS_HKDF_C", True)
    add_idf_sdkconfig_option("CONFIG_MBEDTLS_SSL_PROTO_TLS1_2", True)
    add_idf_sdkconfig_option("CONFIG_LWIP_SO_RCVBUF", True)
    add_idf_sdkconfig_option("CONFIG_LWIP_IPV6", True)
    add_idf_sdkconfig_option("CONFIG_LWIP_MAX_SOCKETS", 16)
    add_idf_sdkconfig_option("CONFIG_LWIP_TCPIP_RECVMBOX_SIZE", 64)
    add_idf_sdkconfig_option("CONFIG_LWIP_TCP_SND_BUF_DEFAULT", 32768)
    add_idf_sdkconfig_option("CONFIG_LWIP_TCP_WND_DEFAULT", 32768)
    add_idf_sdkconfig_option("CONFIG_ESP_TASK_WDT_TIMEOUT_S", 30)

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_auth_key(config[CONF_AUTH_KEY]))
    cg.add(var.set_device_name(config.get(CONF_DEVICE_NAME, "")))
    cg.add(var.set_max_peers(config[CONF_MAX_PEERS]))
    cg.add(var.set_wifi_tx_power_dbm(config[CONF_WIFI_TX_POWER_DBM]))

    if CONF_STATE in config:
        sens = await text_sensor.new_text_sensor(config[CONF_STATE])
        cg.add(var.set_state_sensor(sens))

    if CONF_IP_ADDRESS in config:
        sens = await text_sensor.new_text_sensor(config[CONF_IP_ADDRESS])
        cg.add(var.set_ip_sensor(sens))
