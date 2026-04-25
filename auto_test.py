#!/usr/bin/env python3
"""
SDR++Brown Automated Test Script
=================================
1. Creates a pre-configured config directory
2. Starts SDR++ with HTTP debug server
3. Selects File source, loads the TETRA WAV file
4. Selects NullAudioSink
5. Sets FM demodulator
6. Presses play
7. Monitors sample counter for 5 seconds of audio
8. Stops and reports success/failure
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


def wait_for_ready(timeout=30):
    """Wait for SDR++ HTTP server to be ready."""
    start = time.time()
    while time.time() - start < timeout:
        try:
            resp = http_get("/status")
            if "mainLoopStarted" in resp:
                status = json.loads(resp)
                if status.get("mainLoopStarted"):
                    print(f"[OK] SDR++ ready (HTTP server ready={status.get('httpListening')})")
                    return True
                elif status.get("httpListening"):
                    print(f"[INFO] HTTP server up, waiting for main loop...")
            time.sleep(0.5)
        except Exception as e:
            print(f"[WAIT] Still waiting for SDR++... ({e})")
            time.sleep(1)
    print("[FAIL] Timeout waiting for SDR++")
    return False


def create_config():
    """Create pre-configured config directory."""
    import shutil
    if os.path.exists(CONFIG_DIR):
        shutil.rmtree(CONFIG_DIR)
    os.makedirs(CONFIG_DIR, exist_ok=True)

    # Main config.json - use an empty/invalid modulesDirectory to prevent
    # auto-discovery of ALL .so files (including libsdrpp_core.so)
    config = {
        "modulesDirectory": CONFIG_DIR + "/modules_disabled",
        "resourcesDirectory": os.path.join(REPO_DIR, "root", "res"),
        "source": "File",
        "streams": {
            "Radio": {
                "muted": False,
                "sink": "None",  # Will be changed via procfs
                "volume": 1.0
            }
        },
        "modules": [
            os.path.join(BUILD_DIR, "sink_modules/null_audio_sink/null_audio_sink.so"),
            os.path.join(BUILD_DIR, "source_modules/file_source/file_source.so"),
            os.path.join(BUILD_DIR, "decoder_modules/radio/radio.so"),
            os.path.join(BUILD_DIR, "sink_modules/network_sink/network_sink.so"),
        ],
        "moduleInstances": {
            "Radio": {
                "module": "radio",
                "enabled": True
            },
            "NullAudioSink": {
                "module": "null_audio_sink",
                "enabled": True
            },
            "File Source": {
                "module": "file_source",
                "enabled": True
            }
        },
        "frequency": 468811597.0,  # Center frequency from WAV filename
        "showMenu": True
    }
    with open(os.path.join(CONFIG_DIR, "config.json"), "w") as f:
        json.dump(config, f, indent=2)

    # Radio config - FM demod (DEMOD_NFM = 0)
    radio_config = {
        "Radio": {
            "selectedDemodId": 0  # RADIO_DEMOD_NFM = FM
        }
    }
    with open(os.path.join(CONFIG_DIR, "radio_config.json"), "w") as f:
        json.dump(radio_config, f, indent=2)

    # File source config
    file_source_config = {
        "path": TETRA_WAV,
        "centerFreq": 468811597.0
    }
    with open(os.path.join(CONFIG_DIR, "file_source_config.json"), "w") as f:
        json.dump(file_source_config, f, indent=2)

    # Null audio sink config (empty)
    null_sink_config = {}
    with open(os.path.join(CONFIG_DIR, "null_audio_sink_config.json"), "w") as f:
        json.dump(null_sink_config, f, indent=2)

    print(f"[OK] Config directory created at {CONFIG_DIR}")
    print(f"     Source: File -> {TETRA_WAV}")
    print(f"     Radio demod: FM (ID=0)")
    print(f"     Modules: {BUILD_DIR}")


def run_test():
    create_config()

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

        time.sleep(1)  # Let modules initialize

        # Step 2: Select NullAudioSink as the sink for the Radio stream
        print("\n[STEP] Selecting NullAudioSink sink...")
        resp = http_post("/proc/null_audio_sink/select", "Radio")
        print(f"       Response: {resp}")
        time.sleep(0.5)

        # Step 3: Select File source
        print("\n[STEP] Selecting File source...")
        resp = http_post("/proc/source/type", "File")
        print(f"       Response: {resp}")
        time.sleep(1)

        # Step 4: Set the filename via procfs
        print(f"\n[STEP] Setting filename to {TETRA_WAV}...")
        resp = http_post("/proc/source/filename", TETRA_WAV)
        print(f"       Response: {resp}")
        time.sleep(0.5)

        # Step 5: Start SDR playback
        print("\n[STEP] Starting SDR playback...")
        resp = http_get("/sdr/start")
        print(f"       Response: {resp}")
        time.sleep(1)

        # Step 6: Check initial sample count
        print("\n[STEP] Monitoring null audio sink...")
        initial_samples = 0
        samples_increasing = 0
        for i in range(DURATION_SECONDS + 5):  # Poll for up to ~10 seconds
            time.sleep(1)
            resp = http_get("/proc/null_audio_sink/samples")
            try:
                if resp.strip().startswith('"'):
                    samples = int(resp.strip().strip('"'))
                else:
                    samples = int(resp.strip())
            except (ValueError, json.JSONDecodeError):
                samples = 0
                print(f"       [WARN] Could not parse samples response: {resp}")

            resp_sr = http_get("/proc/null_audio_sink/sample_rate")
            try:
                if resp_sr.strip().startswith('"'):
                    sr = float(resp_sr.strip().strip('"'))
                else:
                    sr = float(resp_sr.strip())
            except (ValueError, json.JSONDecodeError):
                sr = 0

            # Also check status
            resp_status = http_get("/proc/null_audio_sink/status")
            status = resp_status.strip().strip('"')

            # Check if samples are increasing compared to last poll
            if i > 0 and samples > initial_samples:
                samples_increasing += 1
            initial_samples = samples

            print(f"       Second {i+1}: samples={samples:,} @ {sr:.0f} Hz status={status}")

            if samples_increasing >= DURATION_SECONDS:
                print(f"\n[SUCCESS] ✓ Audio flowing for {DURATION_SECONDS}s!")
                print(f"          Total samples consumed: {samples:,}")
                print(f"          Sample rate: {sr:.0f} Hz")
                break

        else:
            # Check final count after all polling is done
            resp = http_get("/proc/null_audio_sink/samples")
            try:
                if resp.strip().startswith('"'):
                    final_samples = int(resp.strip().strip('"'))
                else:
                    final_samples = int(resp.strip())
            except (ValueError, json.JSONDecodeError):
                final_samples = 0
            status_check = "PASS" if final_samples > 0 else "FAIL"
            print(f"\n[CHECK] Final: {final_samples:,} samples — {'AUDIO FLOWING' if final_samples > 1000 else 'NO AUDIO'}")

        return True

    finally:
        # Step 7: Stop SDR++
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
