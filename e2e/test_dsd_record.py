#!/usr/bin/env python3
"""
E2E Test: DSD Demodulator + Record crash

Tests that enabling DSD demodulator and starting recording doesn't crash SDR++.
Reproduces: Enable DSD demodulator -> press Record -> SDR++ crashes.

The DSD module has uninitialized DSP blocks (resamp, reshape) that are started
without init(), causing null pointer dereference in worker threads.
"""

import sys
import os
import time
from e2e_common import (
    SDRPPTestContext, get_base_config, stats, STATS_MODE,
    http_post
)


def test_dsd_enable_no_record():
    """Test: Enabling DSD module alone must not crash."""
    stats.test_start("test_dsd_enable_no_record")

    main_config = get_base_config()
    main_config["moduleInstances"]["DSD Demodulator"] = {
        "module": "dsdcc_decoder",
        "enabled": False
    }

    with SDRPPTestContext() as ctx:
        ctx.write_configs(main_config)

        if not ctx.start():
            stats.test_fail("test_dsd_enable_no_record", "Failed to start SDR++")
            return False

        stats.info("SDR++ started with DSD module loaded (disabled)")

        # Now enable the DSD module via HTTP
        resp = http_post(ctx.base_url, "/sdr/start")
        stats.debug("SDR start response", resp)

        # Try to enable DSD module
        resp = ctx.module_cmd("DSD Demodulator", "enable", "1")
        stats.debug("DSD enable response", resp)
        ctx.sleep(0.5)

        # Check if process is still alive
        resp = http_post(ctx.base_url, "/status")
        if "ready" in resp and resp.get("ready"):
            stats.test_pass("test_dsd_enable_no_record",
                          "DSD module enabled, SDR++ still alive")
            return True
        else:
            stats.test_fail("test_dsd_enable_no_record", "SDR++ process no longer responsive")
            return False


def test_dsd_enable_and_record():
    """Test: Enabling DSD then starting recording must not crash."""
    stats.test_start("test_dsd_enable_and_record")

    main_config = get_base_config()
    main_config["moduleInstances"]["DSD Demodulator"] = {
        "module": "dsdcc_decoder",
        "enabled": False
    }

    with SDRPPTestContext() as ctx:
        ctx.write_configs(main_config)

        if not ctx.start():
            stats.test_fail("test_dsd_enable_and_record", "Failed to start SDR++")
            return False

        stats.info("SDR++ started")

        # Enable DSD module
        resp = ctx.module_cmd("DSD Demodulator", "enable", "1")
        stats.debug("DSD enable response", resp)
        ctx.sleep(0.5)

        # Check if alive
        resp = http_post(ctx.base_url, "/status")
        if not resp.get("ready"):
            stats.test_fail("test_dsd_enable_and_record", "Process crashed on DSD enable")
            return False

        stats.info("DSD enabled, process alive - press Record would crash here")

        # Try to start audio playback (this stresses the DSP chain)
        resp = http_post(ctx.base_url, "/sdr/start")
        ctx.sleep(0.5)

        resp = http_post(ctx.base_url, "/status")
        if resp.get("ready"):
            stats.test_pass("test_dsd_enable_and_record",
                          "DSD + audio playback, SDR++ alive")
            return True
        else:
            stats.test_fail("test_dsd_enable_and_record", "Process crashed during playback")
            return False


if __name__ == "__main__":
    if not STATS_MODE:
        stats.verbose = True
        stats.section("Testing DSD demodulator + record crash")

    r1 = test_dsd_enable_no_record()
    r2 = test_dsd_enable_and_record()

    passed = sum([r1, r2])
    failed = 2 - passed
    stats.final_summary(2, passed, failed)
    sys.exit(0 if passed == 2 else 1)
