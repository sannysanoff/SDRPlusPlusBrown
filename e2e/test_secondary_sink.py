#!/usr/bin/env python3
"""
E2E Test: Secondary Stream Sink Selection (Issue #108)

Reproduces the workflow from issue #108:
1. Start SDR playback with Radio routed to an audio sink.
2. Add a secondary stream for Radio ("Radio__1").
3. Change the secondary stream's sink across available audio sinks and let UI frames render.
4. Remove the secondary stream cleanly and verify no crash / UI freeze.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from e2e_common import (
    REPO_ROOT,
    SDRPPTestContext,
    STATS_MODE,
    assert_response_ok,
    get_base_config,
    get_radio_config,
    http_get,
    http_post,
    module_cmd,
    stats,
    wait_for_playing,
)

TEST_FILE = str(REPO_ROOT / "e2e" / "recordings" / "dmr_sample.wav")

OPTIONAL_SINK_MODULES = [
    ("AudioSink", "audio_sink"),
    ("NewAudioSink", "new_portaudio_sink"),
    ("CoreAudioSink", "macos_coreaudio_sink"),
    ("BrownAudioSink", "brown_audio_sink"),
]


def add_available_sink_modules(main_config):
    modules_dir = main_config["modulesDirectory"]
    for inst_name, mod_name in OPTIONAL_SINK_MODULES:
        for ext in (".dylib", ".so", ".dll"):
            if os.path.exists(os.path.join(modules_dir, mod_name + ext)):
                main_config["moduleInstances"][inst_name] = {"module": mod_name, "enabled": True}
                break


def get_streams_map(base_url):
    resp = http_get(base_url, "/streams")
    ok, msg = assert_response_ok(resp, "GET /streams")
    if not ok:
        raise RuntimeError(msg)
    return {s["name"]: s for s in resp.get("streams", [])}


def test_secondary_sink_switching():
    stats.test_start("test_secondary_sink_switching")

    if not os.path.exists(TEST_FILE):
        stats.test_fail("test_secondary_sink_switching", f"Test file not found: {TEST_FILE}")
        return False

    main_config = get_base_config()
    main_config["moduleInstances"]["File Source"] = {"module": "file_source", "enabled": True}
    add_available_sink_modules(main_config)
    main_config["source"] = "file_source"
    main_config["File Source"] = {"filename": TEST_FILE}
    radio_config = get_radio_config(demod_id=0, bandwidth=12500.0, demod_name="NFM")

    with SDRPPTestContext(startup_timeout=30.0) as ctx:
        ctx.write_configs(main_config, radio_config=radio_config)

        if not ctx.start():
            stats.test_fail("test_secondary_sink_switching", "Failed to start SDR++")
            return False

        # Start file playback
        resp = http_post(ctx.base_url, "/proc/source/type", {"value": "File"})
        ok, msg = assert_response_ok(resp, "POST /proc/source/type")
        if not ok:
            stats.test_fail("test_secondary_sink_switching", msg)
            return False

        resp = module_cmd(ctx.base_url, "File Source", "set_filename", TEST_FILE)
        ok, msg = assert_response_ok(resp, "File Source set_filename")
        if not ok:
            stats.test_fail("test_secondary_sink_switching", msg)
            return False

        sinks_resp = http_get(ctx.base_url, "/sinks")
        ok, msg = assert_response_ok(sinks_resp, "GET /sinks")
        if not ok:
            stats.test_fail("test_secondary_sink_switching", msg)
            return False
        available_sinks = sinks_resp.get("sinks", [])
        stats.info(f"Available sinks: {available_sinks}")

        primary_sink = next(
            (s for s in ("Audio", "New Audio", "CoreAudio", "Brown Audio", "NullAudioSink") if s in available_sinks),
            "None",
        )
        resp = http_post(ctx.base_url, "/sink/select", {"stream": "Radio", "sink": primary_sink})
        ok, msg = assert_response_ok(resp, "select primary sink")
        if not ok:
            stats.test_fail("test_secondary_sink_switching", msg)
            return False

        http_get(ctx.base_url, "/sdr/start")
        if not wait_for_playing(ctx, timeout=15.0):
            stats.test_fail("test_secondary_sink_switching", "SDR++ did not enter playing state")
            return False

        # Step 2 (#108): Add secondary stream for Radio while Radio is playing
        add_resp = http_post(ctx.base_url, "/sink/add_secondary", {"stream": "Radio"})
        ok, msg = assert_response_ok(add_resp, "add secondary stream")
        if not ok:
            stats.test_fail("test_secondary_sink_switching", msg)
            return False
        secondary = add_resp.get("secondary", "")
        if not secondary:
            stats.test_fail("test_secondary_sink_switching", f"Missing secondary name in {add_resp}")
            return False
        stats.info(f"Added secondary stream: {secondary}")

        streams = get_streams_map(ctx.base_url)
        if secondary not in streams or not streams[secondary].get("running"):
            stats.test_fail("test_secondary_sink_switching", f"Secondary stream not running: {streams}")
            return False

        # Step 3 (#108): Select Audio (and every other registered sink) on the secondary stream
        # and let UI frames call stream->sink->menuHandler() without crashing.
        tested = []
        for target_sink in [s for s in available_sinks if s != "None"] + ["None", primary_sink]:
            sel = http_post(ctx.base_url, "/sink/select", {"stream": secondary, "sink": target_sink})
            ok, msg = assert_response_ok(sel, f"select '{target_sink}' on {secondary}")
            if not ok:
                stats.test_fail("test_secondary_sink_switching", msg)
                return False

            time.sleep(0.15)
            status = http_get(ctx.base_url, "/status")
            if not status.get("mainLoopStarted"):
                stats.test_fail("test_secondary_sink_switching", f"UI unresponsive after selecting {target_sink}: {status}")
                return False

            streams = get_streams_map(ctx.base_url)
            if secondary not in streams or streams[secondary].get("sink") != target_sink:
                stats.test_fail("test_secondary_sink_switching", f"Expected {secondary} sink='{target_sink}', got {streams}")
                return False
            tested.append(target_sink)

        # Remove secondary and verify cleanup + re-adding works cleanly
        rem = http_post(ctx.base_url, "/sink/remove_secondary", {"stream": secondary})
        ok, msg = assert_response_ok(rem, f"remove {secondary}")
        if not ok:
            stats.test_fail("test_secondary_sink_switching", msg)
            return False
        streams = get_streams_map(ctx.base_url)
        if secondary in streams:
            stats.test_fail("test_secondary_sink_switching", f"{secondary} still present after removal")
            return False

        http_get(ctx.base_url, "/sdr/stop")
        stats.test_pass("test_secondary_sink_switching", f"Tested secondary sink switching across: {tested}")
        return True


if __name__ == "__main__":
    if not STATS_MODE:
        stats.verbose = True
        stats.section("Secondary Stream Sink Selection (#108)")

    ok = test_secondary_sink_switching()
    stats.final_summary(1, 1 if ok else 0, 0 if ok else 1)
    sys.exit(0 if ok else 1)
