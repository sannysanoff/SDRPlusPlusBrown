#!/usr/bin/env python3
"""Exercise shared capture with fake hardware and the real DSP stream/splitter."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[2]
volk = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'volk'], text=True))
with tempfile.TemporaryDirectory(prefix='brown-mic-test-') as tmp:
    for name, version in (("shared_microphone", 5), ("shared_microphone", 6), ("output_buffer", 6), ("sink_config", 6)):
        binary = str(Path(tmp) / f'{name}-v{version}')
        subprocess.run([
            os.environ.get('CXX', 'c++'), '-std=c++17', '-pthread',
            *shlex.split(os.environ.get('CXXFLAGS', '')),
            f'-DRTAUDIO_VERSION_MAJOR={version}',
            '-Itests/brown_audio_sink/stubs', '-Icore/src', '-Icore/src/imgui',
            f'tests/brown_audio_sink/{name}.cpp', *volk, '-o', binary,
        ], cwd=repo, check=True)
        subprocess.run([binary], check=True, timeout=10)

    watch_bin = str(Path(tmp) / 'coreaudio_period_watch')
    watch_flags = []
    if sys.platform == 'darwin':
        rtaudio = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'rtaudio'], text=True))
        watch_flags = [*rtaudio, '-framework', 'CoreAudio', '-framework', 'CoreFoundation']
    subprocess.run([
        os.environ.get('CXX', 'c++'), '-std=c++17', '-pthread',
        *shlex.split(os.environ.get('CXXFLAGS', '')),
        'tests/brown_audio_sink/coreaudio_period_watch.cpp', *watch_flags, '-o', watch_bin,
    ], cwd=repo, check=True)
    subprocess.run([watch_bin], check=True, timeout=10)
