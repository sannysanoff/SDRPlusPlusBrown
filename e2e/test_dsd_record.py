#!/usr/bin/env python3
"""
E2E Test: DSD Demodulator + File Source + Record

1. Load DMR baseband via File Source
2. Start playback
3. Detect carrier (NullAudioSink samples flowing)
4. Start recording via Recorder HTTP API (handleDebugCommand)
5. Verify no crash throughout
"""

import sys
import os
import time
import json
from e2e_common import (
    SDRPPTestContext, get_base_config, stats, STATS_MODE,
    assert_response_ok, http_post
)


TEST_FILE = "/Users/san/recordings/baseband_144553405Hz_17-40-40_15-05-2024---tarlink--dmr---.wav"
CARRIER_FREQ = 144553405.0
SAMPLE_RATE = 752000.0


def test_dsd_file_play_record():
    stats.test_start("test_dsd_file_play_record")

    if not os.path.exists(TEST_FILE):
        stats.test_fail("test_dsd_file_play_record", f"Test file: {TEST_FILE} not found")
        return False

    main_config = get_base_config()
    main_config["moduleInstances"]["File Source"] = {"module": "file_source", "enabled": True}
    main_config["moduleInstances"]["DSD Demodulator"] = {"module": "dsdcc_decoder", "enabled": False}
    main_config["moduleInstances"]["Recorder"] = {"module": "recorder", "enabled": True}
    main_config["source"] = "file_source"
    main_config["File Source"] = {"filename": TEST_FILE}
    main_config["frequency"] = CARRIER_FREQ
    main_config["sampleRate"] = SAMPLE_RATE

    with SDRPPTestContext() as ctx:
        ctx.write_configs(main_config)

        if not ctx.start():
            stats.test_fail("test_dsd_file_play_record", "Failed to start SDR++")
            return False
        stats.info("SDR++ started")

        # Route Radio audio to NullAudioSink for carrier detection
        resp = ctx.module_cmd("NullAudioSink", "select", "Radio")
        stats.debug("NullAudioSink select", resp)
        resp = http_post(ctx.base_url, "/sink/select",
                        {"stream": "Radio", "sink": "NullAudioSink"})
        stats.debug("sink/select", resp)
        ctx.sleep(0.5)

        # Start playback
        resp = http_post(ctx.base_url, "/sdr/start")
        ctx.sleep(2.0)
        resp = http_post(ctx.base_url, "/status")
        stats.debug("status after play", resp)
        if not resp.get("ready"):
            stats.test_fail("test_dsd_file_play_record", "Crashed on play")
            return False
        stats.info("Playback started")

        # Detect carrier (samples flowing confirms audio pipeline active)
        resp = ctx.module_cmd("NullAudioSink", "get_samples")
        if "error" in resp:
            stats.test_fail("test_dsd_file_play_record", f"NullAudioSink: {resp['error']}")
            return False
        s1 = resp.get("samples", 0)
        ctx.sleep(0.5)
        resp = ctx.module_cmd("NullAudioSink", "get_samples")
        s2 = resp.get("samples", 0)
        if s2 <= s1:
            stats.info("No carrier detected (samples not flowing) — continuing test")
        else:
            stats.info(f"Carrier detected (samples: {s1} -> {s2})")

        # Enable DSD
        resp = ctx.module_cmd("DSD Demodulator", "enable", "1")
        stats.debug("DSD enable", resp)
        ctx.sleep(1.0)
        resp = http_post(ctx.base_url, "/status")
        if not resp.get("ready"):
            stats.test_fail("test_dsd_file_play_record", "Crashed on DSD enable")
            return False
        stats.info("DSD enabled")

        # Start recording
        resp = ctx.module_cmd("Recorder", "start")
        stats.debug("Recorder start", resp)
        ctx.sleep(1.0)
        resp = ctx.module_cmd("Recorder", "status")
        stats.debug("Recorder status", resp)

        if not resp.get("recording"):
            stats.test_fail("test_dsd_file_play_record", "Recorder start failed")
            return False
        stats.info("Recording started")

        ctx.sleep(2.0)
        resp = http_post(ctx.base_url, "/status")
        if not resp.get("ready"):
            stats.test_fail("test_dsd_file_play_record", "Crashed during recording")
            return False

        # Stop recording
        resp = ctx.module_cmd("Recorder", "stop")
        stats.debug("Recorder stop", resp)
        ctx.sleep(0.3)
        resp = ctx.module_cmd("Recorder", "status")
        if resp.get("recording"):
            # Try again
            ctx.module_cmd("Recorder", "stop")
            ctx.sleep(0.3)

        stats.test_pass("test_dsd_file_play_record",
                       "Play → detect carrier → DSD → record: no crash")
        return True


if __name__ == "__main__":
    if not STATS_MODE:
        stats.verbose = True
        stats.section("DSD file play → detect carrier → record")

    r = test_dsd_file_play_record()
    stats.final_summary(1, 1 if r else 0, 0 if r else 1)
    sys.exit(0 if r else 1)
