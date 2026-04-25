#!/usr/bin/env python3
"""
E2E Test: LSB Bandwidth at Startup

Tests that LSB mode has correct VFO bandwidth (~2.7kHz) not the 100kHz bug.
"""

import json
import os
import subprocess
import sys
import tempfile
import time
import shutil

HTTP_PORT = 8085
BASE_URL = f"http://localhost:{HTTP_PORT}"


def http_post(path, json_data=None):
    url = f"{BASE_URL}{path}"
    try:
        import urllib.request
        data = json.dumps(json_data).encode() if json_data else b''
        req = urllib.request.Request(url, data=data, headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(req, timeout=5.0) as resp:
            return json.loads(resp.read().decode())
    except Exception as e:
        return {"error": str(e)}


def module_cmd(instance_name, cmd, args=""):
    return http_post(f"/module/{instance_name.replace(' ', '%20')}/command", 
                    json_data={"cmd": cmd, "args": args})


def wait_for_server(timeout=20.0):
    import urllib.request
    start = time.time()
    while time.time() - start < timeout:
        try:
            with urllib.request.urlopen(f"{BASE_URL}/status", timeout=0.5) as resp:
                data = json.loads(resp.read().decode())
                if data.get("mainLoopStarted"):
                    return True
        except:
            pass
        time.sleep(0.1)
    return False


def test_lsb_startup():
    """Test LSB bandwidth at startup"""
    print("="*60)
    print("Testing LSB bandwidth at startup")
    print("="*60)
    
    # Paths used by sdrpp-cli
    build_dir = "/Users/san/Fun/SDRPlusPlus/cmake-build-debug"
    root_dev = "/Users/san/Fun/SDRPlusPlus/root_dev"
    
    temp_dir = tempfile.mkdtemp(prefix="sdrpp_lsb_")
    
    # Config with correct paths - using paths relative to how sdrpp-cli does it
    config = {
        "frequency": 7100000.0,
        "sampleRate": 48000.0,
        # Don't set modules/resources - let it use defaults or relative paths
        "moduleInstances": {
            "Radio": {"module": "radio", "enabled": True},
            "NullAudioSink": {"module": "null_audio_sink", "enabled": True}
        },
        "streams": {
            "Radio": {"muted": False, "sink": "None", "volume": 1.0}
        },
        "vfoOffsets": {"Radio": 0.0},
    }
    
    # Radio config with LSB pre-selected
    radio_config = {
        "Radio": {
            "selectedDemodId": 6,  # LSB
            "LSB": {"bandwidth": 2700.0, "snapInterval": 100.0}
        }
    }
    
    with open(f"{temp_dir}/config.json", 'w') as f:
        json.dump(config, f, indent=2)
    
    with open(f"{temp_dir}/radio_config.json", 'w') as f:
        json.dump(radio_config, f, indent=2)
    
    # Kill existing
    subprocess.run(["pkill", "-f", f"sdrpp.*--http.*{HTTP_PORT}"], capture_output=True)
    time.sleep(0.5)
    
    # Start SDR++ from the BUILD DIRECTORY (crucial!)
    # This is what sdrpp-cli does - cd to build dir then run ./sdrpp
    binary = "./sdrpp"
    env = os.environ.copy()
    env['QT_QPA_PLATFORM'] = 'offscreen'
    
    # Run from build_dir
    cmd = [binary, '-r', temp_dir, '--http', str(HTTP_PORT)]
    
    log_path = f"{temp_dir}/sdrpp.log"
    with open(log_path, 'w') as log_file:
        # CRITICAL: cwd=build_dir
        proc = subprocess.Popen(cmd, stdout=log_file, stderr=log_file, 
                               env=env, cwd=build_dir)
        
        try:
            print("Waiting for SDR++ to start...")
            if not wait_for_server():
                print("FAILED to start")
                with open(log_path, 'r') as f:
                    log_content = f.read()
                    print("Last 2000 chars of log:")
                    print(log_content[-2000:])
                return False
            
            print("SDR++ ready! Checking bandwidth...")
            time.sleep(2.0)  # Extra time for init
            
            # Check demod
            resp = module_cmd("Radio", "get_demod")
            print(f"Demod: {resp}")
            
            # Check bandwidth
            resp = module_cmd("Radio", "get_vfo_bandwidth")
            print(f"Bandwidth: {resp}")
            
            if "error" in resp:
                print(f"ERROR: {resp['error']}")
                return False
            
            vfo_bw = resp.get("vfo_bandwidth", 0)
            print(f"\n*** VFO bandwidth: {vfo_bw:.1f} Hz ***")
            
            if 1700 <= vfo_bw <= 3700:
                print("*** PASS: Correct LSB bandwidth (~2.7kHz) ***")
                return True
            elif vfo_bw > 50000:
                print("*** FAIL: 100kHz bug detected! ***")
                return False
            else:
                print(f"*** FAIL: Unexpected bandwidth ***")
                return False
                
        finally:
            print("\nStopping...")
            proc.terminate()
            try:
                proc.wait(timeout=5.0)
            except:
                proc.kill()
            
            # Print log before cleanup
            print("\n--- SDR++ Log (last 3000 chars) ---")
            with open(log_path, 'r') as f:
                log_content = f.read()
                print(log_content[-3000:])
            
            shutil.rmtree(temp_dir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(0 if test_lsb_startup() else 1)
