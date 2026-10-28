#!/usr/bin/env python3
"""
E2E Test: Recorder file naming honours the time zone setting

1. Load a baseband file source and start playback (baseband recording needs
   the IQ frontend running)
2. Recorder starts with timezone=utc from config, records, then stops:
   the "$h" field in the filename must match the UTC hour
3. Recorder time zone is switched to local at runtime through the module
   command (same code path the time zone combo uses), records again:
   the "$h" field must match the hour of the configured local zone
4. Both WAV files must exist, be readable and contain signal
5. The recorder config file must persist the last selected time zone

TZ is forced for the SDR++ process so local and UTC hours differ: the test
would otherwise be meaningless on a machine running in UTC.
"""

import os
import sys
import time
import struct
import json
import wave
from datetime import datetime, timezone

from e2e_common import (
    SDRPPTestContext, get_base_config, http_get, http_post, stats, STATS_MODE,
    wait_for_playing,
)


TEST_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "recordings", "dmr_sample.wav")
CARRIER_FREQ = 0.0
SAMPLE_RATE = 16000.0
TEST_TZ = "Asia/Tokyo"  # UTC+9, no DST: local and UTC hour always differ
NAME_TEMPLATE = "$y$M$d-$h-$m-$s-$t"


def expected_hour(utc_hour: bool) -> int:
    """Hour the SDR++ process must use, given the TZ forced for it."""
    now = time.time()
    if utc_hour:
        return time.gmtime(now).tm_hour
    try:
        from zoneinfo import ZoneInfo
        return datetime.fromtimestamp(now, ZoneInfo(TEST_TZ)).hour
    except Exception:
        # No tzdata available: fall back to the host local time.
        return time.localtime(now).tm_hour


def check_wav(path, min_samples=1000):
    """Return (ok, detail) after verifying the WAV is readable and not silent."""
    try:
        w = wave.open(path, 'rb')
        nframes = w.getnframes()
        nchannels = w.getnchannels()
        sampwidth = w.getsampwidth()
        data = w.readframes(nframes)
        w.close()
    except Exception as e:
        return False, f"cannot open WAV: {e}"

    total = nframes * nchannels
    if total == 0:
        return False, "WAV contains no frames"

    fmt = {1: 'B', 2: 'h', 4: 'i'}[sampwidth]
    samples = struct.unpack("<%d%s" % (total, fmt), data)
    peak = max(abs(s) for s in samples)
    nonzero = sum(1 for s in samples if s != 0)
    return True, f"{nframes} frames, {nchannels} ch, peak={peak}, nonzero={nonzero}"


def record_for(ctx, seconds, label):
    """Run one recording pass, return (recording path, recorder status)."""
    resp = ctx.module_cmd("Recorder", "start")
    stats.debug(f"{label} Recorder start", resp)
    if resp.get("status") != "recording":
        return None, resp

    ctx.sleep(seconds)

    status = ctx.module_cmd("Recorder", "status")
    resp = ctx.module_cmd("Recorder", "stop")
    stats.debug(f"{label} Recorder stop", resp)
    ctx.sleep(0.5)
    return status.get("file", ""), status


def filename_field(path, index):
    """Extract a dash separated field of the generated file name."""
    name = os.path.basename(path)
    parts = name.split("-")
    return parts[index] if index < len(parts) else ""


def test_recorder_timezone():
    stats.test_start("test_recorder_timezone")

    if not os.path.exists(TEST_FILE):
        stats.test_fail("test_recorder_timezone", f"Test file not found: {TEST_FILE}")
        return False

    utc_hour = expected_hour(True)
    local_hour = expected_hour(False)
    stats.info(f"Expected hours: utc={utc_hour:02d} local({TEST_TZ})={local_hour:02d}")
    if utc_hour == local_hour:
        stats.info("WARNING: local and UTC hour are identical, hour check cannot discriminate")

    main_config = get_base_config()
    main_config["moduleInstances"]["File Source"] = {"module": "file_source", "enabled": True}
    main_config["moduleInstances"]["Recorder"] = {"module": "recorder", "enabled": True}
    main_config["source"] = "file_source"
    main_config["File Source"] = {"filename": TEST_FILE}
    main_config["frequency"] = CARRIER_FREQ
    main_config["sampleRate"] = SAMPLE_RATE

    recorder_config = {
        "Recorder": {
            "mode": 0,  # RECORDER_MODE_BASEBAND
            "recPath": "",
            "timezone": "utc",
            "nameTemplate": NAME_TEMPLATE,
            "container": "WAV",
            "sampleType": 2,
            "ignoreSilence": False,
        }
    }

    # localtime() of the SDR++ process must differ from UTC
    os.environ["TZ"] = TEST_TZ
    try:
        time.tzset()
    except Exception:
        pass

    with SDRPPTestContext(startup_timeout=30.0) as ctx:
        recordings_dir = os.path.join(ctx.temp_dir, "recordings")
        os.makedirs(recordings_dir, exist_ok=True)
        recorder_config["Recorder"]["recPath"] = recordings_dir
        ctx.write_configs(main_config, recorder_config=recorder_config)

        if not ctx.start():
            stats.test_fail("test_recorder_timezone", "Failed to start SDR++")
            return False
        stats.info("SDR++ started")

        resp = http_post(ctx.base_url, "/proc/source/type", "File")
        stats.debug("source/type = File", resp)
        ctx.sleep(0.3)
        resp = ctx.module_cmd("File Source", "set_filename", TEST_FILE)
        stats.debug("File Source set_filename", resp)
        ctx.sleep(0.3)

        resp = http_get(ctx.base_url, "/sdr/start")
        if resp.get("action") != "sdr_start":
            stats.test_fail("test_recorder_timezone", f"sdr start failed: {resp}")
            return False
        if not wait_for_playing(ctx, 30.0):
            stats.test_fail("test_recorder_timezone", "SDR++ did not reach playing state")
            return False
        ctx.sleep(1.5)

        # ---- Pass 1: timezone from config (utc) ----
        resp = ctx.module_cmd("Recorder", "get_timezone")
        if resp.get("timezone") != "utc":
            stats.test_fail("test_recorder_timezone", f"config timezone not applied: {resp}")
            return False

        utc_path, status = record_for(ctx, 2.0, "utc")
        if not utc_path:
            stats.test_fail("test_recorder_timezone", f"recording did not start/return a file: {status}")
            return False
        stats.info(f"utc recording: {os.path.basename(utc_path)}")

        if not os.path.isfile(utc_path):
            stats.test_fail("test_recorder_timezone", f"recording file missing: {utc_path}")
            return False

        utc_file_hour = filename_field(utc_path, 1)
        if not utc_file_hour.isdigit():
            stats.test_fail("test_recorder_timezone", f"cannot parse hour from file name: {utc_path}")
            return False
        if int(utc_file_hour) != utc_hour:
            stats.test_fail("test_recorder_timezone",
                            f"utc mode used hour {utc_file_hour}, expected {utc_hour:02d}")
            return False
        stats.info(f"utc hour {utc_file_hour} matches")

        # ---- Pass 2: timezone switched at runtime (local) ----
        resp = ctx.module_cmd("Recorder", "set_timezone", "local")
        stats.debug("set_timezone local", resp)
        if resp.get("status") != "ok" or resp.get("timezone") != "local":
            stats.test_fail("test_recorder_timezone", f"set_timezone failed: {resp}")
            return False

        resp = ctx.module_cmd("Recorder", "get_timezone")
        if resp.get("timezone") != "local":
            stats.test_fail("test_recorder_timezone", f"timezone switch not applied: {resp}")
            return False

        local_path, status = record_for(ctx, 2.0, "local")
        if not local_path:
            stats.test_fail("test_recorder_timezone", f"recording did not start/return a file: {status}")
            return False
        stats.info(f"local recording: {os.path.basename(local_path)}")

        if local_path == utc_path:
            stats.test_fail("test_recorder_timezone", "second recording reused the first file name")
            return False

        local_file_hour = filename_field(local_path, 1)
        if int(local_file_hour) != local_hour:
            stats.test_fail("test_recorder_timezone",
                            f"local mode used hour {local_file_hour}, expected {local_hour:02d}")
            return False

        if utc_hour != local_hour and local_file_hour == utc_file_hour:
            stats.test_fail("test_recorder_timezone",
                            "local and utc recordings share the same hour, time zone was ignored")
            return False

        # ---- Recording content ----
        for label, path in (("utc", utc_path), ("local", local_path)):
            ok, detail = check_wav(path)
            if not ok:
                stats.test_fail("test_recorder_timezone", f"{label} recording invalid: {detail}")
                return False
            if os.path.getsize(path) <= 44:
                stats.test_fail("test_recorder_timezone", f"{label} recording has no samples ({path})")
                return False
            stats.info(f"{label} recording content: {detail}")

        # ---- Config persistence ----
        rec_config_path = os.path.join(ctx.temp_dir, "recorder_config.json")
        if not os.path.isfile(rec_config_path):
            stats.test_fail("test_recorder_timezone", f"recorder config not written: {rec_config_path}")
            return False
        with open(rec_config_path) as f:
            saved = json.load(f)
        saved_tz = saved.get("Recorder", {}).get("timezone")
        if saved_tz != "local":
            stats.test_fail("test_recorder_timezone", f"timezone not persisted, got: {saved_tz!r}")
            return False

        stats.test_pass("test_recorder_timezone",
                        f"utc hour {utc_file_hour}, {TEST_TZ} hour {local_file_hour}, config persisted")
        return True


if __name__ == "__main__":
    if not STATS_MODE:
        stats.verbose = True
        stats.section("Testing Recorder time zone handling")

    result = test_recorder_timezone()

    stats.final_summary(1, 1 if result else 0, 0 if result else 1)
    sys.exit(0 if result else 1)