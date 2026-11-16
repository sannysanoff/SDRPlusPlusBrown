#!/usr/bin/env python3
"""
E2E Test: DAB/DAB+ as a mode of the radio module

The DAB receiver is not a radio of its own: the dabplus_decoder module injects a
"DAB" mode into every radio module instance, and the radio's own VFO, IF chain
and audio sinks carry the receiver. This test proves that wiring end to end
against the committed 11D "Bayern" recording (222.064 MHz, Mode I):

1. The headless decoder decodes the recording (engine path, no GUI).
2. The radio's dynamic mode list contains DAB.
3. Selecting DAB and playing the recording leads to OFDM frame sync, the decoded
   ensemble name, the FIC service list and 48 kHz stereo audio with a measured
   level - all reported by the radio module.
4. Selecting a service by subchannel changes the decoded programme.
5. Switching away from DAB and stopping playback complete without a hang.

Environment:
    E2E_VERBOSE=1        - verbose output
    E2E_BUILD_DIR        - build directory holding the sdrpp binary
    E2E_ROOT_DEV         - root holding the installed plugins
    E2E_TEST_FILE        - override the recording used
"""

import json
import os
import subprocess
import sys
import time

from e2e_common import (
    DEFAULT_BUILD_DIR,
    SDRPPTestContext,
    STATS_MODE,
    assert_response_ok,
    get_base_config,
    http_get,
    stats,
    wait_for_playing,
)

RADIO = "Radio"
MODULE_INSTANCE = "DAB+ Modes"
RECORDING = os.environ.get(
    "E2E_TEST_FILE",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "recordings",
                 "dabplus_11d_222064000Hz_8bit.wav"),
)
# Mode I ensemble at 222.064 MHz, 2048 kS/s, 8-bit I/Q.
ENSEMBLE_FREQ = 222064000.0
ENSEMBLE_NAME = "Bayern"


def _dab_status(ctx):
    return ctx.module_cmd(RADIO, "dab_status")


def _wait_for(ctx, predicate, timeout, interval=0.3):
    deadline = time.time() + timeout
    last = {}
    while time.time() < deadline:
        last = _dab_status(ctx)
        if "error" not in last and predicate(last):
            return last
        time.sleep(interval)
    return ("last", last)


def test_dabplus_cli_decode():
    """The headless decoder decodes the recording: ensemble, services, audio."""
    stats.subsection("Headless decoder on the recorded ensemble")
    if not os.path.exists(RECORDING):
        stats.test_fail("test_dabplus_cli_decode", f"Recording not found: {RECORDING}")
        return False

    cli = os.path.join(DEFAULT_BUILD_DIR, "decoder_modules", "dabplus_decoder",
                       "dabplus_decoder_cli")
    if not os.path.isfile(cli):
        stats.test_fail("test_dabplus_cli_decode", f"CLI not built: {cli}")
        return False

    result = subprocess.run([cli, RECORDING, "--passes", "4"],
                            capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        stats.test_fail("test_dabplus_cli_decode", f"CLI failed: {result.stderr[-800:]}")
        return False

    line = [l for l in result.stdout.splitlines() if l.strip().startswith("{")]
    if not line:
        stats.test_fail("test_dabplus_cli_decode", f"No JSON from CLI: {result.stdout[-400:]}")
        return False
    decoded = json.loads(line[-1])

    if decoded.get("sync_state") != 4 or ENSEMBLE_NAME not in decoded.get("ensemble", ""):
        stats.test_fail("test_dabplus_cli_decode", f"No ensemble decoded: {decoded}")
        return False
    if len(decoded.get("services", [])) < 8:
        stats.test_fail("test_dabplus_cli_decode", f"Too few services: {decoded.get('services')}")
        return False
    if decoded.get("audio_rate") != 48000 or decoded.get("audio_frames", 0) <= 0:
        stats.test_fail("test_dabplus_cli_decode", f"No audio decoded: {decoded}")
        return False
    if decoded.get("audio_peak", 0.0) <= 0.001:
        stats.test_fail("test_dabplus_cli_decode", f"Decoded audio is silent: {decoded.get('audio_peak')}")
        return False

    stats.info(f"ensemble={decoded['ensemble'].strip()!r} services={len(decoded['services'])} "
               f"audio={decoded['audio_rate']}Hz peak={decoded['audio_peak']:.3f} "
               f"label={decoded['dynamic_label']!r}")

    # The transforms are the application's own FFT: FFTW3f everywhere, and on
    # macOS vDSP (Accelerate) when the accelerated FFT is enabled. Decoding the
    # same recording on the accelerated backend must give the same ensemble.
    if sys.platform == "darwin":
        accelerated = subprocess.run([cli, RECORDING, "--passes", "4", "--accelerated-fft"],
                                     capture_output=True, text=True, timeout=120)
        lines = [l for l in accelerated.stdout.splitlines() if l.strip().startswith("{")]
        if accelerated.returncode != 0 or not lines:
            stats.test_fail("test_dabplus_cli_decode",
                            f"Accelerated FFT run failed: {accelerated.stderr[-400:]}")
            return False
        fast = json.loads(lines[-1])
        if fast.get("ensemble") != decoded.get("ensemble") or len(fast.get("services", [])) != len(decoded["services"]):
            stats.test_fail("test_dabplus_cli_decode",
                            f"vDSP FFT decodes differently: {fast.get('ensemble')!r} / "
                            f"{len(fast.get('services', []))} services vs {decoded['ensemble']!r} / "
                            f"{len(decoded['services'])}")
            return False
        stats.info(f"accelerated (vDSP) FFT: ensemble={fast['ensemble'].strip()!r} "
                   f"services={len(fast['services'])} peak={fast['audio_peak']:.3f}")

    stats.test_pass("test_dabplus_cli_decode",
                    f"{len(decoded['services'])} services, 48 kHz audio, label decoded")
    return True


def test_dab_as_radio_mode():
    """DAB appears in the radio's mode list and decodes through the radio chain."""
    stats.subsection("DAB mode inside the radio module")

    cfg = get_base_config()
    cfg["frequency"] = ENSEMBLE_FREQ
    cfg["sampleRate"] = 2048000.0
    cfg["moduleInstances"]["File Source"] = {"module": "file_source", "enabled": True}
    cfg["moduleInstances"]["NullAudioSink"] = {"module": "null_audio_sink", "enabled": True}
    cfg["moduleInstances"][MODULE_INSTANCE] = {"module": "dabplus_decoder", "enabled": True}
    cfg["streams"][RADIO] = {"muted": False, "sink": "NullAudioSink", "volume": 1.0}
    cfg["vfoOffsets"][RADIO] = 0.0

    # Loading ~45 plugins from a cold cache (first start after a rebuild and a
    # code-sign sweep) takes longer than the harness default; the receiver is
    # ready once the HTTP port answers, so allow for the disk work.
    with SDRPPTestContext(startup_timeout=60.0) as ctx:
        ctx.write_configs(cfg)
        if not ctx.start():
            stats.test_fail("test_dab_as_radio_mode", "Failed to start SDR++")
            return False

        ctx.sleep(0.5)

        # 1. The mode is registered on the radio, not as a separate receiver.
        demods = ctx.module_cmd(RADIO, "list_demods")
        names = [d.get("name") for d in demods.get("demods", [])]
        if "DAB" not in names:
            stats.test_fail("test_dab_as_radio_mode", f"DAB missing from radio modes: {names}")
            return False
        stats.info(f"radio modes: {names}")

        modules = http_get(ctx.base_url, "/modules")
        if modules.get(MODULE_INSTANCE, {}).get("module") != "dabplus_decoder":
            stats.test_fail("test_dab_as_radio_mode", f"provider module missing: {modules}")
            return False

        resp = ctx.module_cmd("File Source", "set_filename", RECORDING)
        ok, msg = assert_response_ok(resp, "set_filename")
        if not ok:
            stats.test_fail("test_dab_as_radio_mode", msg)
            return False

        # 2. Select the DAB mode and start playing through the radio's chain.
        resp = ctx.module_cmd(RADIO, "set_demod", "DAB")
        if "error" in resp:
            stats.test_fail("test_dab_as_radio_mode", f"set_demod DAB failed: {resp}")
            return False
        selected_demod = ctx.module_cmd(RADIO, "get_demod")
        if selected_demod.get("demod") != "DAB":
            stats.test_fail("test_dab_as_radio_mode", f"radio did not switch to DAB: {selected_demod}")
            return False
        # The mode asks the module column for extra width, which is the space the
        # waterfall gives up while DAB is selected.
        if selected_demod.get("extra_menu_width", 0) <= 0:
            stats.test_fail("test_dab_as_radio_mode",
                            f"DAB does not widen the module column: {selected_demod}")
            return False
        stats.info(f"DAB asks for {selected_demod['extra_menu_width']:.0f} px of extra panel width")

        http_get(ctx.base_url, "/sdr/start")
        if not wait_for_playing(ctx, timeout=10.0):
            stats.test_fail("test_dab_as_radio_mode", "SDR failed to start playing")
            return False

        # 3. Sync, ensemble and the service list. The recording is 0.9 s and the
        #    file source loops it, so the FIC gets repeated chances.
        synced = _wait_for(
            ctx,
            lambda s: s.get("sync_state") == 4 and len(s.get("services", [])) >= 8
            and ENSEMBLE_NAME in s.get("ensemble", ""),
            timeout=25.0,
        )
        if isinstance(synced, tuple):
            stats.test_fail("test_dab_as_radio_mode", f"No ensemble after 25 s: {synced[1]}")
            return False
        stats.info(f"sync_state={synced['sync_state']} ensemble={synced['ensemble'].strip()!r} "
                   f"services={len(synced['services'])} frames={synced['frames']}")

        # 4. Decoded audio, measured at the demodulator output that feeds the
        #    radio's AF chain.
        audiod = _wait_for(
            ctx,
            lambda s: s.get("audio_rate") == 48000 and s.get("audio_peak", 0.0) > 0.001,
            timeout=25.0,
        )
        if isinstance(audiod, tuple):
            stats.test_fail("test_dab_as_radio_mode", f"No decoded audio after 25 s: {audiod[1]}")
            return False
        stats.info(f"audio={audiod['audio_rate']} Hz stereo={audiod['audio_stereo']} "
                   f"peak={audiod['audio_peak']:.3f} selected={audiod['selected']} "
                   f"frames={audiod['audio_frames']} constellation={audiod['constellation_points']}")
        if audiod.get("constellation_points", 0) <= 0:
            stats.test_fail("test_dab_as_radio_mode", f"No constellation samples: {audiod}")
            return False

        # The recording is fed by the file reader, which is far faster than the
        # audio device. The demodulator has to hold it back: decoded audio must
        # advance at the programme's own rate and the ring must not overflow,
        # otherwise the file is consumed at disk speed and the audio comes out
        # fast, with gaps where the engine dropped the backlog.
        first = _dab_status(ctx)
        ctx.sleep(2.0)
        second = _dab_status(ctx)
        elapsed = 2.0
        produced = second["audio_frames"] - first["audio_frames"]
        expected = audiod["audio_rate"] * elapsed
        if produced < expected * 0.7 or produced > expected * 1.4:
            stats.test_fail("test_dab_as_radio_mode",
                            f"audio clock is not the programme's: {produced} frames in "
                            f"{elapsed:.1f} s, expected about {expected:.0f}")
            return False
        if second.get("audio_overflows", -1) != 0:
            stats.test_fail("test_dab_as_radio_mode",
                            f"audio ring overflowed {second['audio_overflows']} times, "
                            "the source is not being held back")
            return False
        stats.info(f"real-time audio clock: {produced / elapsed:.0f} frames/s, no ring overflow")

        # 5. Service selection through the radio's mode.
        services = [s for s in audiod["services"] if s.get("label", "").strip()]
        target = next((s for s in services if s["subchannel"] != audiod["selected"]), None)
        if target is None:
            stats.test_fail("test_dab_as_radio_mode", f"No second service to select: {services}")
            return False
        resp = ctx.module_cmd(RADIO, "dab_select", str(target["subchannel"]))
        if "error" in resp:
            stats.test_fail("test_dab_as_radio_mode", f"dab_select failed: {resp}")
            return False
        selected = _wait_for(ctx, lambda s: s.get("selected") == target["subchannel"], timeout=10.0)
        if isinstance(selected, tuple):
            stats.test_fail("test_dab_as_radio_mode",
                            f"Service {target['subchannel']} not selected: {selected[1]}")
            return False
        stats.info(f"selected {target['label'].strip()!r} (subchannel {target['subchannel']})")

        # 6. Switching away from DAB and stopping must be clean.
        resp = ctx.module_cmd(RADIO, "set_demod", "NFM")
        if "error" in resp:
            stats.test_fail("test_dab_as_radio_mode", f"set_demod NFM failed: {resp}")
            return False
        # NFM wants no extra panel width, so the waterfall gets the space back.
        nfm = ctx.module_cmd(RADIO, "get_demod")
        if nfm.get("demod") != "NFM" or nfm.get("extra_menu_width", -1) != 0:
            stats.test_fail("test_dab_as_radio_mode",
                            f"leaving DAB did not release the panel width: {nfm}")
            return False
        ctx.sleep(0.5)
        stop = http_get(ctx.base_url, "/sdr/stop")
        if stop.get("playing"):
            stats.test_fail("test_dab_as_radio_mode", f"/sdr/stop did not stop playback: {stop}")
            return False

    stats.test_pass("test_dab_as_radio_mode",
                    "DAB listed as a radio mode, ensemble and audio decoded through the radio chain")
    return True


if __name__ == "__main__":
    if not STATS_MODE:
        stats.verbose = True
        stats.section("Testing DAB as a mode of the radio module")

    results = [test_dabplus_cli_decode(), test_dab_as_radio_mode()]
    passed = sum(1 for r in results if r)
    stats.final_summary(len(results), passed, len(results) - passed)
    sys.exit(0 if all(results) else 1)
