#!/usr/bin/env python3
"""Run against an isolated SDR++ with two Radio instances and --http enabled.

Example: python3 tests/brown_audio_sink/live.py http://localhost:8080 Radio 'Radio 2'
The selected sinks are restored afterward. No RF playback is required.
"""
import json
import sys
import urllib.request

base, first, second = sys.argv[1:]
def request(path, data=None):
    body = None if data is None else json.dumps(data).encode()
    req = urllib.request.Request(base + path, body, {'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=15) as response:
        return json.load(response)

def select(stream, sink):
    result = request('/sink/select', dict(stream=stream, sink=sink))
    assert result.get('status') == 'ok', result

streams = {s['name']: s['sink'] for s in request('/streams')['streams']}
assert first in streams and second in streams
try:
    for _ in range(10):
        for name, sink in [(first, 'Brown Audio'), (second, 'Brown Audio'),
                           (first, 'None'), (first, 'Brown Audio'),
                           (second, 'None'), (first, 'None')]:
            select(name, sink)
    assert request('/status')['mainLoopStarted']
    assert 'error' in request('/sink/select', dict(stream=first, sink='nonexistent-test-sink'))
    assert 'error' in request('/sink/select', dict(stream='nonexistent-test-stream', sink='None'))
    print('60 live sink transitions and invalid selections passed')
finally:
    select(first, streams[first])
    select(second, streams[second])
