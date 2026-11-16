#!/usr/bin/env python3
"""
E2E Test: Radiosonde Decoder Module (Issue #103)

Covers:
1. Default desktop config does NOT auto-load a radiosonde_decoder instance
   (left for manual addition via Module Manager), even though the plugin binary
   is built and installed.
2. When added via config (simulating Module Manager or Android default entry):
   - Initial disabled state ("enabled": false, matching Android default) loads cleanly
     and can be enabled/disabled at runtime via /module/<name>/enable|disable.
   - Radiosonde decoder config persists valid platform paths (no Unix backslash bug).
   - Playing a narrowed RS41 baseband IQ sample (`e2e/recordings/rs41_sample.wav`)
     decodes real RS41 packets (`packet_count > 0`, non-empty serial and telemetry).
   - Disabling/re-enabling while playing and stopping SDR complete cleanly without hangs
     or Debug `/tmp/*.data` dumps.
"""

import json
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


def _check_fresh_desktop_not_autoloaded():
    stats.subsection("Default desktop config does not auto-load radiosonde_decoder")
    with SDRPPTestContext() as ctx:
        cfg_path = os.path.join(ctx.temp_dir, "config.json")
        if os.path.exists(cfg_path):
            os.remove(cfg_path)

        base = get_base_config()
        minimal = {
            "modulesDirectory": base["modulesDirectory"],
            "resourcesDirectory": base["resourcesDirectory"],
        }
        with open(cfg_path, "w") as f:
            json.dump(minimal, f, indent=4)

        if not ctx.start():
            stats.test_fail("test_radiosonde_not_autoloaded", "Failed to start SDR++")
            return False

        modules = http_get(ctx.base_url, "/modules")
        for inst_name, info in modules.items():
            if info.get("module") == "radiosonde_decoder":
                stats.test_fail(
                    "test_radiosonde_not_autoloaded",
                    f"radiosonde_decoder should not auto-load on desktop, found instance '{inst_name}'",
                )
                return False

    stats.test_pass("test_radiosonde_not_autoloaded")
    return True


def _check_decode_and_lifecycle():
    stats.subsection("RS41 IQ decoding and enable/disable lifecycle")
    test_file = os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "recordings", "rs41_sample.wav"
    )
    if not os.path.exists(test_file):
        stats.test_fail("test_radiosonde_decode", f"Test file not found: {test_file}")
        return False

    cfg = get_base_config()
    cfg["frequency"] = 0.0
    cfg["sampleRate"] = 15625.0
    cfg["moduleInstances"]["File Source"] = {"module": "file_source", "enabled": True}
    cfg["moduleInstances"]["NullAudioSink"] = {"module": "null_audio_sink", "enabled": True}
    cfg["streams"]["Radio"] = {"muted": False, "sink": "NullAudioSink", "volume": 1.0}
    cfg["moduleInstances"]["Radiosonde Decoder"] = {
        "module": "radiosonde_decoder",
        "enabled": False,
    }

    with SDRPPTestContext() as ctx:
        ctx.write_configs(cfg)
        if not ctx.start():
            stats.test_fail("test_radiosonde_decode", "Failed to start SDR++")
            return False

        ctx.sleep(0.3)
        modules = http_get(ctx.base_url, "/modules")
        rs_inst = modules.get("Radiosonde Decoder")
        if not rs_inst or rs_inst.get("module") != "radiosonde_decoder":
            stats.test_fail("test_radiosonde_decode", f"Missing Radiosonde Decoder in /modules: {modules}")
            return False
        if rs_inst.get("enabled") is not False:
            stats.test_fail("test_radiosonde_decode", f"Expected initially disabled, got: {rs_inst}")
            return False

        # Enable at runtime
        resp = http_get(ctx.base_url, "/module/Radiosonde%20Decoder/enable")
        if not resp.get("enabled"):
            stats.test_fail("test_radiosonde_decode", f"Failed to enable Radiosonde Decoder: {resp}")
            return False

        # Start File Source playback and center Radiosonde Decoder VFO at 0 Hz
        http_post(ctx.base_url, "/proc/source/type", "File")
        ctx.sleep(0.2)
        resp = ctx.module_cmd("File Source", "set_filename", test_file)
        ok, msg = assert_response_ok(resp, "set_filename")
        if not ok:
            stats.test_fail("test_radiosonde_decode", msg)
            return False

        http_get(ctx.base_url, "/vfo/offset?name=Radiosonde%20Decoder&offset=0")
        http_get(ctx.base_url, "/sdr/start")
        if not wait_for_playing(ctx, timeout=5.0):
            stats.test_fail("test_radiosonde_decode", "SDR failed to start playing")
            return False

        # Wait up to 4 seconds for real RS41 packets to decode
        decoded = None
        deadline = time.time() + 4.0
        while time.time() < deadline:
            st = ctx.module_cmd("Radiosonde Decoder", "get_status")
            if st.get("packet_count", 0) > 0 and st.get("serial"):
                decoded = st
                break
            time.sleep(0.2)

        if not decoded:
            st = ctx.module_cmd("Radiosonde Decoder", "get_status")
            stats.test_fail("test_radiosonde_decode", f"No RS41 packets decoded from sample WAV: {st}")
            return False

        stats.info(
            f"Decoded RS41: serial={decoded.get('serial')} seq={decoded.get('seq')} "
            f"lat={decoded.get('lat'):.4f} lon={decoded.get('lon'):.4f} alt={decoded.get('alt'):.1f}m "
            f"packets={decoded.get('packet_count')}"
        )

        # Verify persisted config paths have valid platform separators (after 1s autosave)
        rs_cfg_path = os.path.join(ctx.temp_dir, "radiosonde_decoder_config.json")
        inst_cfg = {}
        cfg_deadline = time.time() + 2.0
        while time.time() < cfg_deadline:
            if os.path.exists(rs_cfg_path):
                try:
                    with open(rs_cfg_path, "r") as f:
                        rs_cfg = json.load(f)
                    if "Radiosonde Decoder" in rs_cfg:
                        inst_cfg = rs_cfg["Radiosonde Decoder"]
                        break
                except Exception:
                    pass
            time.sleep(0.1)

        gpx_path = inst_cfg.get("gpxPath", "")
        ptu_path = inst_cfg.get("ptuPath", "")
        if not gpx_path or not ptu_path:
            stats.test_fail("test_radiosonde_decode", f"Missing default paths in config: {inst_cfg}")
            return False
        if sys.platform != "win32" and ("\\" in gpx_path or "\\" in ptu_path):
            stats.test_fail("test_radiosonde_decode", f"Invalid backslash in POSIX temp path: {inst_cfg}")
            return False

        # Toggle disable while playing, then re-enable while playing, then stop
        resp = http_get(ctx.base_url, "/module/Radiosonde%20Decoder/disable")
        if resp.get("enabled") is not False:
            stats.test_fail("test_radiosonde_decode", f"Failed to disable while playing: {resp}")
            return False
        ctx.sleep(0.2)

        resp = http_get(ctx.base_url, "/module/Radiosonde%20Decoder/enable")
        if resp.get("enabled") is not True:
            stats.test_fail("test_radiosonde_decode", f"Failed to re-enable while playing: {resp}")
            return False
        ctx.sleep(0.2)

        stop_resp = http_get(ctx.base_url, "/sdr/stop")
        if not isinstance(stop_resp, dict) or stop_resp.get("action") != "sdr_stop":
            stats.test_fail("test_radiosonde_decode", f"Unexpected /sdr/stop response: {stop_resp}")
            return False

    for stray in ("/tmp/rs41frames.data", "/tmp/afsk.txt", "/tmp/gfsk.data"):
        if os.path.exists(stray):
            stats.test_fail("test_radiosonde_decode", f"Unexpected debug file created: {stray}")
            return False

    stats.test_pass("test_radiosonde_decode")
    return True


def test_radiosonde_decoder():
    stats.test_start("test_radiosonde_decoder")
    if not _check_fresh_desktop_not_autoloaded():
        return False
    if not _check_decode_and_lifecycle():
        return False
    stats.test_pass("test_radiosonde_decoder")
    return True


if __name__ == "__main__":
    if not STATS_MODE:
        stats.verbose = True
        stats.section("Testing Issue #103: Radiosonde Decoder")

    result = test_radiosonde_decoder()
    stats.final_summary(1, 1 if result else 0, 0 if result else 1)
    sys.exit(0 if result else 1)
