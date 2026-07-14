from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LEGACY_PRODUCT_PATH = ROOT / "product" / "espframe.json"
TIMEZONES_PATH = ROOT / "components" / "espframe" / "timezones.py"
TZ_HEADER_PATH = ROOT / "components" / "espframe" / "tz_data_generated.h"
TIME_YAML_PATH = ROOT / "common" / "addon" / "time.yaml"
WEB_SRC_DIR = ROOT / "docs" / "webserver" / "src"
WEB_TEMPLATE_PATH = WEB_SRC_DIR / "app.template.js"
WEB_MODULE_PATHS = {
    "__ESPFRAME_WEB_APP_SHELL__": WEB_SRC_DIR / "app_shell.js",
    "__ESPFRAME_WEB_ENDPOINTS__": WEB_SRC_DIR / "endpoints.js",
    "__ESPFRAME_WEB_RUNTIME_STATE__": WEB_SRC_DIR / "runtime_state.js",
    "__ESPFRAME_WEB_STARTUP_WIZARD__": WEB_SRC_DIR / "startup_wizard.js",
    "__ESPFRAME_WEB_SETTINGS_IMMICH_CARDS__": WEB_SRC_DIR / "settings_immich_cards.js",
    "__ESPFRAME_WEB_SETTINGS_SCREEN_CARDS__": WEB_SRC_DIR / "settings_screen_cards.js",
    "__ESPFRAME_WEB_SETTINGS_FIRMWARE_CARD__": WEB_SRC_DIR / "settings_firmware_card.js",
    "__ESPFRAME_WEB_SETTINGS_CONTROLS__": WEB_SRC_DIR / "settings_controls.js",
    "__ESPFRAME_WEB_LIVE_HELPERS__": WEB_SRC_DIR / "live_helpers.js",
    "__ESPFRAME_WEB_BACKUP_IMPORT__": WEB_SRC_DIR / "backup_import.js",
}
WEB_COMPAT_HELPERS_PATH = WEB_SRC_DIR / "compat.js"
WEB_STYLE_PATH = WEB_SRC_DIR / "style.css"
WEB_PUBLIC_STYLE_PATH = ROOT / "docs" / "public" / "webserver" / "style.css"
WEB_APP_PATH = ROOT / "docs" / "public" / "webserver" / "app.js"
