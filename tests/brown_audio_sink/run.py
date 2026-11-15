#!/usr/bin/env python3
"""Exercise shared capture with fake hardware and the real DSP stream/splitter."""
import os
from pathlib import Path
import shlex
import subprocess
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
