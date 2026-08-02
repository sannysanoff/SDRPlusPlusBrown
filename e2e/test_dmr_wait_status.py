#!/usr/bin/env python3

import os
import sys

from e2e_common import SDRPPTestContext, get_base_config, http_get, http_post, stats, STATS_MODE, wait_for_playing


TEST_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "recordings", "dmr_sample.wav")
TEST_FREQ = 0.0
DMR_OFFSET_HZ = 0


def test_wait_for_dmr_voice_status():
    stats.test_start("test_wait_for_dmr_voice_status")

    if not os.path.exists(TEST_FILE):
        stats.test_fail("test_wait_for_dmr_voice_status", f"missing test file: {TEST_FILE}")
        return False

    main_config = get_base_config()
    main_config["frequency"] = TEST_FREQ
    main_config["sampleRate"] = 16000.0
    main_config["source"] = "file_source"
    main_config["moduleInstances"]["File Source"] = {"module": "file_source", "enabled": True}
    main_config["moduleInstances"]["Recorder"] = {"module": "recorder", "enabled": True}
    main_config["moduleInstances"]["Extra V/UHF"] = {"module": "ch_extravhf_decoder", "enabled": True}

    recorder_config = {
        "Recorder": {
            "mode": 1,
            "audioStream": "Radio",
            "audioVolume": 1.0,
            "stereo": True,
            "ignoreSilence": False,
        }
    }

    with SDRPPTestContext(startup_timeout=30.0) as ctx:
        ctx.write_configs(main_config, recorder_config=recorder_config)

        if not ctx.start():
            stats.test_fail("test_wait_for_dmr_voice_status", "failed to start SDR++")
            return False

        resp = ctx.module_cmd("File Source", "set_filename", TEST_FILE)
        if "error" in resp:
            stats.test_fail("test_wait_for_dmr_voice_status", f"set_filename failed: {resp}")
            return False

        resp = ctx.module_cmd("Radio", "set_demod", "DSD")
        if "error" in resp:
            stats.test_fail("test_wait_for_dmr_voice_status", f"set_demod failed: {resp}")
            return False

        resp = http_get(ctx.base_url, f"/vfo/set_offset?name=Radio&offset={DMR_OFFSET_HZ}")
        if resp.get("status") != "ok":
            stats.test_fail("test_wait_for_dmr_voice_status", f"vfo offset failed: {resp}")
            return False

        resp = http_post(ctx.base_url, "/sink/select", {"stream": "Radio", "sink": "NullAudioSink"})
        if resp.get("status") != "ok":
            stats.test_fail("test_wait_for_dmr_voice_status", f"sink select failed: {resp}")
            return False

        resp = http_get(ctx.base_url, "/sdr/start")
        if resp.get("action") != "sdr_start":
            stats.test_fail("test_wait_for_dmr_voice_status", f"sdr start failed: {resp}")
            return False

        if not wait_for_playing(ctx, 30.0):
            stats.test_fail("test_wait_for_dmr_voice_status", "SDR++ did not reach playing state within 30s")
            return False

        resp = ctx.module_cmd("Extra V/UHF", "wait_dmr_sync_voice", "Radio,2000,10000", timeout=15.0)
        if resp.get("status") != "ok":
            stats.test_fail("test_wait_for_dmr_voice_status", f"wait_dmr_sync_voice failed: {resp}")
            return False

        stats.test_pass("test_wait_for_dmr_voice_status", "DMR sync/voice became stable")
        return True


if __name__ == "__main__":
    if not STATS_MODE:
        stats.verbose = True
        stats.section("DMR stable sync/voice wait")

    result = test_wait_for_dmr_voice_status()
    stats.final_summary(1, 1 if result else 0, 0 if result else 1)
    sys.exit(0 if result else 1)
