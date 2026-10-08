"""Exercise component registration with and without LVGL and parent overrides."""
import asyncio
from itertools import product
from pathlib import Path
import runpy
import sys
import types
import unittest
from unittest.mock import AsyncMock, MagicMock, patch

ROOT = Path(__file__).resolve().parents[1]


class MemoryCodegenTests(unittest.TestCase):
    def test_registration_matrix(self):
        for lvgl, psram, diagnostics in product((False, True), repeat=3):
            with self.subTest(lvgl=lvgl, psram=psram, diagnostics=diagnostics):
                cg = MagicMock()
                cg.register_component = AsyncMock()
                core = types.ModuleType("esphome.core")
                full_config = {}
                if lvgl:
                    full_config["lvgl"] = [{}]
                if psram:
                    full_config["psram"] = {}
                core.CORE = types.SimpleNamespace(config=full_config)
                const = types.ModuleType("esphome.const")
                const.CONF_ID = "id"
                const.CONF_SETUP_PRIORITY = "setup_priority"
                modules = {
                    "esphome": types.ModuleType("esphome"),
                    "esphome.codegen": cg,
                    "esphome.config_validation": MagicMock(),
                    "esphome.final_validate": MagicMock(),
                    "esphome.const": const,
                    "esphome.core": core,
                }
                with patch.dict(sys.modules, modules):
                    component = runpy.run_path(str(ROOT / "components/espframe/__init__.py"))
                    config = {
                        "id": "parent", "setup_priority": 500,
                        "memory_diagnostics": diagnostics,
                        "memory_before_id": "before", "memory_after_id": "after",
                    }
                    asyncio.run(component["to_code"](config))
                registrations = cg.register_component.await_args_list
                self.assertEqual(registrations[0].args[1], config)
                self.assertEqual(len(registrations), 3 if lvgl and psram else 1)
                self.assertEqual(cg.add_define.call_count, int(diagnostics))
                if lvgl and psram:
                    cg.add_build_flag.assert_called_once_with("-Wl,--wrap=heap_caps_aligned_alloc")
                    for call in registrations[1:]:
                        self.assertNotIn("setup_priority", call.args[1])
                    self.assertEqual(cg.new_Pvariable.call_args_list[1].args, ("before", True))
                    self.assertEqual(cg.new_Pvariable.call_args_list[2].args, ("after", False))
                else:
                    cg.add_build_flag.assert_not_called()
                self.assertEqual(config["setup_priority"], 500)

    def test_lvgl_priority_validation(self):
        cv = MagicMock()
        cv.Invalid = ValueError
        fv = MagicMock()
        const = types.ModuleType("esphome.const")
        const.CONF_ID = "id"
        const.CONF_SETUP_PRIORITY = "setup_priority"
        modules = {
            "esphome": types.ModuleType("esphome"),
            "esphome.codegen": MagicMock(),
            "esphome.config_validation": cv,
            "esphome.final_validate": fv,
            "esphome.const": const,
            "esphome.core": MagicMock(),
        }
        with patch.dict(sys.modules, modules):
            component = runpy.run_path(str(ROOT / "components/espframe/__init__.py"))
            validate = component["FINAL_VALIDATE_SCHEMA"]
            config = {"setup_priority": 500}
            for full in ({}, {"lvgl": [{"setup_priority": 500}]},
                         {"psram": {}, "lvgl": [{}]},
                         {"psram": {}, "lvgl": [{"setup_priority": 400}]}):
                fv.full_config.get.return_value = full
                self.assertIs(validate(config), config)
            for priority in (399, 401, 500, 100):
                fv.full_config.get.return_value = {"psram": {}, "lvgl": [{}, {"setup_priority": priority}]}
                with self.assertRaisesRegex(ValueError, "lvgl.setup_priority.*400"):
                    validate(config)


if __name__ == "__main__":
    unittest.main()
