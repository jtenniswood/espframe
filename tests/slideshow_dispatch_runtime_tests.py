#!/usr/bin/env python3
"""Run with the pinned ESPHome environment; exercises production dispatch YAML."""
from pathlib import Path
import re
import subprocess
import sys
import yaml

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / '.esphome' / 'slideshow-dispatch-runtime'


class LambdaLoader(yaml.SafeLoader):
    pass


LambdaLoader.add_constructor('!lambda', lambda loader, node: loader.construct_scalar(node))


def main():
    source = yaml.load((ROOT / 'common/addon/immich_slideshow.yaml').read_text(), Loader=LambdaLoader)
    dispatch = next(script for script in source['script'] if script['id'] == 'espframe_slideshow_dispatch')
    body = dispatch['then'][-1]['lambda']
    # Host globals expose references; device components expose pointers when
    # passed to the layout helper. Adapt only those hardware arguments.
    dispatch['then'][-1]['lambda'] = body.replace('apply_slot_layout(id(', 'apply_slot_layout(&id(')
    endpoints = sorted(set(re.findall(r'id\((\w+)\)', body)) - {'espframe_core', 'photo_source_menu_open'})
    api = yaml.load((ROOT / 'common/addon/immich_api.yaml').read_text(), Loader=LambdaLoader)
    tag_scope = next(script for script in api['script'] if script['id'] == 'immich_fetch_asset_tag_scope')
    # Run the production scheduling and stale/offline guards, replacing only
    # the network condition and HTTP endpoint on the host platform.
    request_index = next(i for i, action in enumerate(tag_scope['then']) if 'http_request.get' in action)
    tag_scope['then'] = tag_scope['then'][:request_index]
    for action in tag_scope['then']:
        if action.get('if', {}).get('condition') == {'not': {'wifi.connected': None}}:
            action['if']['condition'] = {'lambda': 'return !id(test_wifi_connected);'}
    tag_scope['then'].append({'lambda': '''
      dispatch_require(!dispatch_response_active, "asset request unwound the search response");
      id(tag_request_count)++;
    '''})
    actions = [
        {'delay': '100ms'},
        {'lambda': '''
          id(immich_fetch_portrait_companion).on_call = []() {
            dispatch_require(!dispatch_response_active, "HTTP work unwound the triggering response");
            dispatch_order.push_back(1);
            // A synchronous response adds a second command while dispatch runs.
            dispatch_response_active = true;
            id(espframe_core).slideshow().emit_command(SLIDESHOW_COMMAND_UPDATE_PORTRAIT_LEFT);
            id(espframe_slideshow_dispatch).execute();
            dispatch_response_active = false;
          };
          id(immich_portrait_left).on_call = []() {
            dispatch_require(!dispatch_response_active, "image request unwound the response");
            dispatch_order.push_back(2);
          };
          dispatch_response_active = true;
          id(espframe_core).slideshow().emit_command(SLIDESHOW_COMMAND_FETCH_COMPANION);
          id(espframe_slideshow_dispatch).execute();
          dispatch_require(dispatch_order.empty(), "caller returned before dispatch");
          dispatch_response_active = false;
        '''},
        {'delay': '100ms'},
        {'lambda': '''
          dispatch_require(dispatch_order == std::vector<int>({1, 2}), "reentrant command order, exactly once");
          dispatch_require(!id(espframe_slideshow_dispatch).is_running(), "queued runs drained");
          // Recovery clears the model before the scheduled dispatch can run.
          id(espframe_core).slideshow().emit_command(SLIDESHOW_COMMAND_FETCH_COMPANION);
          id(espframe_slideshow_dispatch).execute();
          id(espframe_core).slideshow().reset_state();
        '''},
        {'delay': '100ms'},
        {'lambda': '''
          dispatch_require(dispatch_order.size() == 2, "no stale requests after recovery");
          // A temporarily open menu keeps commands queued for the next dispatch.
          id(photo_source_menu_open) = true;
          id(espframe_core).slideshow().emit_command(SLIDESHOW_COMMAND_UPDATE_PORTRAIT_LEFT);
          id(espframe_slideshow_dispatch).execute();
        '''},
        {'delay': '100ms'},
        {'lambda': '''
          dispatch_require(dispatch_order.size() == 2, "menu blocks dispatch");
          id(photo_source_menu_open) = false;
          id(espframe_slideshow_dispatch).execute();
        '''},
        {'delay': '100ms'},
        {'lambda': '''
          dispatch_require(dispatch_order == std::vector<int>({1, 2, 2}), "menu resume preserves queued command");
          dispatch_response_active = true;
          id(immich_fetch_asset_tag_scope).execute();
          dispatch_require(id(tag_request_count) == 0, "asset request is deferred");
          dispatch_response_active = false;
        '''},
        {'delay': '100ms'},
        {'lambda': '''
          dispatch_require(id(tag_request_count) == 1, "one asset request after response returned");
          id(immich_fetch_asset_tag_scope).execute();
          // A filter reset invalidates a pending request before its continuation.
          id(immich_request_state).current = false;
          dispatch_require(id(immich_request_state).filter_scope_request_pending(),
                           "invalidated scope remains pending until the continuation runs");
        '''},
        {'delay': '100ms'},
        {'lambda': '''
          dispatch_require(id(tag_request_count) == 1, "no stale asset request after filter reset");
          dispatch_require(!id(immich_fetch_asset_tag_scope).is_running(), "stale script stopped");
          dispatch_require(!id(immich_request_state).filter_scope_request_pending(),
                           "filter reset clears stale pending scope state");
          id(immich_request_state).begin_filter_scope_request();
          id(test_wifi_connected) = false;
          id(espframe_core).slideshow().state().slot0.filter_tag_scope_known = true;
          id(immich_fetch_asset_tag_scope).execute();
        '''},
        {'delay': '100ms'},
        {'lambda': '''
          dispatch_require(id(tag_request_count) == 1, "no asset request while offline");
          dispatch_require(!id(immich_request_state).filter_scope_request_pending(),
                           "offline guard clears pending request");
          dispatch_require(!id(espframe_core).slideshow().state().slot0.filter_tag_scope_known, "offline scope remains unknown");
          std::puts("Slideshow dispatch runtime tests passed");
          std::exit(0);
        '''},
    ]
    config = {
        'substitutions': {'display_width': '1280', 'display_height': '800',
                          'portrait_display_width': '800', 'portrait_display_height': '1280'},
        'esphome': {'name': 'slideshow-dispatch-test',
                    'platformio_options': {'build_flags': [f'-include {ROOT / "tests/slideshow_dispatch_fixture.h"}',
                                                          f'-I{ROOT / "tests/slideshow_dispatch_stubs"}']},
                    'on_boot': [{'priority': -200, 'then': actions}]},
        'host': {}, 'logger': {'level': 'INFO'}, 'json': {},
        'globals': [{'id': 'espframe_core', 'type': 'DispatchTestCore'},
                    {'id': 'immich_request_state', 'type': 'DispatchTestRequestState'},
                    {'id': 'tag_request_count', 'type': 'int', 'initial_value': '0'},
                    {'id': 'test_wifi_connected', 'type': 'bool', 'initial_value': 'true'},
                    {'id': 'photo_source_menu_open', 'type': 'bool', 'initial_value': 'false'}] +
                   [{'id': name, 'type': 'DispatchTestEndpoint'} for name in endpoints],
        'script': [dispatch, tag_scope],
    }
    CACHE.mkdir(parents=True, exist_ok=True)
    config_path = CACHE / 'runtime.yaml'
    config_path.write_text(yaml.safe_dump(config, sort_keys=False))
    subprocess.run([sys.executable, '-m', 'esphome', 'compile', str(config_path)], check=True)
    executable = CACHE / '.esphome/build/slideshow-dispatch-test/.pioenvs/slideshow-dispatch-test/program'
    subprocess.run([str(executable)], check=True, timeout=15, cwd=CACHE)


if __name__ == '__main__':
    main()
