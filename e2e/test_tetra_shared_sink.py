#!/usr/bin/env python3
"""
E2E Test: Issue #106 regressions & fixes

Covers:
1. CLI `-c` flag works as a no-op on macOS/Linux without "Unknown argument".
2. No `Glfw Error 65548` from `glfwSetWindowIcon` on macOS.
3. With both `Radio` and `TETRA Demodulator` assigned to the SAME audio sink
   (`Audio`, `New Audio`, `CoreAudio` on macOS, `NullAudioSink`), pressing Play
   does not freeze the waterfall after row 1 (`/proc/waterfall/fft_count` keeps
   increasing) or hang audio/stop.
"""

import os
import sys
import time

from e2e_common import (
    SDRPPTestContext,
    STATS_MODE,
    assert_response_ok,
    get_base_config,
    http_get,
    http_post,
    stats,
    wait_for_playing,
)


def get_fft_count(base_url):
    resp = http_get(base_url, "/proc/waterfall/fft_count")
    if isinstance(resp, dict) and "error" in resp:
        return None
    try:
        return int(resp)
    except (TypeError, ValueError):
        return None


def wait_for_stopped(base_url, timeout=5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        st = http_get(base_url, "/sdr/status")
        if st.get("playing") is False:
            return True
        time.sleep(0.1)
    return False


def test_tetra_shared_sink():
    stats.test_start("test_tetra_shared_sink")

    test_file = os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "recordings", "tetra_sample.wav"
    )
    if not os.path.exists(test_file):
        stats.test_fail("test_tetra_shared_sink", f"Test file not found: {test_file}")
        return False

    sink_cases = [
        ("NullAudioSink", "NullAudioSink", "null_audio_sink"),
        ("Audio", "Audio Sink", "audio_sink"),
        ("New Audio", "New Audio Sink", "new_portaudio_sink"),
    ]
    if sys.platform == "darwin":
        sink_cases.append(("CoreAudio", "CoreAudio Sink", "macos_coreaudio_sink"))

    for idx, (sink_name, inst_name, mod_name) in enumerate(sink_cases):
        stats.subsection(f"Shared sink '{sink_name}' (Radio + TETRA Demodulator)")
        cfg = get_base_config()
        cfg["frequency"] = 0.0
        cfg["sampleRate"] = 32000.0
        cfg["moduleInstances"][inst_name] = {"module": mod_name, "enabled": True}
        cfg["moduleInstances"]["TETRA Demodulator"] = {
            "module": "ch_tetra_demodulator",
            "enabled": True,
        }
        cfg["moduleInstances"]["File Source"] = {
            "module": "file_source",
            "enabled": True,
        }
        cfg["streams"]["Radio"] = {"muted": False, "sink": sink_name, "volume": 1.0}
        cfg["streams"]["TETRA Demodulator"] = {
            "muted": False,
            "sink": sink_name,
            "volume": 1.0,
        }

        with SDRPPTestContext() as ctx:
            ctx.write_configs(cfg)
            extra = ["-c"] if idx == 0 else None
            if not ctx.start(extra_args=extra):
                stats.test_fail(
                    f"test_tetra_shared_sink_{sink_name}",
                    f"Failed to start SDR++ (extra_args={extra})",
                )
                return False

            if sys.platform == "darwin" and idx == 0:
                err_match = ctx.find_in_log("Glfw Error 65548")
                if err_match:
                    stats.test_fail(
                        "test_glfw_no_65548_macos",
                        f"Unexpected Cocoa window icon error: {err_match}",
                    )
                    return False
                stats.test_pass("test_cli_c_and_no_glfw_65548")

            ctx.sleep(0.3)
            http_post(ctx.base_url, "/proc/source/type", "File")
            ctx.sleep(0.2)
            resp = ctx.module_cmd("File Source", "set_filename", test_file)
            ok, msg = assert_response_ok(resp, "set_filename")
            if not ok:
                stats.test_fail(f"test_tetra_shared_sink_{sink_name}", msg)
                return False

            streams = http_get(ctx.base_url, "/streams").get("streams", [])
            stream_map = {s["name"]: s["sink"] for s in streams}
            if (
                stream_map.get("Radio") != sink_name
                or stream_map.get("TETRA Demodulator") != sink_name
            ):
                stats.test_fail(
                    f"test_tetra_shared_sink_{sink_name}",
                    f"Expected both streams on '{sink_name}', got {stream_map}",
                )
                return False

            # Press Play and ensure DSP + waterfall keep running
            resp = http_get(ctx.base_url, "/sdr/start")
            if resp.get("action") != "sdr_start":
                stats.test_fail(
                    f"test_tetra_shared_sink_{sink_name}",
                    f"Unexpected /sdr/start response: {resp}",
                )
                return False

            if not wait_for_playing(ctx, 10.0):
                stats.test_fail(
                    f"test_tetra_shared_sink_{sink_name}",
                    "SDR++ did not reach playing state after /sdr/start",
                )
                return False

            ctx.sleep(0.5)
            fft1 = get_fft_count(ctx.base_url)
            if fft1 is None:
                stats.test_fail(
                    f"test_tetra_shared_sink_{sink_name}",
                    "Missing /proc/waterfall/fft_count endpoint",
                )
                return False

            samples1 = None
            if sink_name == "NullAudioSink":
                samples1 = ctx.module_cmd("NullAudioSink", "get_samples").get(
                    "samples", 0
                )

            ctx.sleep(1.0)
            fft2 = get_fft_count(ctx.base_url)
            if fft2 is None or fft2 <= fft1 + 3 or fft2 <= 5:
                stats.test_fail(
                    f"test_tetra_shared_sink_{sink_name}",
                    f"Waterfall stalled with shared sink '{sink_name}': fft1={fft1}, fft2={fft2}",
                )
                return False

            if sink_name == "NullAudioSink":
                samples2 = ctx.module_cmd("NullAudioSink", "get_samples").get(
                    "samples", 0
                )
                if samples2 <= samples1:
                    stats.test_fail(
                        f"test_tetra_shared_sink_{sink_name}",
                        f"Audio stream stalled on NullAudioSink: {samples1} -> {samples2}",
                    )
                    return False

            tetra_st = ctx.module_cmd("TETRA Demodulator", "get_status")
            ok, msg = assert_response_ok(tetra_st, "TETRA get_status")
            if not ok:
                stats.test_fail(f"test_tetra_shared_sink_{sink_name}", msg)
                return False

            # Press Stop and ensure clean stop without hang
            http_get(ctx.base_url, "/sdr/stop")
            if not wait_for_stopped(ctx.base_url, 5.0):
                stats.test_fail(
                    f"test_tetra_shared_sink_{sink_name}",
                    "SDR++ failed to stop cleanly after /sdr/stop",
                )
                return False

            stats.test_pass(
                f"test_tetra_shared_sink_{sink_name}",
                f"fft_count {fft1}->{fft2}, stop clean",
            )

    stats.test_pass("test_tetra_shared_sink", "All shared-sink checks passed")
    return True


if __name__ == "__main__":
    if not STATS_MODE:
        stats.verbose = True
        stats.section("Testing Issue #106: Radio + TETRA shared audio sink & macOS fixes")

    result = test_tetra_shared_sink()
    stats.final_summary(1, 1 if result else 0, 0 if result else 1)
    sys.exit(0 if result else 1)
