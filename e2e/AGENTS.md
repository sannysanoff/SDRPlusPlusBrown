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

## Test Framework Library

`sdrpp_test_framework.py` provides reusable utilities for creating E2E tests:

### SDRPPTestInstance

Manages a test instance of SDR++:

```python
from sdrpp_test_framework import SDRPPTestInstance, create_lsb_radio_config

# Create config
main_config, radio_config = create_lsb_radio_config(
    radio_name="Radio",
    frequency=7100000,  # 40m band
    bandwidth=2700,     # LSB bandwidth
    sample_rate=48000
)

# Launch SDR++
with SDRPPTestInstance(
    config=main_config,
    radio_config=radio_config,
    binary_path="./cmake-build-debug/sdrpp",
    debug_port=8080,
    headless=True
) as sdrpp:
    # Query via debug API
    response = sdrpp.debug_cmd("Radio", "get_vfo_bandwidth")
    print(f"VFO bandwidth: {response['vfo_bandwidth']}")
```

### Creating New Tests

1. Create a new Python file in `e2e/`
2. Import `urllib.request` and `json` (standard library only)
3. Use HTTP POST to `/module/<instance>/command` endpoint
4. Parse JSON responses and verify expected values

Example pattern:
```python
import json
import urllib.request
import tempfile
import subprocess
import os

def test_my_feature():
    # Create temp config
    temp_dir = tempfile.mkdtemp()
    # ... write config files ...
    
    # Launch SDR++
    proc = subprocess.Popen([...], cwd=build_dir)
    
    # Wait for HTTP server
    # ... polling loop ...
    
    # Query and verify
    url = f"http://localhost:{port}/module/Radio/command"
    req = urllib.request.Request(url, data=json.dumps({
        "cmd": "my_command", "args": ""
    }).encode(), headers={'Content-Type': 'application/json'})
    
    with urllib.request.urlopen(req, timeout=5.0) as resp:
        data = json.loads(resp.read().decode())
        # Verify expected values
        assert data['some_field'] == expected_value
    
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
- Verify modules are installed: `ls root_dev/inst/lib/sdrpp_brown/plugins/`

## Current Tests

- `test_lsb_startup.py` - Verifies LSB VFO bandwidth is correct (~2.7 kHz) at startup
  - This test was created to verify the fix for the "100kHz bug" where VFO bandwidth
    stayed at default 200 kHz instead of the configured demodulator bandwidth
