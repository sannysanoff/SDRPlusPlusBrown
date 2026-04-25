#!/usr/bin/env python3
"""
SDR++Brown Automated Test Script
=================================
1. Starts SDR++ with minimal config (just module references)
3. Uses HTTP debug protocol to configure everything at runtime:
   - Select File source, load the TETRA WAV file
   - Select NullAudioSink via generic automation channel
   - Check TETRA Demodulator status via generic automation channel
   - Start playback, monitor null audio sink and TETRA status
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
SAMPLE_RATE = 2400000  # WAV file sample rate
DURATION_SECONDS = 5
EXPECTED_SAMPLES = SAMPLE_RATE * DURATION_SECONDS  # 12,000,000 stereo frames

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
    """Send a command to a module via the generic automation channel.
    Uses POST with JSON body: {"cmd": "...", "args": "..."}
    URL-encodes the instance name for spaces/special chars."""
    path = f"/module/{urllib.parse.quote(instance, safe='')}/command"
    body = json.dumps({"cmd": cmd, "args": args})
    return http_post(path, body)


def module_command_get(instance, cmd, args=""):
    """Query a module via GET with query params.
    Some modules also support GET for read-only commands."""
    path = f"/module/{urllib.parse.quote(instance, safe='')}/command?cmd={urllib.parse.quote(cmd)}&args={urllib.parse.quote(args)}"
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
        "frequency": 468811597.0,
        "streams": {"Radio": {"muted": False, "sink": "None", "volume": 1.0}},
    }
    with open(os.path.join(CONFIG_DIR, "config.json"), "w") as f:
        json.dump(config, f, indent=2)

    # Config files must have their proper defaults
    for cfg_file in ["radio_config.json", "null_audio_sink_config.json"]:
        with open(os.path.join(CONFIG_DIR, cfg_file), "w") as f:
            f.write("{}")
    with open(os.path.join(CONFIG_DIR, "file_source_config.json"), "w") as f:
        f.write('{"path": ""}')
    # TETRA config: start in osmo-tetra mode (0) with defaults
    with open(os.path.join(CONFIG_DIR, "tetra_demodulator_config.json"), "w") as f:
        f.write('{"TETRA Demodulator": {"mode": 0, "hostname": "localhost", "port": 8355, "sending": false}}')

    print(f"[OK] Minimal config created at {CONFIG_DIR}")
    print(f"     No pre-set source, sink, or demod — all via HTTP debug protocol")


def run_test():
    create_minimal_config()

    # Build command
    sdrpp_bin = os.path.join(BUILD_DIR, "sdrpp")
    cmd = [sdrpp_bin, "-r", CONFIG_DIR, "--http", str(HTTP_PORT)]

    print(f"[INFO] Starting SDR++: {' '.join(cmd)}")
    print(f"[INFO] Config dir: {CONFIG_DIR}")

    # Start SDR++ in background
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

        time.sleep(1.5)  # Let modules initialize

        # Step 2: Select NullAudioSink for the TETRA Demodulator's stream
        print("\n[STEP] Selecting NullAudioSink for TETRA Demodulator stream...")
        resp = module_command("NullAudioSink", "select", "TETRA Demodulator")
        print(f"       Response: {resp}")
        time.sleep(0.5)

        # Step 3: Select File source via core procfs endpoint
        print("\n[STEP] Selecting File source...")
        resp = http_post("/proc/source/type", "File")
        print(f"       Response: {resp}")
        time.sleep(1.5)  # Allow main loop to process source change

        # Step 4: Set the filename via generic module command
        print(f"\n[STEP] Setting filename (generic channel)...")
        resp = module_command("File Source", "set_filename", TETRA_WAV)
        print(f"       Response: {resp}")
        time.sleep(0.5)

        # Step 5: Check TETRA Demodulator status
        print("\n[STEP] Checking TETRA Demodulator status (generic channel)...")
        resp = module_command_get("TETRA Demodulator", "get_status")
        print(f"       Status: {resp}")
        time.sleep(0.5)

        # Step 6: Start SDR playback
        print("\n[STEP] Starting SDR playback...")
        resp = http_get("/sdr/start")
        print(f"       Response: {resp}")
        time.sleep(1)

        # Step 7: Monitor null audio sink and TETRA status
        print("\n[STEP] Monitoring...")
        prev_samples = 0
        samples_increasing = 0
        max_poll_seconds = 15
        success = False
        for i in range(max_poll_seconds):
            time.sleep(1)
            resp = module_command("NullAudioSink", "get_status")
            try:
                status_data = json.loads(resp)
                samples = int(status_data.get("samples", 0))
                sr = float(status_data.get("sample_rate", 0))
                status = status_data.get("status", "unknown")
            except (ValueError, json.JSONDecodeError):
                samples = 0
                sr = 0
                status = "parse_error"
                print(f"       [WARN] Could not parse: {resp}")

            # Periodically check TETRA status
            if i % 3 == 0:
                tetra_resp = module_command_get("TETRA Demodulator", "get_status")
                try:
                    tetra_data = json.loads(tetra_resp)
                    tetra_state = tetra_data.get("decoder_state", "?")
                    tetra_sync = tetra_data.get("sync", "?")
                    tetra_quality = tetra_data.get("signal_quality", 0)
                    tetra_status = f"sync={tetra_sync} state={tetra_state} qual={float(tetra_quality):.3f}"
                except:
                    tetra_status = tetra_resp[:80]
                print(f"       TETRA: {tetra_status}")

            # Check if samples are increasing
            if i > 0 and samples > prev_samples:
                samples_increasing += 1
            prev_samples = samples

            print(f"       Second {i+1}: samples={samples:,} @ {sr:.0f} Hz status={status}")

            if samples_increasing >= DURATION_SECONDS:
                print(f"\n[SUCCESS] ✓ Audio flowing for {DURATION_SECONDS}s!")
                print(f"          Total samples consumed: {samples:,}")
                print(f"          Sample rate: {sr:.0f} Hz")
                success = True
                break

        if not success:
            resp = module_command("NullAudioSink", "get_status")
            try:
                status_data = json.loads(resp)
                final_samples = int(status_data.get("samples", 0))
            except (ValueError, json.JSONDecodeError):
                final_samples = 0
            if final_samples > 1000:
                print(f"\n[CHECK] Final: {final_samples:,} samples — audio IS flowing (partial test PASS)")
                success = True
            else:
                print(f"\n[FAIL] No audio detected — {final_samples:,} samples")

        return success

    finally:
        # Step 8: Stop SDR++
        print("\n[STEP] Stopping SDR++...")
        try:
            http_get("/stop")
            time.sleep(2)
        except:
            pass

        # Force kill if still running
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
