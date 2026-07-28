#!/usr/bin/env python3
"""
E2E Test: DSD Demodulator + Record crash

Tests that enabling DSD demodulator and starting playback/recording doesn't
crash SDR++. Uses a baseband recording with File Source, matching pattern
from test_tetra_demodulator.py.

Reproduces: Enable DSD demodulator -> press Record -> SDR++ crashes.

Root cause: DSD module started uninitialized DSP blocks (resamp, reshape)
whose worker threads dereference null _in pointers.

Environment Variables:
    E2E_VERBOSE=1      - Enable verbose output
    E2E_HTTP_PORT=NNNN - Use specific HTTP port
    E2E_TEST_FILE      - Path to test recording
"""

import sys
import os
import time
from e2e_common import (
    SDRPPTestContext, get_base_config, stats, STATS_MODE,
    assert_response_ok, http_post
)


TEST_FILE = "/Users/san/recordings/baseband_144553405Hz_17-40-40_15-05-2024---tarlink--dmr---.wav"
CARRIER_FREQ = 144553405.0
SAMPLE_RATE = 752000.0


def test_dsd_enable_with_file_source():
    """Load baseband file, enable DSD, start playback, verify no crash."""
    stats.test_start("test_dsd_enable_with_file_source")

    if not os.path.exists(TEST_FILE):
        stats.test_fail("test_dsd_enable_with_file_source",
                       f"Test file not found: {TEST_FILE}")
        return False

    stats.info(f"Using test file: {TEST_FILE}")

    # Base config
    main_config = get_base_config()

    # Add File Source
    main_config["moduleInstances"]["File Source"] = {
        "module": "file_source",
        "enabled": True
    }
    main_config["source"] = "file_source"
    main_config["File Source"] = {"filename": TEST_FILE}

    # Add DSD module (disabled initially)
    main_config["moduleInstances"]["DSD Demodulator"] = {
        "module": "dsdcc_decoder",
        "enabled": False
    }

    # Match recording params
    main_config["frequency"] = CARRIER_FREQ
    main_config["sampleRate"] = SAMPLE_RATE

    with SDRPPTestContext() as ctx:
        ctx.write_configs(main_config)

        if not ctx.start():
            stats.test_fail("test_dsd_enable_with_file_source",
                          "Failed to start SDR++")
            return False

        stats.info("SDR++ started")

        # Verify File Source loaded
        resp = ctx.module_cmd("File Source", "get_filename")
        stats.debug("File Source", resp)
        ok, msg = assert_response_ok(resp, "get_filename")
        if not ok:
            stats.test_fail("test_dsd_enable_with_file_source",
                          f"File Source error: {msg}")
            return False

        # Enable DSD module via HTTP
        resp = ctx.module_cmd("DSD Demodulator", "enable", "1")
        stats.debug("DSD enable", resp)
        ctx.sleep(0.5)

        # Check alive after DSD enable
        resp = http_post(ctx.base_url, "/status")
        if not resp.get("ready"):
            stats.test_fail("test_dsd_enable_with_file_source",
                          "Process crashed after DSD enable")
            return False
        stats.info("DSD module enabled, process alive")

        # Start SDR playback (file source -> VFO -> DSD chain)
        resp = http_post(ctx.base_url, "/sdr/start")
        stats.debug("SDR start", resp)
        ctx.sleep(1.0)

        # Verify process survives playback
        resp = http_post(ctx.base_url, "/status")
        if not resp.get("ready"):
            stats.test_fail("test_dsd_enable_with_file_source",
                          "Process crashed during playback")
            return False
        stats.info("Playback started, process alive")

        stats.test_pass("test_dsd_enable_with_file_source",
                       "DSD + File Source playback: no crash")
        return True


def test_dsd_reenable():
    """Enable DSD, disable, re-enable - verify no crash."""
    stats.test_start("test_dsd_reenable")

    main_config = get_base_config()
    main_config["moduleInstances"]["DSD Demodulator"] = {
        "module": "dsdcc_decoder",
        "enabled": False
    }

    with SDRPPTestContext() as ctx:
        ctx.write_configs(main_config)
        if not ctx.start():
            stats.test_fail("test_dsd_reenable", "Failed to start SDR++")
            return False

        # Enable
        ctx.module_cmd("DSD Demodulator", "enable", "1")
        ctx.sleep(0.3)
        resp = http_post(ctx.base_url, "/status")
        if not resp.get("ready"):
            stats.test_fail("test_dsd_reenable", "Crashed on enable")
            return False

        # Disable
        ctx.module_cmd("DSD Demodulator", "disable", "1")
        ctx.sleep(0.3)
        resp = http_post(ctx.base_url, "/status")
        if not resp.get("ready"):
            stats.test_fail("test_dsd_reenable", "Crashed on disable")
            return False

        # Re-enable
        ctx.module_cmd("DSD Demodulator", "enable", "1")
        ctx.sleep(0.3)
        resp = http_post(ctx.base_url, "/status")
        if not resp.get("ready"):
            stats.test_fail("test_dsd_reenable", "Crashed on re-enable")
            return False

        stats.test_pass("test_dsd_reenable",
                       "DSD enable/disable/re-enable: no crash")
        return True


if __name__ == "__main__":
    if not STATS_MODE:
        stats.verbose = True
        stats.section("Testing DSD demodulator + record crash")

    r1 = test_dsd_enable_with_file_source()
    r2 = test_dsd_reenable()

    passed = sum([r1, r2])
    failed = 2 - passed
    stats.final_summary(2, passed, failed)
    sys.exit(0 if passed == 2 else 1)
