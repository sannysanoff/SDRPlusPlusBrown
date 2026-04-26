# E2E Testing Framework for SDR++

This directory contains the E2E (end-to-end) testing framework for SDR++.

## Overview

The E2E tests verify SDR++ behavior by:
1. Launching SDR++ with specific configurations
2. Using the HTTP debug protocol to query state
3. Verifying expected behavior

## Requirements

- Python 3.7+
- SDR++ built with HTTP debug server support (`./sdrpp-cli build`)
- No external Python dependencies (uses only standard library)

## Running Tests

### Basic Usage

```bash
# Run the LSB bandwidth startup test
python3 e2e/test_lsb_startup.py
```

The test will:
1. Create a temporary config directory with LSB mode pre-configured
2. Launch SDR++ with HTTP debug server on port 8085
3. Query the radio module for VFO bandwidth
4. Verify the bandwidth is ~2.7 kHz (not the buggy 200 kHz)
5. Clean up temporary files

### Expected Output

```
============================================================
Testing LSB bandwidth at startup
============================================================
Waiting for SDR++ to start...
SDR++ ready! Checking bandwidth...
Demod: {'demod': 'LSB', 'id': 6}
Bandwidth: {'vfo_bandwidth': 2700.0, ...}

*** VFO bandwidth: 2700.0 Hz ***
*** PASS: Correct LSB bandwidth (~2.7kHz) ***
```

## Creating New Tests

**Important:** E2E tests must use only Python standard library (no external dependencies like `requests`). Use `urllib.request` for HTTP communication.

1. Create a new Python file in `e2e/`
2. Import `urllib.request` and `json` (standard library only)
3. Use HTTP POST to `/module/<instance>/command` endpoint
4. Parse JSON responses and verify expected values

#### Standard Library HTTP Helper Pattern

```python
import json
import urllib.request

def http_post(base_url, path, json_data=None):
    """Make HTTP POST request using only standard library"""
    url = f"{base_url}{path}"
    try:
        data = json.dumps(json_data).encode() if json_data else b''
        req = urllib.request.Request(
            url, 
            data=data, 
            headers={'Content-Type': 'application/json'}
        )
        with urllib.request.urlopen(req, timeout=5.0) as resp:
            return json.loads(resp.read().decode())
    except Exception as e:
        return {"error": str(e)}

def module_cmd(base_url, instance_name, cmd, args=""):
    """Send command to SDR++ module instance"""
    return http_post(
        base_url,
        f"/module/{instance_name.replace(' ', '%20')}/command",
        json_data={"cmd": cmd, "args": args}
    )

# Usage
response = module_cmd("http://localhost:8080", "Radio", "get_vfo_bandwidth")
print(f"Bandwidth: {response.get('vfo_bandwidth')}")
```

#### Complete Test Pattern

```python
import json
import urllib.request
import tempfile
import subprocess
import os
import time
import shutil

def test_my_feature():
    # Configuration
    port = 8080
    base_url = f"http://localhost:{port}"
    build_dir = "/path/to/cmake-build-debug"
    
    # Create temp config
    temp_dir = tempfile.mkdtemp()
    # ... write config.json ...
    
    # Launch SDR++
    env = os.environ.copy()
    env['QT_QPA_PLATFORM'] = 'offscreen'  # Headless mode
    
    proc = subprocess.Popen(
        ["./sdrpp", '-r', temp_dir, '--http', str(port)],
        cwd=build_dir,
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )
    
    # Wait for HTTP server
    for _ in range(150):  # 15 seconds timeout
        try:
            urllib.request.urlopen(f"{base_url}/", timeout=0.5)
            break
        except:
            time.sleep(0.1)
    else:
        proc.kill()
        raise RuntimeError("SDR++ failed to start")
    
    # Query and verify
    try:
        response = module_cmd(base_url, "Radio", "my_command")
        assert response.get('some_field') == expected_value
    finally:
        # Cleanup
        proc.terminate()
        proc.wait()
        shutil.rmtree(temp_dir)
```

## Debug Protocol Reference

See [docs/debug_protocol.md](../docs/debug_protocol.md) for full HTTP debug protocol documentation including:
- All available endpoints
- Module commands (Radio, File Source, NullAudioSink, etc.)
- ProcFS endpoints
- GUI automation endpoints

## Adding New Debug Commands

To add a command to a module:

1. Open the module's main header file (e.g., `decoder_modules/radio/src/radio_module.h`)
2. Find the `handleDebugCommand` method
3. Add a new `if (cmd == "my_command")` block
4. Return JSON string with results

Example:
```cpp
if (cmd == "get_my_value") {
    double value = someInternalValue;
    return "{\"value\": " + std::to_string(value) + "}";
}
```

## CI Integration

Tests can be run in CI by:

1. Building SDR++: `./sdrpp-cli build`
2. Running tests: `python3 e2e/test_lsb_startup.py`
3. Checking exit code (0 = pass, 1 = fail)

## Troubleshooting

### "SDR++ did not become ready"

- Check if another SDR++ instance is running (`./sdrpp-cli status`)
- Check the log file in the temp directory
- Ensure binary path is correct

### "Cannot connect to SDR++"

- Verify HTTP debug server is running on expected port
- Check firewall settings
- Try different port number

### "Module not found"

- Ensure `modulesDirectory` in config points to correct plugins path
- Verify modules are installed: `ls root_dev/inst/lib/sdrpp/plugins/`

## Current Tests

- `test_lsb_startup.py` - Verifies LSB VFO bandwidth is correct (~2.7 kHz) at startup
  - This test was created to verify the fix for the "100kHz bug" where VFO bandwidth
    stayed at default 200 kHz instead of the configured demodulator bandwidth
