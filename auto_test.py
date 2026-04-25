#!/usr/bin/env python3
"""
SDR++Brown Automated TETRA Test Script
========================================
1. Starts SDR++ with minimal config (just module references)
2. Uses HTTP debug protocol to configure everything at runtime:
   - Select File source, load the TETRA WAV file
   - Select NullAudioSink for TETRA Demodulator's stream
   - Set TETRA VFO offset to tune to the actual signal frequency
   - Start playback, monitor TETRA demodulator status
   - Verify TETRA decoder achieves sync
"""

import json
import os
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request
import urllib.parse

# Configuration
REPO_DIR = "/opt/data/SDRPlusPlusBrown"
BUILD_DIR = os.path.join(REPO_DIR, "build")
TETRA_WAV = "/opt/data/baseband_468811597Hz_18-53-50_29-12-2025___tetra.wav"
CONFIG_DIR = "/tmp/sdrpp_auto_test"
HTTP_PORT = 8080
BASE_URL = f"http://localhost:{HTTP_PORT}"

# Source center frequency from filename
SOURCE_CENTER_HZ = 468811597.0

# TETRA signal frequency identified from WAV spectral analysis
# The recording covers 467.612 - 470.012 MHz centered at 468.811597 MHz
# Strongest persistent signal at ~468.125 MHz (on 25 kHz TETRA grid)
# VFO offset = signal_freq - source_center_freq
TETRA_SIGNAL_HZ = 468125000.0  # strongest peak, on 25 kHz grid
VFO_OFFSET_HZ = TETRA_SIGNAL_HZ - SOURCE_CENTER_HZ  # = -686597 Hz

# Environment
DEPS_PREFIX = "/opt/data/.local/build_deps"
CMAKE_PREFIX = "/opt/data/.local/cmake"
ENV = {
    "PATH": f"{CMAKE_PREFIX}/bin:{DEPS_PREFIX}/usr/bin:{os.environ.get('PATH', '')}",
    "PKG_CONFIG_PATH": f"{DEPS_PREFIX}/usr/lib/x86_64-linux-gnu/pkgconfig",
    "LD_LIBRARY_PATH": f"{DEPS_PREFIX}/usr/lib/x86_64-linux-gnu:{BUILD_DIR}/core",
    "CMAKE_ROOT": f"{CMAKE_PREFIX}/share/cmake-3.30",
    "DISPLAY": ":99",
    "XDG_RUNTIME_DIR": "/tmp/runtime-hermes",
}


def http_get(path):
    """HTTP GET request."""
    url = f"{BASE_URL}{path}"
    try:
        resp = urllib.request.urlopen(url, timeout=5)
        return resp.read().decode()
    except urllib.error.HTTPError as e:
        return e.read().decode()
    except Exception as e:
        return f"ERROR: {e}"


def http_post(path, data=""):
    """HTTP POST request."""
    url = f"{BASE_URL}{path}"
    try:
        req = urllib.request.Request(url, data=data.encode(), method='POST')
        resp = urllib.request.urlopen(req, timeout=5)
        return resp.read().decode()
    except urllib.error.HTTPError as e:
        return e.read().decode()
    except Exception as e:
        return f"ERROR: {e}"


def module_command(instance, cmd, args=""):
    """Send a command to a module via the generic automation channel."""
    path = f"/module/{urllib.parse.quote(instance, safe='')}/command"
    body = json.dumps({"cmd": cmd, "args": args})
    return http_post(path, body)


def module_command_get(instance, cmd, args=""):
    """Query a module via GET with query params."""
    path = f"/module/{urllib.parse.quote(instance, safe='')}/command?cmd={urllib.parse.quote(cmd)}&args={urllib.parse.quote(args)}"
    return http_get(path)


def vfo_set_offset(name, offset_hz):
    """Set VFO center offset for any module by instance name.
    Core endpoint - works for any VFO regardless of module type.
    """
    path = f"/vfo/set_offset?name={urllib.parse.quote(name)}&offset={offset_hz}"
    return http_get(path)


def wait_for_ready(timeout=30):
    """Wait for SDR++ HTTP server to be ready."""
    start = time.time()
    while time.time() - start < timeout:
        try:
            resp = http_get("/status")
            if "mainLoopStarted" in resp:
                status = json.loads(resp)
                if status.get("mainLoopStarted"):
                    print(f"[OK] SDR++ ready (main loop started)")
                    return True
                elif status.get("httpListening"):
                    print(f"[INFO] HTTP server up, waiting for main loop...")
            time.sleep(0.5)
        except Exception as e:
            print(f"[WAIT] Still waiting for SDR++... ({e})")
            time.sleep(1)
    print("[FAIL] Timeout waiting for SDR++")
    return False


def create_minimal_config():
    """Create minimal config — no pre-configured source, sink, or demod settings."""
    import shutil
    if os.path.exists(CONFIG_DIR):
        shutil.rmtree(CONFIG_DIR)
    os.makedirs(CONFIG_DIR, exist_ok=True)
    os.makedirs(os.path.join(CONFIG_DIR, "modules_disabled"), exist_ok=True)

    config = {
        "modulesDirectory": CONFIG_DIR + "/modules_disabled",
        "resourcesDirectory": os.path.join(REPO_DIR, "root", "res"),
        "modules": [
            os.path.join(BUILD_DIR, "sink_modules/null_audio_sink/null_audio_sink.so"),
            os.path.join(BUILD_DIR, "source_modules/file_source/file_source.so"),
            os.path.join(BUILD_DIR, "decoder_modules/ch_tetra_demodulator/ch_tetra_demodulator.so"),
        ],
        "moduleInstances": {
            "NullAudioSink": {"module": "null_audio_sink", "enabled": True},
            "File Source": {"module": "file_source", "enabled": True},
            "TETRA Demodulator": {"module": "ch_tetra_demodulator", "enabled": True},
        },
        "showMenu": True,
        "source": "None",
        "frequency": SOURCE_CENTER_HZ,
        "streams": {"Radio": {"muted": False, "sink": "None", "volume": 1.0}},
    }
    with open(os.path.join(CONFIG_DIR, "config.json"), "w") as f:
        json.dump(config, f, indent=2)

    for cfg_file in ["radio_config.json", "null_audio_sink_config.json"]:
        with open(os.path.join(CONFIG_DIR, cfg_file), "w") as f:
            f.write("{}")
    with open(os.path.join(CONFIG_DIR, "file_source_config.json"), "w") as f:
        f.write('{"path": ""}')
    with open(os.path.join(CONFIG_DIR, "tetra_demodulator_config.json"), "w") as f:
        f.write('{"TETRA Demodulator": {"mode": 0, "hostname": "localhost", "port": 8355, "sending": false}}')

    print(f"[OK] Minimal config created at {CONFIG_DIR}")
    print(f"     WAV: {TETRA_WAV}")
    print(f"     Source center: {SOURCE_CENTER_HZ} Hz")
    print(f"     TETRA signal:  {TETRA_SIGNAL_HZ} Hz")
    print(f"     VFO offset:    {VFO_OFFSET_HZ} Hz")


def run_test():
    create_minimal_config()

    sdrpp_bin = os.path.join(BUILD_DIR, "sdrpp")
    cmd = [sdrpp_bin, "-r", CONFIG_DIR, "--http", str(HTTP_PORT)]

    print(f"[INFO] Starting SDR++: {' '.join(cmd)}")
    print(f"[INFO] Config dir: {CONFIG_DIR}")

    log_file = open(os.path.join(CONFIG_DIR, "sdrpp.log"), "w")
    process = subprocess.Popen(
        cmd,
        stdout=log_file,
        stderr=subprocess.STDOUT,
        env={**os.environ, **ENV}
    )
    print(f"[INFO] SDR++ PID: {process.pid}")

    try:
        # Step 1: Wait for readiness
        if not wait_for_ready():
            return False

        time.sleep(1.5)

        # Step 2: Select NullAudioSink for the TETRA Demodulator's stream
        print("\n[STEP] Selecting NullAudioSink for TETRA Demodulator stream...")
        resp = module_command("NullAudioSink", "select", "TETRA Demodulator")
        print(f"       Response: {resp}")
        time.sleep(0.5)

        # Step 3: Select File source via core procfs endpoint
        print("\n[STEP] Selecting File source...")
        resp = http_post("/proc/source/type", "File")
        print(f"       Response: {resp}")
        time.sleep(1.5)

        # Step 4: Set the filename via generic module command
        print(f"\n[STEP] Setting filename (generic channel)...")
        resp = module_command("File Source", "set_filename", TETRA_WAV)
        print(f"       Response: {resp}")
        time.sleep(0.5)

        # Step 5: Set TETRA VFO offset to tune to the actual signal
        # This shifts the VFO from source center to the TETRA signal frequency
        print(f"\n[STEP] Setting TETRA VFO offset to {VFO_OFFSET_HZ} Hz...")
        resp = vfo_set_offset("TETRA Demodulator", VFO_OFFSET_HZ)
        print(f"       Response: {resp}")
        time.sleep(0.3)

        # Step 6: Check initial TETRA Demodulator status
        print("\n[STEP] Checking TETRA Demodulator initial status...")
        resp = module_command_get("TETRA Demodulator", "get_status")
        print(f"       Status: {resp}")
        time.sleep(0.5)

        # Step 7: Start SDR playback
        print("\n[STEP] Starting SDR playback...")
        resp = http_get("/sdr/start")
        print(f"       Response: {resp}")
        time.sleep(2)  # Give TETRA demod time to start processing

        # Step 8: Monitor TETRA status and null audio sink
        print("\n[STEP] Monitoring (15 seconds)...")
        max_poll_seconds = 15
        tetra_synced = False
        audio_flowing = False
        prev_samples = 0

        for i in range(max_poll_seconds):
            time.sleep(1)

            # Check TETRA status
            tetra_resp = module_command_get("TETRA Demodulator", "get_status")
            try:
                tetra_data = json.loads(tetra_resp)
                tetra_state = tetra_data.get("decoder_state", "?")
                tetra_sync = tetra_data.get("sync", False)
                tetra_quality = float(tetra_data.get("signal_quality", 0))
                tetra_status = f"sync={'✓' if tetra_sync else '✗'} state={tetra_state} qual={tetra_quality:.3f}"
                
                # Show more details if available
                extras = ""
                if "mcc" in tetra_data:
                    extras = f" | MCC={tetra_data['mcc']} MNC={tetra_data['mnc']}"
                if "voice_service" in tetra_data:
                    extras += f" voice={'✓' if tetra_data['voice_service'] else '✗'}"
                if "hyperframe" in tetra_data:
                    extras += f" HF={tetra_data['hyperframe']}:{tetra_data['multiframe']}:{tetra_data['frame']}"
            except Exception as e:
                tetra_status = tetra_resp[:80]
                extras = ""
                tetra_sync = False

            # Check null audio sink
            resp = module_command("NullAudioSink", "get_status")
            try:
                status_data = json.loads(resp)
                samples = int(status_data.get("samples", 0))
                sr = float(status_data.get("sample_rate", 0))
                sink_status = status_data.get("status", "unknown")
            except (ValueError, json.JSONDecodeError):
                samples = 0
                sr = 0
                sink_status = "?"

            if samples > prev_samples:
                audio_flowing = True
            prev_samples = samples

            print(f"       Second {i+1}: {tetra_status}{extras}")
            print(f"         Audio: {samples:,} samples @ {sr:.0f} Hz status={sink_status}")

            if tetra_sync:
                tetra_synced = True
                print(f"\n[SUCCESS] TETRA demodulator achieved sync! ✓")
                # Continue monitoring to see more data
                if i >= 5:  # Give it 5 seconds of extra monitoring after sync
                    break

        # Final summary
        print(f"\n{'='*60}")
        print(f"=== TEST RESULTS ===")
        print(f"{'='*60}")
        print(f"  TETRA Sync achieved:   {'✓ YES' if tetra_synced else '✗ NO'}")
        print(f"  Audio flowing:         {'✓ YES' if audio_flowing else '✗ NO'}")

        if tetra_synced:
            print(f"\n[PASS] TETRA demodulator successfully decoded the signal!")
            # Get final status for display
            final = module_command_get("TETRA Demodulator", "get_status")
            print(f"Final status: {final}")
            return True
        else:
            print(f"\n[PARTIAL] Audio flowing but TETRA not synced.")
            print(f"Possible reasons:")
            print(f"  - Signal might not be TETRA at {TETRA_SIGNAL_HZ} Hz")
            print(f"  - VFO offset may need adjustment (trying nearby frequencies...)")
            
            # Try a few nearby frequencies
            for try_offset in [-684000, -685000, -686597, -688000, -689000]:
                print(f"\n  Trying offset {try_offset} Hz...")
                vfo_set_offset("TETRA Demodulator", try_offset)
                time.sleep(2)
                resp = module_command_get("TETRA Demodulator", "get_status")
                try:
                    data = json.loads(resp)
                    if data.get("sync"):
                        print(f"  ✓ SYNC achieved at offset {try_offset} Hz!")
                        return True
                    print(f"    sync={data.get('sync')} state={data.get('decoder_state')} qual={data.get('signal_quality',0):.3f}")
                except:
                    print(f"    {resp[:60]}")
            
            return False

    finally:
        # Stop SDR++
        print("\n[STEP] Stopping SDR++...")
        try:
            http_get("/stop")
            time.sleep(2)
        except:
            pass

        if process.poll() is None:
            print("[INFO] Force killing SDR++...")
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()

        log_file.close()
        print(f"[INFO] SDR++ stopped")
        print(f"[INFO] Log: {CONFIG_DIR}/sdrpp.log")


if __name__ == "__main__":
    success = run_test()
    sys.exit(0 if success else 1)
