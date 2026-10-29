#!/usr/bin/env python3
"""
E2E Test: Vowel Squelch (radio module)

The radio module's vowel squelch passes the demodulated audio only while it sounds like a
spoken vowel. It is driven here with two 48 kHz IQ fixtures through the File Source:

    recordings/vowel_squelch_ssb.wav    noise, then an SSB contest station, then noise
    recordings/vowel_squelch_noise.wav  the same bandwidth and level, no signal at all

Both were cut from /Users/san/recordings/baseband_14174296Hz_11-10-09_24-02-2024-contest-ssb.wav
(the station at +47.9 kHz, shifted to 0 Hz, upper sideband only, decimated to 48 kHz).

The test samples http://127.0.0.1:PORT/proc/radio/Radio/vowel_squelch_open while the file plays and
checks the two things that matter: the gate opens on the station (a good part of the time, and it does
visit both states) and it never opens on noise of the same level and bandwidth.

Note on timing: with the null audio sink the audio chain runs as fast as it can, not in real time (the
7.5 s fixture is consumed in about a quarter of a second of wall clock), so a sample cannot be tied to a
position in the file. Both fixtures loop, so the samples are effectively drawn from the whole recording,
which is what makes a distribution check the right tool here. The time-accurate behaviour (250 ms buffer,
1 s hangover) is covered by the detector's own measurements, not by this test.

Environment Variables:
    E2E_VERBOSE=1      - Enable verbose output
    E2E_HTTP_PORT=NNNN - Use a specific HTTP port
"""

import json
import os
import sys
import time

from e2e_common import (
    SDRPPTestContext, get_base_config, stats, http_get, http_post,
)

HERE = os.path.dirname(os.path.abspath(__file__))
SPEECH_FIXTURE = os.path.join(HERE, "recordings", "vowel_squelch_ssb.wav")
NOISE_FIXTURE = os.path.join(HERE, "recordings", "vowel_squelch_noise.wav")

PROC_PATH = "/proc/radio/Radio/vowel_squelch_open"


def build_config(fixture_path, enabled=True, decay=1.0, sensitivity=0.5):
    cfg = get_base_config()
    cfg["sampleRate"] = 48000.0
    cfg["frequency"] = 0.0            # the fixture has the station at 0 Hz
    cfg["source"] = "file_source"
    cfg["moduleInstances"]["File Source"] = {"module": "file_source", "enabled": True}
    # the radio module keeps its own configuration file
    radio_config = {
        "Radio": {
            "selectedDemodId": 4,     # USB
            "USB": {
                "bandwidth": 2700.0,
                "snapInterval": 100.0,
                "vowelSquelchEnabled": enabled,
                "vowelSquelchDecay": decay,
                "vowelSquelchSensitivity": sensitivity,
            },
        },
    }
    return cfg, radio_config


def squelch_is_open(base_url):
    resp = http_get(base_url, PROC_PATH)
    if isinstance(resp, bool):
        return resp
    if isinstance(resp, dict):
        if "error" in resp:
            return None
        for key in ("value", "vowel_squelch_open", "result"):
            if key in resp:
                return bool(resp[key])
    return None


def record_gate(ctx, seconds, interval=0.05):
    """Poll the squelch state and return [(t, open)] with t measured from the first sample."""
    samples = []
    t0 = time.time()
    while True:
        t = time.time() - t0
        if t > seconds:
            break
        state = squelch_is_open(ctx.base_url)
        if state is not None:
            samples.append((t, state))
        time.sleep(interval)
    return samples


def run_fixture(fixture_path, seconds, enabled=True):
    main_config, radio_config = build_config(fixture_path, enabled=enabled)
    with SDRPPTestContext() as ctx:
        ctx.write_configs(main_config, radio_config)
        # the file source keeps its own configuration, in its own file
        with open(os.path.join(ctx.temp_dir, "file_source_config.json"), "w") as f:
            json.dump({"path": fixture_path}, f)
        if not ctx.start():
            return None
        http_post(ctx.base_url, "/proc/source/type", "File")
        ctx.sleep(0.3)
        http_post(ctx.base_url, "/proc/source/filename", fixture_path)
        ctx.sleep(0.3)
        # start playback and begin polling at once: the sample times then follow the file, not the app
        http_get(ctx.base_url, "/sdr/start")

        # the endpoint must exist at all, otherwise the test would pass for the wrong reason
        probe = http_get(ctx.base_url, PROC_PATH)
        if isinstance(probe, dict) and "error" in probe:
            stats.test_fail("test_vowel_squelch", f"procfs endpoint missing: {probe['error']}")
            return None

        return record_gate(ctx, seconds)


def test_vowel_squelch():
    stats.test_start("test_vowel_squelch")

    for path in (SPEECH_FIXTURE, NOISE_FIXTURE):
        if not os.path.exists(path):
            stats.test_fail("test_vowel_squelch", f"fixture missing: {path}")
            return False

    # ---- 1. the SSB channel: shut in the noise, open on the voice, shut again after it
    stats.subsection("SSB channel fixture")
    samples = run_fixture(SPEECH_FIXTURE, seconds=8.0)
    if samples is None:
        stats.test_fail("test_vowel_squelch", "could not run the speech fixture")
        return False
    if not samples:
        stats.test_fail("test_vowel_squelch", "the squelch state was never readable")
        return False

    open_fraction = sum(1 for _, o in samples if o) / len(samples)
    stats.debug("gate samples (first 12)", [(round(t, 2), o) for t, o in samples[:12]])
    stats.info(f"gate open in {100 * open_fraction:.0f}% of {len(samples)} samples")

    if open_fraction <= 0.0:
        stats.test_fail("test_vowel_squelch", "the squelch never opened on the station")
        return False
    if open_fraction < 0.2:
        stats.test_fail("test_vowel_squelch", f"the squelch was open only {100 * open_fraction:.0f}% of the samples")
        return False
    if open_fraction > 0.95:
        stats.test_fail("test_vowel_squelch", f"the squelch stayed open {100 * open_fraction:.0f}% of the time, i.e. it never closed")
        return False

    # ---- 2. the same bandwidth, same level, nothing but noise: it must never open
    stats.subsection("noise fixture")
    samples = run_fixture(NOISE_FIXTURE, seconds=6.0)
    if samples is None:
        stats.test_fail("test_vowel_squelch", "could not run the noise fixture")
        return False
    opened = [(t, o) for t, o in samples if o]
    stats.info(f"{len(opened)} open samples of {len(samples)} on noise")
    if opened:
        stats.test_fail("test_vowel_squelch", f"the squelch opened on noise at {opened[:5]}")
        return False

    stats.test_pass("test_vowel_squelch", "shut on noise, open on the voice, shut after the decay")
    return True


def main():
    ok = test_vowel_squelch()
    stats.test_end("test_vowel_squelch")
    stats.final_summary(1, 1 if ok else 0, 0 if ok else 1)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())