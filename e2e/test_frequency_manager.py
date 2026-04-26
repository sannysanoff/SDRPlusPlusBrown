#!/usr/bin/env python3
"""
E2E Test: Frequency Manager Debug Protocol

Tests the frequency manager debug commands including:
- get_lists, get_current_list, set_current_list
- get_bookmarks, add_bookmark, remove_bookmark, apply_bookmark
- get_scanner_status, start_scanner, stop_scanner
"""

import json
import os
import subprocess
import sys
import tempfile
import time
import shutil

HTTP_PORT = 8086
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


def test_frequency_manager():
    """Test frequency manager debug protocol"""
    print("="*60)
    print("Testing Frequency Manager Debug Protocol")
    print("="*60)
    
    # Paths used by sdrpp-cli
    build_dir = "/Users/san/Fun/SDRPlusPlus/cmake-build-debug"
    root_dev = "/Users/san/Fun/SDRPlusPlus/root_dev"
    
    temp_dir = tempfile.mkdtemp(prefix="sdrpp_freqmgr_")
    
    # Config with frequency manager enabled
    config = {
        "frequency": 7100000.0,
        "sampleRate": 48000.0,
        "moduleInstances": {
            "Radio": {"module": "radio", "enabled": True},
            "NullAudioSink": {"module": "null_audio_sink", "enabled": True},
            "Frequency Manager": {"module": "frequency_manager", "enabled": True}
        },
        "streams": {
            "Radio": {"muted": False, "sink": "None", "volume": 1.0}
        },
        "vfoOffsets": {"Radio": 0.0},
    }
    
    # Radio config with FM mode
    radio_config = {
        "Radio": {
            "selectedDemodId": 0,  # FM
            "FM": {"bandwidth": 12500.0, "snapInterval": 100.0}
        }
    }
    
    # Pre-populate frequency manager config with a test list
    freq_manager_config = {
        "selectedList": "TestList",
        "bookmarkDisplayMode": 0,
        "lists": {
            "TestList": {
                "showOnWaterfall": True,
                "bookmarks": {
                    "TestStation1": {
                        "frequency": 145500000.0,
                        "bandwidth": 12500.0,
                        "mode": 0  # FM
                    },
                    "TestStation2": {
                        "frequency": 446000000.0,
                        "bandwidth": 12500.0,
                        "mode": 0  # FM
                    }
                }
            },
            "EmptyList": {
                "showOnWaterfall": False,
                "bookmarks": {}
            }
        },
        "scanner": {
            "scanIntervalMs": 100.0,
            "listenTimeSec": 5.0,
            "noiseFloor": -120.0,
            "signalMarginDb": 6.0,
            "squelchEnabled": False
        }
    }
    
    with open(f"{temp_dir}/config.json", 'w') as f:
        json.dump(config, f, indent=2)
    
    with open(f"{temp_dir}/radio_config.json", 'w') as f:
        json.dump(radio_config, f, indent=2)
    
    with open(f"{temp_dir}/frequency_manager_config.json", 'w') as f:
        json.dump(freq_manager_config, f, indent=2)
    
    # Kill existing
    subprocess.run(["pkill", "-f", f"sdrpp.*--http.*{HTTP_PORT}"], capture_output=True)
    time.sleep(0.5)
    
    # Start SDR++ from the BUILD DIRECTORY
    binary = "./sdrpp"
    env = os.environ.copy()
    env['QT_QPA_PLATFORM'] = 'offscreen'
    
    # Run from build_dir
    cmd = [binary, '-r', temp_dir, '--http', str(HTTP_PORT)]
    
    log_path = f"{temp_dir}/sdrpp.log"
    
    all_passed = True
    
    with open(log_path, 'w') as log_file:
        proc = subprocess.Popen(cmd, stdout=log_file, stderr=log_file, 
                               env=env, cwd=build_dir)
        
        try:
            print("Waiting for SDR++ to start...")
            if not wait_for_server():
                print("FAILED to start")
                all_passed = False
                with open(log_path, 'r') as f:
                    log_content = f.read()
                    print("Last 2000 chars of log:")
                    print(log_content[-2000:])
                return False
            
            print("SDR++ ready! Testing frequency manager...")
            time.sleep(2.0)  # Extra time for init
            
            # Test 1: Get lists
            print("\n--- Test 1: get_lists ---")
            resp = module_cmd("Frequency Manager", "get_lists")
            print(f"Response: {json.dumps(resp, indent=2)}")
            if "error" in resp:
                print("FAIL: get_lists returned error")
                all_passed = False
            elif "lists" not in resp or "TestList" not in resp.get("lists", []):
                print("FAIL: Expected 'TestList' in lists")
                all_passed = False
            else:
                print("PASS: get_lists returned expected lists")
            
            # Test 2: Get current list
            print("\n--- Test 2: get_current_list ---")
            resp = module_cmd("Frequency Manager", "get_current_list")
            print(f"Response: {json.dumps(resp, indent=2)}")
            if "error" in resp:
                print("FAIL: get_current_list returned error")
                all_passed = False
            elif resp.get("current_list") != "TestList":
                print(f"FAIL: Expected current_list='TestList', got '{resp.get('current_list')}'")
                all_passed = False
            else:
                print("PASS: get_current_list returned expected list")
            
            # Test 3: Get bookmarks
            print("\n--- Test 3: get_bookmarks ---")
            resp = module_cmd("Frequency Manager", "get_bookmarks")
            print(f"Response: {json.dumps(resp, indent=2)}")
            if "error" in resp:
                print("FAIL: get_bookmarks returned error")
                all_passed = False
            elif "bookmarks" not in resp:
                print("FAIL: Expected 'bookmarks' in response")
                all_passed = False
            else:
                bookmarks = resp.get("bookmarks", [])
                if len(bookmarks) != 2:
                    print(f"FAIL: Expected 2 bookmarks, got {len(bookmarks)}")
                    all_passed = False
                else:
                    print("PASS: get_bookmarks returned expected bookmarks")
            
            # Test 4: Add bookmark
            print("\n--- Test 4: add_bookmark ---")
            resp = module_cmd("Frequency Manager", "add_bookmark", "NewStation|433500000|12500|FM")
            print(f"Response: {json.dumps(resp, indent=2)}")
            if "error" in resp:
                print("FAIL: add_bookmark returned error")
                all_passed = False
            elif resp.get("status") != "ok":
                print("FAIL: Expected status='ok'")
                all_passed = False
            else:
                print("PASS: add_bookmark succeeded")
            
            # Test 5: Verify bookmark was added
            print("\n--- Test 5: Verify added bookmark ---")
            resp = module_cmd("Frequency Manager", "get_bookmarks")
            print(f"Response: {resp}")
            bookmarks = resp.get("bookmarks", [])
            new_station = next((bm for bm in bookmarks if bm.get("name") == "NewStation"), None)
            if new_station is None:
                print("FAIL: NewStation not found in bookmarks")
                all_passed = False
            elif abs(new_station.get("frequency", 0) - 433500000.0) > 0.1:
                print(f"FAIL: Expected frequency=433500000, got {new_station.get('frequency')}")
                all_passed = False
            else:
                print("PASS: New bookmark verified")
            
            # Test 6: Apply bookmark
            print("\n--- Test 6: apply_bookmark ---")
            resp = module_cmd("Frequency Manager", "apply_bookmark", "TestStation1")
            print(f"Response: {json.dumps(resp, indent=2)}")
            if "error" in resp:
                print("FAIL: apply_bookmark returned error")
                all_passed = False
            elif resp.get("status") != "ok":
                print("FAIL: Expected status='ok'")
                all_passed = False
            else:
                print("PASS: apply_bookmark succeeded")
            
            # Test 7: Set current list
            print("\n--- Test 7: set_current_list ---")
            resp = module_cmd("Frequency Manager", "set_current_list", "EmptyList")
            print(f"Response: {json.dumps(resp, indent=2)}")
            if "error" in resp:
                print("FAIL: set_current_list returned error")
                all_passed = False
            elif resp.get("status") != "ok":
                print("FAIL: Expected status='ok'")
                all_passed = False
            else:
                print("PASS: set_current_list succeeded")
            
            # Verify list was changed
            resp = module_cmd("Frequency Manager", "get_current_list")
            if resp.get("current_list") != "EmptyList":
                print(f"FAIL: Expected current_list='EmptyList', got '{resp.get('current_list')}'")
                all_passed = False
            else:
                print("PASS: Current list verified as EmptyList")
            
            # Test 8: Try to start scanner on empty list (should fail)
            print("\n--- Test 8: start_scanner on empty list (should fail) ---")
            resp = module_cmd("Frequency Manager", "start_scanner")
            print(f"Response: {json.dumps(resp, indent=2)}")
            if "error" not in resp:
                print("FAIL: Expected error when starting scanner with no bookmarks")
                all_passed = False
            else:
                print("PASS: Scanner correctly refused to start on empty list")
            
            # Switch back to TestList for scanner tests
            module_cmd("Frequency Manager", "set_current_list", "TestList")
            
            # Test 9: Get scanner status (not scanning)
            print("\n--- Test 9: get_scanner_status (not scanning) ---")
            resp = module_cmd("Frequency Manager", "get_scanner_status")
            print(f"Response: {json.dumps(resp, indent=2)}")
            if "error" in resp:
                print("FAIL: get_scanner_status returned error")
                all_passed = False
            elif resp.get("scanning") != False:
                print("FAIL: Expected scanning=false")
                all_passed = False
            else:
                print("PASS: Scanner status shows not scanning")
            
            # Test 10: Remove bookmark
            print("\n--- Test 10: remove_bookmark ---")
            resp = module_cmd("Frequency Manager", "remove_bookmark", "NewStation")
            print(f"Response: {json.dumps(resp, indent=2)}")
            if "error" in resp:
                print("FAIL: remove_bookmark returned error")
                all_passed = False
            elif resp.get("status") != "ok":
                print("FAIL: Expected status='ok'")
                all_passed = False
            else:
                print("PASS: remove_bookmark succeeded")
            
            # Verify bookmark was removed
            resp = module_cmd("Frequency Manager", "get_bookmarks")
            bookmarks = resp.get("bookmarks", [])
            new_station = next((bm for bm in bookmarks if bm.get("name") == "NewStation"), None)
            if new_station is not None:
                print("FAIL: NewStation still exists after removal")
                all_passed = False
            else:
                print("PASS: Bookmark removal verified")
            
            # Test 11: Error cases
            print("\n--- Test 11: Error cases ---")
            
            # Invalid list name
            resp = module_cmd("Frequency Manager", "set_current_list", "NonExistentList")
            if "error" not in resp:
                print("FAIL: Expected error for non-existent list")
                all_passed = False
            else:
                print("PASS: Correctly rejected non-existent list")
            
            # Invalid bookmark name for apply
            resp = module_cmd("Frequency Manager", "apply_bookmark", "NonExistentBookmark")
            if "error" not in resp:
                print("FAIL: Expected error for non-existent bookmark")
                all_passed = False
            else:
                print("PASS: Correctly rejected non-existent bookmark")
            
            # Test 12: Unknown command
            print("\n--- Test 12: Unknown command ---")
            resp = module_cmd("Frequency Manager", "invalid_command")
            if "error" not in resp:
                print("FAIL: Expected error for unknown command")
                all_passed = False
            else:
                print("PASS: Correctly rejected unknown command")
            
            # Final summary
            print("\n" + "="*60)
            if all_passed:
                print("ALL TESTS PASSED!")
            else:
                print("SOME TESTS FAILED!")
            print("="*60)
            
            return all_passed
                
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
    sys.exit(0 if test_frequency_manager() else 1)
