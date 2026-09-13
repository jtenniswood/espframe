#!/usr/bin/env python3
"""Run in the pinned ESPHome Docker image; no Wi-Fi secrets or device required.

Loads production rotation actions and recovery worker IDs into ESPHome's host
platform. Only hardware endpoints are faked, so cancellation/reentrancy runs
through the real Script, Select, Switch, Action and Scheduler implementations.
"""
from pathlib import Path
import re
import subprocess
import sys
import yaml

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / '.esphome' / 'automation-runtime'


def main():
    rotation = yaml.safe_load((ROOT / 'common/addon/screen_rotation.yaml').read_text())
    slideshow = (ROOT / 'common/addon/immich_slideshow.yaml').read_text()
    recovery = slideshow.split('stop_slideshow_workers(', 1)[1].split(');', 1)[0]
    workers = re.findall(r'id\((\w+)\)', recovery)
    if len(workers) != 22 or len(set(workers)) != 22:
        raise ValueError('Review the recovery fixture when its worker set changes')
    select = rotation['select'][0]
    select.pop('web_server')
    select['restore_value'] = False  # Tests must not depend on host preferences.
    globals_ = [
        {'id': 'espframe_core', 'type': 'AutomationTestCore'},
        {'id': 'frame_lvgl', 'type': 'AutomationTestDisplay *', 'initial_value': 'new AutomationTestDisplay()'},
        {'id': 'test_state', 'type': 'AutomationTestState'},
    ] + [{'id': name, 'type': 'int', 'initial_value': '0'} for name in
         ('layout_count', 'worker_count', 'fetch_count')]
    scripts = rotation['script'] + [
        {'id': 'immich_reapply_current_image_layout', 'then': [{'lambda': 'id(layout_count)++;'}]},
        {'id': 'fetch_guard', 'mode': 'single', 'then': [
            {'lambda': '''
              auto decision = esphome::espframe::slot_fetch_decision(id(test_state), true, true,
                                                                   true, false, false);
              if (decision != esphome::espframe::SlotFetchDecision::ALLOW) id(fetch_guard).stop();
            '''},
            {'lambda': 'id(fetch_count)++;'},
        ]},
    ] + [{'id': name, 'mode': 'restart', 'then': [
        {'delay': '100ms'}, {'lambda': 'id(worker_count)++;'},
    ]} for name in workers]
    actions = [{'delay': '300ms'}, {'lambda': '''
      id(fetch_guard).execute();
      automation_require(id(fetch_count) == 0 && !id(fetch_guard).is_running(), "self-stop suppressed continuation");
      id(developer_features_enabled).turn_on();
      id(layout_count) = 0;
      id(screen_rotation_select).publish_state("180");
    '''}, {'delay': '50ms'}, {'lambda': 'id(screen_rotation_select).publish_state("0");'},
        {'delay': '60ms'}, {'lambda': 'automation_require(id(layout_count) == 0, "restart cancelled old delay");'},
        {'delay': '100ms'}, {'lambda': 'automation_require(id(layout_count) == 1, "one delayed layout after restart");'}]
    for option, angle, pairing in [('90', 180, False), ('180', 270, True), ('270', 0, False), ('0', 90, True)]:
        actions += [{'lambda': f'id(screen_rotation_select).publish_state("{option}");'}, {'delay': '150ms'},
                    {'lambda': f'automation_require(id(frame_lvgl)->rotation == {angle} && id(portrait_pairing_enabled).state == {str(pairing).lower()}, "rotation {option} mapping/pairing");'}]
    actions += [{'lambda': '''
      id(developer_features_enabled).turn_off();
      id(layout_count) = 0;
      id(screen_rotation_select).publish_state("90");
    '''}, {'delay': '200ms'}, {'lambda': '''
      automation_require(id(screen_rotation_select).current_option() == "0", "developer clamp select");
      automation_require(id(frame_lvgl)->rotation == 90 && id(portrait_pairing_enabled).state, "developer clamp display");
      automation_require(id(layout_count) == 1, "one delayed layout after reentrant clamp");
    '''}]
    start = '\n'.join(f'id({worker}).execute();' for worker in workers)
    stopped = ' && '.join(f'!id({worker}).is_running()' for worker in workers)
    actions += [{'lambda': start + '\nesphome::espframe::stop_scripts_in_order(' + recovery + ');\n'
                 + f'automation_require({stopped}, "all recovery workers stopped");'},
                {'delay': '150ms'}, {'lambda': '''
      automation_require(id(worker_count) == 0, "no late recovery continuations");
      std::puts("Automation runtime tests passed");
      std::exit(0);
    '''}]
    config = {
        'substitutions': {'screen_rotation': '0', 'screen_rotation_0': '90', 'screen_rotation_90': '180',
                          'screen_rotation_180': '270', 'screen_rotation_270': '0'},
        'esphome': {'name': 'automation-runtime-test',
                    'platformio_options': {'build_flags': [f'-include {ROOT / "tests/automation_runtime_fixture.h"}']},
                    'on_boot': [{'priority': -200, 'then': actions}]},
        'host': {}, 'logger': {'level': 'INFO'}, 'globals': globals_, 'select': [select],
        'switch': [{'platform': 'template', 'id': name, 'optimistic': True} for name in
                   ('developer_features_enabled', 'portrait_pairing_enabled')],
        'script': scripts,
    }
    CACHE.mkdir(parents=True, exist_ok=True)
    config_path = CACHE / 'runtime.yaml'
    config_path.write_text(yaml.safe_dump(config, sort_keys=False))
    subprocess.run([sys.executable, '-m', 'esphome', 'compile', str(config_path)], check=True)
    executable = CACHE / '.esphome/build/automation-runtime-test/.pioenvs/automation-runtime-test/program'
    subprocess.run([str(executable)], check=True, timeout=15, cwd=CACHE)


if __name__ == '__main__':
    main()
