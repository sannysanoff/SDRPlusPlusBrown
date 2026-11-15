#!/usr/bin/env python3
"""Check queued module controls and nonblocking DMR waits on an isolated app.
Requires Radio and Extra V/UHF instances. Run with the app's debug base URL.
"""
import concurrent.futures
import json
import sys
import time
import urllib.request

base = sys.argv[1]
def request(path, data=None):
    req = urllib.request.Request(base + path, None if data is None else json.dumps(data).encode(),
                                 {'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=15) as response:
        return json.load(response)

def command(cmd, args=''):
    return request('/module/Radio/command', dict(cmd=cmd, args=args))

original_enabled = request('/module/Radio/enabled')['enabled']
original_mode = command('get_demod')['demod']
try:
    for _ in range(5):
        assert request('/module/Radio/disable')['enabled'] is False
        assert request('/module/Radio/enabled')['enabled'] is False
        assert request('/module/Radio/enable')['enabled'] is True
        assert 'error' not in command('get_demod')
    assert 'error' in request('/module/Missing/enable')
    assert 'error' in request('/module/Missing/command', dict(cmd='get_demod'))
    assert command('set_demod', 'DSD')['status'] == 'ok'
    with concurrent.futures.ThreadPoolExecutor() as pool:
        pending = pool.submit(request, '/module/Extra%20V%2FUHF/command',
                              dict(cmd='wait_dmr_sync_voice', args='Radio,5000,3000'))
        time.sleep(0.15)
        assert request('/module/Radio/enabled')['enabled']
        assert not pending.done(), 'UI command waited for the blocking DMR job'
        # Destroy the DSD instance while its static, read-only wait is running.
        assert command('set_demod', 'NFM')['status'] == 'ok'
        assert pending.result()['available'] is False
    print('Module controls, missing instances and concurrent background wait passed')
finally:
    command('set_demod', original_mode)
    request('/module/Radio/enable' if original_enabled else '/module/Radio/disable')
